#include "vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define INITIAL_STACK_CAPACITY 256
#define INITIAL_FRAME_CAPACITY 64

static void vm_reset_stack(VM *vm);
static void runtime_error(VM *vm, const char *message);
static bool call_value(VM *vm, CallFrame *caller, uint8_t dest_reg, Value callee, uint8_t arg_count, const uint8_t *arg_registers);
static bool call_function(VM *vm, CallFrame *caller, ObjFunction *function, uint8_t dest_reg, uint8_t arg_count, const uint8_t *arg_registers);
static InterpretResult run(VM *vm, Value *result_out);
static bool ensure_globals_capacity(VM *vm, size_t required);
static bool concatenate(VM *vm, Value *dest, Value left, Value right);
static void mark_roots(VM *vm);
static void mark_value(VM *vm, Value value);
static void mark_object(VM *vm, Obj *object);
static void trace_references(VM *vm);
static void blacken_object(VM *vm, Obj *object);
static void mark_array(VM *vm, ValueArray *array);
static void sweep(VM *vm);

static bool ensure_stack_capacity(VM *vm, int additional_slots) {
    int current_count = (int)(vm->stack_top - vm->stack);
    int required = current_count + additional_slots;
    if (required <= vm->stack_capacity) {
        return true;
    }
    int new_capacity = vm->stack_capacity == 0 ? INITIAL_STACK_CAPACITY : vm->stack_capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
        if (new_capacity <= 0) {
            return false;
        }
    }
    Value *new_stack = (Value *)realloc(vm->stack, (size_t)new_capacity * sizeof(Value));
    if (!new_stack) {
        return false;
    }
    int offset = (int)(vm->stack_top - vm->stack);
    vm->stack = new_stack;
    vm->stack_capacity = new_capacity;
    vm->stack_top = vm->stack + offset;
    return true;
}

static bool ensure_globals_capacity(VM *vm, size_t required) {
    if (required <= vm->global_capacity) {
        return true;
    }
    size_t new_capacity = vm->global_capacity == 0 ? 8 : vm->global_capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
        if (new_capacity < vm->global_capacity) {
            return false;
        }
    }
    Value *values = (Value *)malloc(new_capacity * sizeof(Value));
    bool *defined = (bool *)malloc(new_capacity * sizeof(bool));
    if (!values || !defined) {
        free(values);
        free(defined);
        return false;
    }
    for (size_t i = 0; i < vm->global_capacity; ++i) {
        values[i] = vm->globals[i];
        defined[i] = vm->global_defined[i];
    }
    for (size_t i = vm->global_capacity; i < new_capacity; ++i) {
        values[i] = value_make_null();
        defined[i] = false;
    }
    free(vm->globals);
    free(vm->global_defined);
    vm->globals = values;
    vm->global_defined = defined;
    vm->global_capacity = new_capacity;
    if (vm->global_count > vm->global_capacity) {
        vm->global_count = vm->global_capacity;
    }
    return true;
}

static void mark_value(VM *vm, Value value) {
    if (value_is_obj(value)) {
        mark_object(vm, value_as_obj(value));
    }
}

static void mark_array(VM *vm, ValueArray *array) {
    for (size_t i = 0; i < array->count; ++i) {
        mark_value(vm, array->values[i]);
    }
}

static void mark_roots(VM *vm) {
    for (Value *slot = vm->stack; slot && slot < vm->stack_top; ++slot) {
        mark_value(vm, *slot);
    }
    for (int i = 0; i < vm->frame_count; ++i) {
        mark_object(vm, (Obj *)vm->frames[i].function);
    }
    for (size_t i = 0; i < vm->global_count; ++i) {
        if (vm->global_defined[i]) {
            mark_value(vm, vm->globals[i]);
        }
    }
}

static void trace_references(VM *vm) {
    while (vm->gray_count > 0) {
        Obj *object = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, object);
    }
}

static void blacken_object(VM *vm, Obj *object) {
    switch (object->type) {
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *)object;
            if (function->name) {
                mark_object(vm, (Obj *)function->name);
            }
            mark_array(vm, &function->chunk.constants);
            break;
        }
        case OBJ_STRING:
            break;
        case OBJ_ARRAY: {
            ObjArray *array = (ObjArray *)object;
            for (size_t i = 0; i < array->elements.count; ++i) {
                mark_value(vm, array->elements.values[i]);
            }
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *)object;
            if (klass->name) {
                mark_object(vm, (Obj *)klass->name);
            }
            for (size_t i = 0; i < klass->method_count; ++i) {
                if (klass->methods[i].name) {
                    mark_object(vm, (Obj *)klass->methods[i].name);
                }
                mark_value(vm, klass->methods[i].value);
            }
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *)object;
            if (instance->klass) {
                mark_object(vm, (Obj *)instance->klass);
            }
            for (size_t i = 0; i < instance->field_count; ++i) {
                if (instance->fields[i].name) {
                    mark_object(vm, (Obj *)instance->fields[i].name);
                }
                mark_value(vm, instance->fields[i].value);
            }
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bound = (ObjBoundMethod *)object;
            mark_value(vm, bound->receiver);
            if (bound->method) {
                mark_object(vm, (Obj *)bound->method);
            }
            break;
        }
    }
}

static void sweep(VM *vm) {
    Obj *previous = NULL;
    Obj *object = vm->objects;
    while (object) {
        if (object->marked) {
            object->marked = false;
            previous = object;
            object = object->next;
            continue;
        }
        Obj *unreached = object;
        object = object->next;
        if (previous) {
            previous->next = object;
        } else {
            vm->objects = object;
        }
        obj_free(vm, unreached);
    }
}

static void mark_object(VM *vm, Obj *object) {
    if (!object || object->marked) {
        return;
    }
    object->marked = true;
    if (vm->gray_count + 1 > vm->gray_capacity) {
        int old_capacity = vm->gray_capacity;
        vm->gray_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        Obj **gray_stack = (Obj **)realloc(vm->gray_stack, (size_t)vm->gray_capacity * sizeof(Obj *));
        if (!gray_stack) {
            fprintf(stderr, "Failed to grow GC gray stack.\n");
            exit(EXIT_FAILURE);
        }
        vm->gray_stack = gray_stack;
    }
    vm->gray_stack[vm->gray_count++] = object;
}

void vm_collect_garbage(VM *vm) {
    if (!vm) {
        return;
    }
    mark_roots(vm);
    trace_references(vm);
    table_remove_white(&vm->strings);
    sweep(vm);
    size_t target = vm->bytes_allocated * 2;
    vm->next_gc = target < 1024 ? 1024 : target;
}

static bool ensure_frame_capacity(VM *vm, int additional_frames) {
    int required = vm->frame_count + additional_frames;
    if (required <= vm->frame_capacity) {
        return true;
    }
    int new_capacity = vm->frame_capacity == 0 ? INITIAL_FRAME_CAPACITY : vm->frame_capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
        if (new_capacity <= 0) {
            return false;
        }
    }
    CallFrame *frames = (CallFrame *)realloc(vm->frames, (size_t)new_capacity * sizeof(CallFrame));
    if (!frames) {
        return false;
    }
    vm->frames = frames;
    vm->frame_capacity = new_capacity;
    return true;
}

void vm_init(VM *vm) {
    if (!vm) {
        return;
    }
    vm->frames = NULL;
    vm->frame_capacity = 0;
    vm->frame_count = 0;
    vm->stack = NULL;
    vm->stack_capacity = 0;
    vm->stack_top = NULL;
    vm->globals = NULL;
    vm->global_defined = NULL;
    vm->global_count = 0;
    vm->global_capacity = 0;
    table_init(&vm->strings);
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;
    vm->gray_stack = NULL;
    vm->gray_count = 0;
    vm->gray_capacity = 0;
    if (!ensure_stack_capacity(vm, 0)) {
        fprintf(stderr, "Failed to allocate VM stack.\n");
        exit(EXIT_FAILURE);
    }
    vm_reset_stack(vm);
    if (!ensure_frame_capacity(vm, 0)) {
        fprintf(stderr, "Failed to allocate VM call frames.\n");
        exit(EXIT_FAILURE);
    }
}

void vm_free(VM *vm) {
    if (!vm) {
        return;
    }
    Obj *object = vm->objects;
    while (object) {
        Obj *next = object->next;
        obj_free(vm, object);
        object = next;
    }
    vm->objects = NULL;
    free(vm->gray_stack);
    vm->gray_stack = NULL;
    vm->gray_count = 0;
    vm->gray_capacity = 0;
    free(vm->frames);
    free(vm->stack);
    free(vm->globals);
    free(vm->global_defined);
    table_free(&vm->strings);
    vm->frames = NULL;
    vm->stack = NULL;
    vm->stack_top = NULL;
    vm->frame_capacity = 0;
    vm->stack_capacity = 0;
    vm->frame_count = 0;
    vm->globals = NULL;
    vm->global_defined = NULL;
    vm->global_count = 0;
    vm->global_capacity = 0;
    vm->bytes_allocated = 0;
    vm->next_gc = 0;
}

InterpretResult vm_interpret(VM *vm, ObjFunction *function, Value *result_out) {
    if (!vm || !function) {
        return INTERPRET_RUNTIME_ERROR;
    }
    if (function->arity != 0) {
        runtime_error(vm, "Can only directly interpret zero-arity functions.");
        return INTERPRET_RUNTIME_ERROR;
    }
    vm_reset_stack(vm);
    if (!ensure_frame_capacity(vm, 1)) {
        fprintf(stderr, "Unable to grow call frame stack.\n");
        return INTERPRET_RUNTIME_ERROR;
    }
    vm_push(vm, value_make_function(function));
    if (!call_function(vm, NULL, function, 0, 0, NULL)) {
        return INTERPRET_RUNTIME_ERROR;
    }
    return run(vm, result_out);
}

static void vm_reset_stack(VM *vm) {
    if (!vm || !vm->stack) {
        return;
    }
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
}

void vm_push(VM *vm, Value value) {
    if (!ensure_stack_capacity(vm, 1)) {
        fprintf(stderr, "Stack overflow.\n");
        exit(EXIT_FAILURE);
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM *vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

static void runtime_error(VM *vm, const char *message) {
    fprintf(stderr, "Runtime error: %s\n", message ? message : "Unknown error");
    for (int i = vm->frame_count - 1; i >= 0; --i) {
        CallFrame *frame = &vm->frames[i];
        ObjFunction *function = frame->function;
        size_t instruction_index = (size_t)(frame->ip - function->chunk.code);
        if (instruction_index > 0) {
            instruction_index -= 1;
        }
        int line = 0;
        if (instruction_index < (size_t)function->chunk.count) {
            line = function->chunk.lines[instruction_index];
        }
        const char *name = (function->name && function->name->chars) ? function->name->chars : "<script>";
        fprintf(stderr, "[line %d] in %s\n", line, name);
    }
    vm_reset_stack(vm);
}

static bool call_function(VM *vm, CallFrame *caller, ObjFunction *function, uint8_t dest_reg, uint8_t arg_count, const uint8_t *arg_registers) {
    if (function->arity != arg_count) {
        runtime_error(vm, "Incorrect number of arguments.");
        return false;
    }
    if (function->register_count < function->arity) {
        runtime_error(vm, "Function does not provide enough registers for its parameters.");
        return false;
    }
    if (!ensure_frame_capacity(vm, 1)) {
        fprintf(stderr, "Call frame overflow.\n");
        return false;
    }
    if (!ensure_stack_capacity(vm, function->register_count)) {
        fprintf(stderr, "Register stack overflow.\n");
        return false;
    }

    Value *registers = vm->stack_top;
    for (int i = 0; i < function->register_count; ++i) {
        registers[i] = value_make_null();
    }
    if (caller && arg_count > 0) {
        for (uint8_t i = 0; i < arg_count; ++i) {
            registers[i] = caller->registers[arg_registers[i]];
        }
    }

    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->registers = registers;
    frame->caller_registers = caller ? caller->registers : NULL;
    frame->return_reg = dest_reg;

    vm->stack_top += function->register_count;
    return true;
}

static bool call_value(VM *vm, CallFrame *caller, uint8_t dest_reg, Value callee, uint8_t arg_count, const uint8_t *arg_registers) {
    if (value_is_bound_method(callee)) {
        ObjBoundMethod *bound = value_as_bound_method(callee);
        ObjFunction *function = bound->method;
        if ((uint8_t)arg_count != (uint8_t)(function->arity - 1)) {
            runtime_error(vm, "Incorrect number of arguments.");
            return false;
        }
        if (!caller) {
            runtime_error(vm, "Invalid call context.");
            return false;
        }
        uint8_t extended[UINT8_MAX];
        extended[0] = dest_reg;
        for (uint8_t i = 0; i < arg_count; ++i) {
            extended[i + 1] = arg_registers[i];
        }
        caller->registers[dest_reg] = bound->receiver;
        return call_function(vm, caller, function, dest_reg, (uint8_t)(arg_count + 1), extended);
    }

    if (value_is_class(callee)) {
        if (!caller) {
            runtime_error(vm, "Cannot instantiate class in this context.");
            return false;
        }
        ObjClass *klass = value_as_class(callee);
        ObjInstance *instance = obj_instance_new(vm, klass);
        if (!instance) {
            runtime_error(vm, "Failed to allocate instance.");
            return false;
        }
        Value instance_value = value_make_instance(instance);
        caller->registers[dest_reg] = instance_value;

        ObjString *ctor_name = obj_string_copy(vm, "constructor", strlen("constructor"));
        Value method_value;
        if (obj_class_find_method(klass, ctor_name, &method_value)) {
            if (!value_is_function(method_value)) {
                runtime_error(vm, "Constructor is not callable.");
                return false;
            }
            ObjFunction *function = value_as_function(method_value);
            if ((uint8_t)(arg_count + 1) != (uint8_t)function->arity) {
                runtime_error(vm, "Incorrect number of arguments.");
                return false;
            }
            uint8_t extended[UINT8_MAX];
            extended[0] = dest_reg;
            for (uint8_t i = 0; i < arg_count; ++i) {
                extended[i + 1] = arg_registers[i];
            }
            return call_function(vm, caller, function, dest_reg, (uint8_t)(arg_count + 1), extended);
        }
        if (arg_count > 0) {
            runtime_error(vm, "Constructor not defined.");
            return false;
        }
        return true;
    }

    if (value_is_function(callee)) {
        return call_function(vm, caller, value_as_function(callee), dest_reg, arg_count, arg_registers);
    }
    runtime_error(vm, "Attempted to call a non-function value.");
    return false;
}

static bool concatenate(VM *vm, Value *dest, Value left, Value right) {
    if (!value_is_string(left) || !value_is_string(right)) {
        return false;
    }
    ObjString *a = value_as_string(left);
    ObjString *b = value_as_string(right);
    size_t length = a->length + b->length;
    char *chars = (char *)malloc(length + 1);
    if (!chars) {
        fprintf(stderr, "Failed to concatenate strings.\n");
        exit(EXIT_FAILURE);
    }
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    ObjString *result = obj_string_take(vm, chars, length);
    *dest = value_make_string(result);
    return true;
}

static uint8_t read_byte(CallFrame *frame) {
    return *frame->ip++;
}

static uint16_t read_short(CallFrame *frame) {
    frame->ip += 2;
    uint16_t high = (uint16_t)frame->ip[-2];
    uint16_t low = (uint16_t)frame->ip[-1];
    return (uint16_t)((high << 8) | low);
}

static InterpretResult run(VM *vm, Value *result_out) {
    uint8_t argument_registers[UINT8_MAX];
    for (;;) {
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        Value *registers = frame->registers;
        uint8_t instruction = read_byte(frame);
        switch (instruction) {
            case OP_LOAD_CONST: {
                uint8_t dest = read_byte(frame);
                uint16_t index = read_short(frame);
                registers[dest] = chunk_get_constant(&frame->function->chunk, index);
                break;
            }
            case OP_LOAD_NULL: {
                uint8_t dest = read_byte(frame);
                registers[dest] = value_make_null();
                break;
            }
            case OP_LOAD_TRUE: {
                uint8_t dest = read_byte(frame);
                registers[dest] = value_make_bool(true);
                break;
            }
            case OP_LOAD_FALSE: {
                uint8_t dest = read_byte(frame);
                registers[dest] = value_make_bool(false);
                break;
            }
            case OP_MOVE: {
                uint8_t dest = read_byte(frame);
                uint8_t src = read_byte(frame);
                registers[dest] = registers[src];
                break;
            }
            case OP_ADD:
            case OP_SUBTRACT:
            case OP_MULTIPLY:
            case OP_DIVIDE:
            case OP_EQUAL:
            case OP_GREATER:
            case OP_LESS: {
                uint8_t dest = read_byte(frame);
                uint8_t left = read_byte(frame);
                uint8_t right = read_byte(frame);
                Value a = registers[left];
                Value b = registers[right];
                switch (instruction) {
                    case OP_ADD:
                        if (value_is_array(a)) {
                            ObjArray *left_array = value_as_array(a);
                            ObjArray *result = obj_array_copy(vm, left_array->elements.values, left_array->elements.count);
                            if (!result) {
                                runtime_error(vm, "Failed to allocate array.");
                                return INTERPRET_RUNTIME_ERROR;
                            }
                            Value array_value = value_make_array(result);
                            vm_push(vm, array_value);
                            if (value_is_array(b)) {
                                ObjArray *right_array = value_as_array(b);
                                if (!obj_array_extend(vm, result, right_array->elements.values, right_array->elements.count)) {
                                    vm_pop(vm);
                                    runtime_error(vm, "Failed to extend array.");
                                    return INTERPRET_RUNTIME_ERROR;
                                }
                            } else {
                                if (!obj_array_append(vm, result, b)) {
                                    vm_pop(vm);
                                    runtime_error(vm, "Failed to append to array.");
                                    return INTERPRET_RUNTIME_ERROR;
                                }
                            }
                            registers[dest] = array_value;
                            vm_pop(vm);
                            break;
                        }
                        if (value_is_array(b)) {
                            runtime_error(vm, "Left operand must be an array for array addition.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        if (value_is_string(a) && value_is_string(b)) {
                            if (!concatenate(vm, &registers[dest], a, b)) {
                                runtime_error(vm, "Failed to concatenate strings.");
                                return INTERPRET_RUNTIME_ERROR;
                            }
                            break;
                        }
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers or strings.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_number(value_as_number(a) + value_as_number(b));
                        break;
                    case OP_SUBTRACT:
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_number(value_as_number(a) - value_as_number(b));
                        break;
                    case OP_MULTIPLY:
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_number(value_as_number(a) * value_as_number(b));
                        break;
                    case OP_DIVIDE:
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_number(value_as_number(a) / value_as_number(b));
                        break;
                    case OP_EQUAL:
                        registers[dest] = value_make_bool(value_equals(a, b));
                        break;
                    case OP_GREATER:
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_bool(value_as_number(a) > value_as_number(b));
                        break;
                    case OP_LESS:
                        if (!value_is_number(a) || !value_is_number(b)) {
                            runtime_error(vm, "Operands must be numbers.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        registers[dest] = value_make_bool(value_as_number(a) < value_as_number(b));
                        break;
                    default:
                        break;
                }
                break;
            }
            case OP_NOT: {
                uint8_t dest = read_byte(frame);
                uint8_t operand = read_byte(frame);
                registers[dest] = value_make_bool(!value_is_truthy(registers[operand]));
                break;
            }
            case OP_NEGATE: {
                uint8_t dest = read_byte(frame);
                uint8_t operand = read_byte(frame);
                Value value = registers[operand];
                if (!value_is_number(value)) {
                    runtime_error(vm, "Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                registers[dest] = value_make_number(-value_as_number(value));
                break;
            }
            case OP_JUMP: {
                uint16_t offset = read_short(frame);
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint8_t condition = read_byte(frame);
                uint16_t offset = read_short(frame);
                if (!value_is_truthy(registers[condition])) {
                    frame->ip += offset;
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = read_short(frame);
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                uint8_t dest = read_byte(frame);
                uint8_t callee_reg = read_byte(frame);
                uint8_t arg_count = read_byte(frame);
                for (uint8_t i = 0; i < arg_count; ++i) {
                    argument_registers[i] = read_byte(frame);
                }
                Value callee = registers[callee_reg];
                if (!call_value(vm, frame, dest, callee, arg_count, argument_registers)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                continue;
            }
            case OP_BUILD_ARRAY: {
                uint8_t dest = read_byte(frame);
                uint8_t element_count = read_byte(frame);
                ObjArray *array = obj_array_new(vm);
                if (!array) {
                    runtime_error(vm, "Failed to allocate array.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value array_value = value_make_array(array);
                vm_push(vm, array_value);
                for (uint8_t i = 0; i < element_count; ++i) {
                    uint8_t source_reg = read_byte(frame);
                    if (!obj_array_append(vm, array, registers[source_reg])) {
                        vm_pop(vm);
                        runtime_error(vm, "Failed to append to array.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                registers[dest] = array_value;
                vm_pop(vm);
                break;
            }
            case OP_ARRAY_GET: {
                uint8_t dest = read_byte(frame);
                uint8_t array_reg = read_byte(frame);
                uint8_t index_reg = read_byte(frame);
                Value array_value = registers[array_reg];
                Value index_value = registers[index_reg];
                if (!value_is_array(array_value)) {
                    runtime_error(vm, "Operand is not an array.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!value_is_number(index_value)) {
                    runtime_error(vm, "Array index must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double index_double = value_as_number(index_value);
                if (index_double < 0.0 || index_double > (double)SIZE_MAX) {
                    runtime_error(vm, "Array index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                size_t index = (size_t)index_double;
                if ((double)index != index_double) {
                    runtime_error(vm, "Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray *array_obj = value_as_array(array_value);
                if (index >= array_obj->elements.count) {
                    runtime_error(vm, "Array index out of range.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                registers[dest] = array_obj->elements.values[index];
                break;
            }
            case OP_GET_PROPERTY: {
                uint8_t dest = read_byte(frame);
                uint8_t object_reg = read_byte(frame);
                uint16_t name_index = read_short(frame);
                Value object = registers[object_reg];
                Value name_value = chunk_get_constant(&frame->function->chunk, name_index);
                if (!value_is_string(name_value)) {
                    runtime_error(vm, "Property name must be a string constant.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString *name = value_as_string(name_value);

                if (value_is_instance(object)) {
                    ObjInstance *instance = value_as_instance(object);
                    Value field;
                    if (obj_instance_get_field(instance, name, &field)) {
                        registers[dest] = field;
                        break;
                    }
                    Value method_value;
                    if (obj_class_find_method(instance->klass, name, &method_value)) {
                        if (!value_is_function(method_value)) {
                            runtime_error(vm, "Method value is not callable.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        ObjBoundMethod *bound = obj_bound_method_new(vm, object, value_as_function(method_value));
                        registers[dest] = value_make_bound_method(bound);
                        break;
                    }
                    runtime_error(vm, "Undefined property on instance.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (value_is_class(object)) {
                    ObjClass *klass = value_as_class(object);
                    Value method_value;
                    if (obj_class_find_method(klass, name, &method_value)) {
                        registers[dest] = method_value;
                        break;
                    }
                    runtime_error(vm, "Undefined property on class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                runtime_error(vm, "Only instances and classes have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }
            case OP_SET_PROPERTY: {
                uint8_t object_reg = read_byte(frame);
                uint16_t name_index = read_short(frame);
                uint8_t value_reg = read_byte(frame);
                Value object = registers[object_reg];
                Value name_value = chunk_get_constant(&frame->function->chunk, name_index);
                if (!value_is_string(name_value)) {
                    runtime_error(vm, "Property name must be a string constant.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString *name = value_as_string(name_value);
                if (!value_is_instance(object)) {
                    runtime_error(vm, "Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance *instance = value_as_instance(object);
                if (!obj_instance_set_field(vm, instance, name, registers[value_reg])) {
                    runtime_error(vm, "Failed to set instance field.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_CLASS: {
                uint8_t dest = read_byte(frame);
                uint16_t name_index = read_short(frame);
                Value name_value = chunk_get_constant(&frame->function->chunk, name_index);
                if (!value_is_string(name_value)) {
                    runtime_error(vm, "Class name must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString *name = value_as_string(name_value);
                ObjClass *klass = obj_class_new(vm, name);
                registers[dest] = value_make_class(klass);
                break;
            }
            case OP_METHOD: {
                uint8_t class_reg = read_byte(frame);
                uint16_t name_index = read_short(frame);
                uint8_t method_reg = read_byte(frame);
                Value class_value = registers[class_reg];
                Value name_value = chunk_get_constant(&frame->function->chunk, name_index);
                if (!value_is_class(class_value)) {
                    runtime_error(vm, "OP_METHOD target is not a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!value_is_string(name_value)) {
                    runtime_error(vm, "Method name must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjClass *klass = value_as_class(class_value);
                ObjString *name = value_as_string(name_value);
                Value method = registers[method_reg];
                if (!obj_class_define_method(vm, klass, name, method)) {
                    runtime_error(vm, "Failed to define method.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_INVOKE: {
                uint8_t dest = read_byte(frame);
                uint8_t object_reg = read_byte(frame);
                uint16_t name_index = read_short(frame);
                uint8_t arg_count = read_byte(frame);
                for (uint8_t i = 0; i < arg_count; ++i) {
                    argument_registers[i] = read_byte(frame);
                }
                Value receiver = registers[object_reg];
                Value name_value = chunk_get_constant(&frame->function->chunk, name_index);
                if (!value_is_string(name_value)) {
                    runtime_error(vm, "Method name must be a string.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString *name = value_as_string(name_value);

                Value callee;
                bool pushed_bound = false;
                if (value_is_instance(receiver)) {
                    ObjInstance *instance = value_as_instance(receiver);
                    Value field;
                    if (obj_instance_get_field(instance, name, &field)) {
                        callee = field;
                    } else {
                        Value method_value;
                        if (!obj_class_find_method(instance->klass, name, &method_value)) {
                            runtime_error(vm, "Undefined method on instance.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        if (!value_is_function(method_value)) {
                            runtime_error(vm, "Method value is not callable.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        ObjBoundMethod *bound = obj_bound_method_new(vm, receiver, value_as_function(method_value));
                        Value bound_value = value_make_bound_method(bound);
                        vm_push(vm, bound_value);
                        registers[dest] = bound_value;
                        callee = bound_value;
                        pushed_bound = true;
                    }
                } else if (value_is_class(receiver)) {
                    ObjClass *klass = value_as_class(receiver);
                    Value method_value;
                    if (!obj_class_find_method(klass, name, &method_value)) {
                        runtime_error(vm, "Undefined method on class.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    callee = method_value;
                } else {
                    runtime_error(vm, "Only instances and classes have methods.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!call_value(vm, frame, dest, callee, arg_count, argument_registers)) {
                    if (pushed_bound) {
                        vm_pop(vm);
                    }
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (pushed_bound) {
                    vm_pop(vm);
                }
                continue;
            }
            case OP_RETURN: {
                uint8_t src = read_byte(frame);
                Value result = registers[src];
                Value *callee_registers = frame->registers;
                Value *caller_registers = frame->caller_registers;
                uint8_t return_reg = frame->return_reg;

                vm->frame_count--;
                vm->stack_top = callee_registers;

                if (vm->frame_count == 0) {
                    if (result_out) {
                        *result_out = result;
                    }
                    vm_reset_stack(vm);
                    return INTERPRET_OK;
                }

                CallFrame *caller = &vm->frames[vm->frame_count - 1];
                if (caller_registers) {
                    caller_registers[return_reg] = result;
                }
                vm->stack_top = caller->registers + caller->function->register_count;
                continue;
            }
            case OP_GET_GLOBAL: {
                uint8_t dest = read_byte(frame);
                uint16_t slot = read_short(frame);
                if (slot >= vm->global_count || !vm->global_defined[slot]) {
                    runtime_error(vm, "Undefined global variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                registers[dest] = vm->globals[slot];
                break;
            }
            case OP_DEFINE_GLOBAL: {
                uint8_t src = read_byte(frame);
                uint16_t slot = read_short(frame);
                size_t required = (size_t)slot + 1;
                if (!ensure_globals_capacity(vm, required)) {
                    runtime_error(vm, "Unable to expand globals array.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (required > vm->global_count) {
                    vm->global_count = required;
                }
                vm->globals[slot] = registers[src];
                vm->global_defined[slot] = true;
                break;
            }
            case OP_SET_GLOBAL: {
                uint8_t src = read_byte(frame);
                uint16_t slot = read_short(frame);
                if (slot >= vm->global_count || !vm->global_defined[slot]) {
                    runtime_error(vm, "Undefined global variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm->globals[slot] = registers[src];
                break;
            }
            default:
                runtime_error(vm, "Unknown opcode.");
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}

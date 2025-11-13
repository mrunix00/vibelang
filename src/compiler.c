#include "compiler.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "object.h"
#include "value.h"

#define MAX_LOCALS 256

typedef struct {
    const char *name;
    int depth;
    bool is_initialized;
    int reg;
} Local;

typedef struct {
    const char **names;
    size_t count;
    size_t capacity;
} GlobalTable;

typedef struct Compilation Compilation;
typedef struct Compiler Compiler;

struct Compilation {
    VM *vm;
    GlobalTable globals;
};

typedef enum {
    FUNCTION_TYPE_SCRIPT,
    FUNCTION_TYPE_FUNCTION,
    FUNCTION_TYPE_METHOD,
    FUNCTION_TYPE_INITIALIZER
} FunctionType;

typedef struct {
    int reg;
    bool is_temp;
} RegisterResult;

static inline RegisterResult register_result_make(int reg, bool is_temp) {
    RegisterResult result;
    result.reg = reg;
    result.is_temp = is_temp;
    return result;
}

static inline RegisterResult register_result_invalid(void) {
    return register_result_make(-1, false);
}

static inline bool register_result_is_valid(RegisterResult result) {
    return result.reg >= 0;
}

struct Compiler {
    VM *vm;
    const Program *program;
    Compiler *enclosing;
    ObjFunction *function;
    GlobalTable *globals;
    Compilation *compilation;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
    bool has_pending_expression;
    int stack_depth;
    bool pending_has_value;
    RegisterResult pending_value;
    FunctionType type;
};

static void compiler_errorf(char **error_message, const char *format, ...);
static void global_table_init(GlobalTable *table);
static void global_table_free(GlobalTable *table);
static int global_table_find(const GlobalTable *table, const char *name);
static bool global_table_add(GlobalTable *table, const char *name, uint16_t *index_out, char **error_message);
static void compiler_init(Compiler *compiler, Compilation *compilation, Compiler *enclosing, ObjFunction *function, const Program *program, FunctionType type);
static bool compile_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_expression(Compiler *compiler, const Expression *expression, char **error_message);
static Chunk *current_chunk(Compiler *compiler);
static void emit_byte(Compiler *compiler, uint8_t byte);
static bool patch_jump(Compiler *compiler, int offset, char **error_message);
static bool emit_return(Compiler *compiler, char **error_message);
static bool begin_scope(Compiler *compiler);
static void end_scope(Compiler *compiler);
static int add_local(Compiler *compiler, const char *name, char **error_message);
static int resolve_local(Compiler *compiler, const char *name, bool for_assignment, char **error_message);
static bool compile_block(Compiler *compiler, const StatementList *statements, char **error_message);
static bool compile_let_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_expression_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_if_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_while_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_function_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_return_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_assignment(Compiler *compiler, const Expression *expression, char **error_message);
static bool compile_call(Compiler *compiler, const Expression *expression, char **error_message);
static bool compile_array_literal(Compiler *compiler, const Expression *expression, char **error_message);
static bool compile_index_expression(Compiler *compiler, const Expression *expression, char **error_message);
static bool compile_class_statement(Compiler *compiler, const Statement *statement, char **error_message);
static bool compile_method(Compiler *compiler, const ClassMethod *method, int class_reg, char **error_message);
static void discard_pending_expression(Compiler *compiler);

static void compiler_errorf(char **error_message, const char *format, ...) {
    if (!error_message || *error_message) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return;
    }
    size_t size = (size_t)needed + 1;
    char *buffer = (char *)malloc(size);
    if (!buffer) {
        va_end(args);
        return;
    }
    vsnprintf(buffer, size, format, args);
    va_end(args);
    *error_message = buffer;
}

static void global_table_init(GlobalTable *table) {
    if (!table) {
        return;
    }
    table->names = NULL;
    table->count = 0;
    table->capacity = 0;
}

static void global_table_free(GlobalTable *table) {
    if (!table) {
        return;
    }
    free(table->names);
    table->names = NULL;
    table->count = 0;
    table->capacity = 0;
}

static int global_table_find(const GlobalTable *table, const char *name) {
    if (!table || !name) {
        return -1;
    }
    for (size_t i = 0; i < table->count; ++i) {
        const char *candidate = table->names[i];
        if (candidate && strcmp(candidate, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool global_table_add(GlobalTable *table, const char *name, uint16_t *index_out, char **error_message) {
    if (!table || !name) {
        return false;
    }
    if (global_table_find(table, name) >= 0) {
        compiler_errorf(error_message, "Global '%s' already defined.", name);
        return false;
    }
    if (table->count >= UINT16_MAX) {
        compiler_errorf(error_message, "Too many global variables defined.");
        return false;
    }
    if (table->count == table->capacity) {
        size_t new_capacity = table->capacity == 0 ? 8 : table->capacity * 2;
        const char **names = (const char **)realloc(table->names, new_capacity * sizeof(const char *));
        if (!names) {
            compiler_errorf(error_message, "Out of memory while growing globals table.");
            return false;
        }
        table->names = names;
        table->capacity = new_capacity;
    }
    table->names[table->count] = name;
    if (index_out) {
        *index_out = (uint16_t)table->count;
    }
    table->count++;
    return true;
}

static Chunk *current_chunk(Compiler *compiler) {
    return &compiler->function->chunk;
}

static void compiler_init(Compiler *compiler, Compilation *compilation, Compiler *enclosing, ObjFunction *function, const Program *program, FunctionType type) {
    compiler->vm = compilation->vm;
    compiler->program = program;
    compiler->enclosing = enclosing;
    compiler->function = function;
    compiler->globals = &compilation->globals;
    compiler->compilation = compilation;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->has_pending_expression = false;
    compiler->stack_depth = 0;
    compiler->pending_has_value = false;
    compiler->pending_value = register_result_invalid();
    compiler->function->register_count = 0;
    compiler->type = type;
}

static void emit_byte(Compiler *compiler, uint8_t byte) {
    chunk_write(current_chunk(compiler), byte, 0);
}

static bool update_register_usage(Compiler *compiler, char **error_message) {
    int total = compiler->local_count + compiler->stack_depth;
    if (total > compiler->function->register_count) {
        compiler->function->register_count = total;
    }
    if (compiler->function->register_count > UINT8_MAX) {
        compiler_errorf(error_message, "Function requires more than %u registers.", (unsigned)UINT8_MAX);
        return false;
    }
    return true;
}

static int stack_base(const Compiler *compiler) {
    return compiler->local_count;
}

static int stack_register(const Compiler *compiler, int depth_index) {
    return stack_base(compiler) + depth_index;
}

static int stack_top_register(const Compiler *compiler, int distance) {
    return stack_register(compiler, compiler->stack_depth - 1 - distance);
}

static bool push_stack_slot(Compiler *compiler, char **error_message, int *dest_out) {
    int dest = stack_register(compiler, compiler->stack_depth);
    compiler->stack_depth++;
    if (!update_register_usage(compiler, error_message)) {
        return false;
    }
    if (dest_out) {
        *dest_out = dest;
    }
    return true;
}

static void pop_stack_slots(Compiler *compiler, int count) {
    compiler->stack_depth -= count;
    if (compiler->stack_depth < 0) {
        compiler->stack_depth = 0;
    }
}

static bool emit_op_load_constant(Compiler *compiler, int dest, Value value, char **error_message) {
    Chunk *chunk = current_chunk(compiler);
    uint16_t index = chunk_add_constant(chunk, value);
    emit_byte(compiler, OP_LOAD_CONST);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)((index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(index & 0xFF));
    (void)error_message;
    return true;
}

static void emit_op_load_null(Compiler *compiler, int dest) {
    emit_byte(compiler, OP_LOAD_NULL);
    emit_byte(compiler, (uint8_t)dest);
}

static void emit_op_load_bool(Compiler *compiler, int dest, bool value) {
    emit_byte(compiler, value ? OP_LOAD_TRUE : OP_LOAD_FALSE);
    emit_byte(compiler, (uint8_t)dest);
}

static void emit_op_move(Compiler *compiler, int dest, int src) {
    emit_byte(compiler, OP_MOVE);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)src);
}

static void emit_op_unary(Compiler *compiler, OpCode opcode, int dest, int operand) {
    emit_byte(compiler, opcode);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)operand);
}

static void emit_op_binary(Compiler *compiler, OpCode opcode, int dest, int left, int right) {
    emit_byte(compiler, opcode);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)left);
    emit_byte(compiler, (uint8_t)right);
}

static void emit_op_get_global(Compiler *compiler, int dest, uint16_t slot) {
    emit_byte(compiler, OP_GET_GLOBAL);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)((slot >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(slot & 0xFF));
}

static void emit_op_set_global(Compiler *compiler, int src, uint16_t slot) {
    emit_byte(compiler, OP_SET_GLOBAL);
    emit_byte(compiler, (uint8_t)src);
    emit_byte(compiler, (uint8_t)((slot >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(slot & 0xFF));
}

static void emit_op_define_global(Compiler *compiler, int src, uint16_t slot) {
    emit_byte(compiler, OP_DEFINE_GLOBAL);
    emit_byte(compiler, (uint8_t)src);
    emit_byte(compiler, (uint8_t)((slot >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(slot & 0xFF));
}

static void emit_op_call(Compiler *compiler, int dest, int callee, uint8_t arg_count, const uint8_t *args) {
    emit_byte(compiler, OP_CALL);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)callee);
    emit_byte(compiler, arg_count);
    for (uint8_t i = 0; i < arg_count; ++i) {
        emit_byte(compiler, args[i]);
    }
}

static void emit_op_build_array(Compiler *compiler, int dest, uint8_t element_count, const uint8_t *elements) {
    emit_byte(compiler, OP_BUILD_ARRAY);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, element_count);
    for (uint8_t i = 0; i < element_count; ++i) {
        emit_byte(compiler, elements[i]);
    }
}

static void emit_op_array_get(Compiler *compiler, int dest, int array_reg, int index_reg) {
    emit_byte(compiler, OP_ARRAY_GET);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)array_reg);
    emit_byte(compiler, (uint8_t)index_reg);
}

static bool make_string_constant(Compiler *compiler, const char *text, uint16_t *index_out, char **error_message) {
    ObjString *string = obj_string_copy(compiler->vm, text, text ? strlen(text) : 0);
    if (!string) {
        compiler_errorf(error_message, "Out of memory while creating string constant.");
        return false;
    }
    Value value = value_make_string(string);
    vm_push(compiler->vm, value);
    uint16_t index = chunk_add_constant(current_chunk(compiler), value);
    vm_pop(compiler->vm);
    if (index_out) {
        *index_out = index;
    }
    return true;
}

static void emit_op_class(Compiler *compiler, int dest, uint16_t name_index) {
    emit_byte(compiler, OP_CLASS);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)((name_index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(name_index & 0xFF));
}

static void emit_op_method(Compiler *compiler, int class_reg, uint16_t name_index, int method_reg) {
    emit_byte(compiler, OP_METHOD);
    emit_byte(compiler, (uint8_t)class_reg);
    emit_byte(compiler, (uint8_t)((name_index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(name_index & 0xFF));
    emit_byte(compiler, (uint8_t)method_reg);
}

static void emit_op_get_property(Compiler *compiler, int dest, int object_reg, uint16_t name_index) {
    emit_byte(compiler, OP_GET_PROPERTY);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)object_reg);
    emit_byte(compiler, (uint8_t)((name_index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(name_index & 0xFF));
}

static void emit_op_set_property(Compiler *compiler, int object_reg, uint16_t name_index, int value_reg) {
    emit_byte(compiler, OP_SET_PROPERTY);
    emit_byte(compiler, (uint8_t)object_reg);
    emit_byte(compiler, (uint8_t)((name_index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(name_index & 0xFF));
    emit_byte(compiler, (uint8_t)value_reg);
}

static void emit_op_invoke(Compiler *compiler, int dest, int object_reg, uint16_t name_index, uint8_t arg_count, const uint8_t *args) {
    emit_byte(compiler, OP_INVOKE);
    emit_byte(compiler, (uint8_t)dest);
    emit_byte(compiler, (uint8_t)object_reg);
    emit_byte(compiler, (uint8_t)((name_index >> 8) & 0xFF));
    emit_byte(compiler, (uint8_t)(name_index & 0xFF));
    emit_byte(compiler, arg_count);
    for (uint8_t i = 0; i < arg_count; ++i) {
        emit_byte(compiler, args[i]);
    }
}

static int emit_jump_unconditional(Compiler *compiler) {
    emit_byte(compiler, OP_JUMP);
    emit_byte(compiler, 0xFF);
    emit_byte(compiler, 0xFF);
    return current_chunk(compiler)->count - 2;
}

static int emit_jump_if_false(Compiler *compiler, int condition_reg) {
    emit_byte(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, (uint8_t)condition_reg);
    emit_byte(compiler, 0xFF);
    emit_byte(compiler, 0xFF);
    return current_chunk(compiler)->count - 2;
}

static bool patch_jump(Compiler *compiler, int offset, char **error_message) {
    Chunk *chunk = current_chunk(compiler);
    int jump = chunk->count - offset - 2;
    if (jump < 0 || jump > UINT16_MAX) {
        compiler_errorf(error_message, "Jump offset out of range.");
        return false;
    }
    chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
    return true;
}

static bool emit_loop_instruction(Compiler *compiler, int loop_start, char **error_message) {
    Chunk *chunk = current_chunk(compiler);
    emit_byte(compiler, OP_LOOP);
    emit_byte(compiler, 0);
    emit_byte(compiler, 0);
    int offset = chunk->count - loop_start;
    if (offset < 0 || offset > UINT16_MAX) {
        compiler_errorf(error_message, "Loop body too large.");
        return false;
    }
    chunk->code[chunk->count - 2] = (uint8_t)((offset >> 8) & 0xFF);
    chunk->code[chunk->count - 1] = (uint8_t)(offset & 0xFF);
    return true;
}

static void emit_return_value(Compiler *compiler, int reg) {
    emit_byte(compiler, OP_RETURN);
    emit_byte(compiler, (uint8_t)reg);
}

static bool emit_return(Compiler *compiler, char **error_message) {
    if (!compiler->enclosing && compiler->pending_has_value) {
        emit_return_value(compiler, compiler->pending_value.reg);
        compiler->pending_has_value = false;
        compiler->has_pending_expression = false;
        compiler->stack_depth = 0;
        return true;
    }
    if (compiler->type == FUNCTION_TYPE_INITIALIZER && compiler->local_count > 0) {
        emit_return_value(compiler, compiler->locals[0].reg);
        return true;
    }
    int dest = 0;
    if (!push_stack_slot(compiler, error_message, &dest)) {
        return false;
    }
    emit_op_load_null(compiler, dest);
    emit_return_value(compiler, dest);
    pop_stack_slots(compiler, 1);
    return true;
}

static bool begin_scope(Compiler *compiler) {
    compiler->scope_depth++;
    return true;
}

static void end_scope(Compiler *compiler) {
    compiler->scope_depth--;
    while (compiler->local_count > 0 && compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {
        compiler->local_count--;
    }
}

static int add_local(Compiler *compiler, const char *name, char **error_message) {
    if (compiler->local_count >= MAX_LOCALS) {
        compiler_errorf(error_message, "Too many local variables.");
        return -1;
    }
    if (compiler->local_count >= UINT8_MAX) {
        compiler_errorf(error_message, "Too many registers required for locals.");
        return -1;
    }
    Local *local = &compiler->locals[compiler->local_count];
    local->name = name;
    local->depth = -1;
    local->is_initialized = false;
    local->reg = compiler->local_count;
    compiler->local_count++;
    if (compiler->local_count > compiler->function->register_count) {
        compiler->function->register_count = compiler->local_count;
    }
    if (compiler->function->register_count > UINT8_MAX) {
        compiler_errorf(error_message, "Function requires more than %u registers.", (unsigned)UINT8_MAX);
        return -1;
    }
    return compiler->local_count - 1;
}

static int resolve_local(Compiler *compiler, const char *name, bool for_assignment, char **error_message) {
    for (int i = compiler->local_count - 1; i >= 0; --i) {
        Local *local = &compiler->locals[i];
        if (local->name && strcmp(local->name, name) == 0) {
            if (!local->is_initialized && !for_assignment) {
                compiler_errorf(error_message, "Cannot read local variable '%s' before initialization.", name);
                return -1;
            }
            return i;
        }
    }
    return -1;
}

static bool compile_literal_string(Compiler *compiler, const char *text, char **error_message) {
    size_t length = text ? strlen(text) : 0;
    ObjString *string = obj_string_copy(compiler->vm, text, length);
    int dest = 0;
    if (!push_stack_slot(compiler, error_message, &dest)) {
        return false;
    }
    return emit_op_load_constant(compiler, dest, value_make_string(string), error_message);
}

static bool compile_expression(Compiler *compiler, const Expression *expression, char **error_message) {
    if (!expression) {
        compiler_errorf(error_message, "Null expression encountered during compilation.");
        return false;
    }
    switch (expression->type) {
        case EXPR_LITERAL_NUMBER: {
            int dest = 0;
            if (!push_stack_slot(compiler, error_message, &dest)) {
                return false;
            }
            return emit_op_load_constant(compiler, dest, value_make_number(expression->as.number_literal.value), error_message);
        }
        case EXPR_LITERAL_STRING:
            return compile_literal_string(compiler, expression->as.string_literal.value, error_message);
        case EXPR_LITERAL_BOOL: {
            int dest = 0;
            if (!push_stack_slot(compiler, error_message, &dest)) {
                return false;
            }
            emit_op_load_bool(compiler, dest, expression->as.bool_literal.value);
            return true;
        }
        case EXPR_LITERAL_NULL: {
            int dest = 0;
            if (!push_stack_slot(compiler, error_message, &dest)) {
                return false;
            }
            emit_op_load_null(compiler, dest);
            return true;
        }
        case EXPR_IDENTIFIER: {
            const char *name = expression->as.identifier.name;
            int local = resolve_local(compiler, name, false, error_message);
            int dest = 0;
            if (!push_stack_slot(compiler, error_message, &dest)) {
                return false;
            }
            if (local >= 0) {
                emit_op_move(compiler, dest, compiler->locals[local].reg);
                return true;
            }
            int global = global_table_find(compiler->globals, name);
            if (global < 0) {
                pop_stack_slots(compiler, 1);
                compiler_errorf(error_message, "Undefined variable '%s'.", name);
                return false;
            }
            emit_op_get_global(compiler, dest, (uint16_t)global);
            return true;
        }
        case EXPR_UNARY: {
            if (!compile_expression(compiler, expression->as.unary.right, error_message)) {
                return false;
            }
            int reg = stack_top_register(compiler, 0);
            switch (expression->as.unary.operator_type) {
                case TOKEN_MINUS:
                    emit_op_unary(compiler, OP_NEGATE, reg, reg);
                    return true;
                case TOKEN_BANG:
                    emit_op_unary(compiler, OP_NOT, reg, reg);
                    return true;
                default:
                    compiler_errorf(error_message, "Unsupported unary operator.");
                    return false;
            }
        }
        case EXPR_BINARY: {
            if (!compile_expression(compiler, expression->as.binary.left, error_message)) {
                return false;
            }
            if (!compile_expression(compiler, expression->as.binary.right, error_message)) {
                return false;
            }
            int right_reg = stack_top_register(compiler, 0);
            int left_reg = stack_top_register(compiler, 1);
            int dest_reg = left_reg;
            switch (expression->as.binary.operator_type) {
                case TOKEN_PLUS:
                    emit_op_binary(compiler, OP_ADD, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_MINUS:
                    emit_op_binary(compiler, OP_SUBTRACT, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_STAR:
                    emit_op_binary(compiler, OP_MULTIPLY, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_SLASH:
                    emit_op_binary(compiler, OP_DIVIDE, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_GREATER:
                    emit_op_binary(compiler, OP_GREATER, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_GREATER_EQUAL:
                    emit_op_binary(compiler, OP_LESS, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    emit_op_unary(compiler, OP_NOT, dest_reg, dest_reg);
                    return true;
                case TOKEN_LESS:
                    emit_op_binary(compiler, OP_LESS, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_LESS_EQUAL:
                    emit_op_binary(compiler, OP_GREATER, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    emit_op_unary(compiler, OP_NOT, dest_reg, dest_reg);
                    return true;
                case TOKEN_EQUAL_EQUAL:
                    emit_op_binary(compiler, OP_EQUAL, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    return true;
                case TOKEN_BANG_EQUAL:
                    emit_op_binary(compiler, OP_EQUAL, dest_reg, left_reg, right_reg);
                    pop_stack_slots(compiler, 1);
                    emit_op_unary(compiler, OP_NOT, dest_reg, dest_reg);
                    return true;
                default:
                    compiler_errorf(error_message, "Unsupported binary operator.");
                    return false;
            }
        }
        case EXPR_ASSIGNMENT:
            return compile_assignment(compiler, expression, error_message);
        case EXPR_CALL:
            return compile_call(compiler, expression, error_message);
        case EXPR_ARRAY:
            return compile_array_literal(compiler, expression, error_message);
        case EXPR_INDEX:
            return compile_index_expression(compiler, expression, error_message);
        case EXPR_THIS: {
            int local = resolve_local(compiler, "this", false, error_message);
            if (local < 0) {
                compiler_errorf(error_message, "Cannot use 'this' outside of class method.");
                return false;
            }
            int dest = 0;
            if (!push_stack_slot(compiler, error_message, &dest)) {
                return false;
            }
            emit_op_move(compiler, dest, compiler->locals[local].reg);
            return true;
        }
        case EXPR_GET_PROPERTY: {
            if (!compile_expression(compiler, expression->as.get_property.object, error_message)) {
                return false;
            }
            uint16_t name_index = 0;
            if (!make_string_constant(compiler, expression->as.get_property.name, &name_index, error_message)) {
                return false;
            }
            int object_reg = stack_top_register(compiler, 0);
            emit_op_get_property(compiler, object_reg, object_reg, name_index);
            return true;
        }
        case EXPR_SET_PROPERTY: {
            if (!compile_expression(compiler, expression->as.set_property.object, error_message)) {
                return false;
            }
            if (!compile_expression(compiler, expression->as.set_property.value, error_message)) {
                return false;
            }
            int value_reg = stack_top_register(compiler, 0);
            int object_reg = stack_top_register(compiler, 1);
            uint16_t name_index = 0;
            if (!make_string_constant(compiler, expression->as.set_property.name, &name_index, error_message)) {
                return false;
            }
            emit_op_set_property(compiler, object_reg, name_index, value_reg);
            emit_op_move(compiler, object_reg, value_reg);
            pop_stack_slots(compiler, 1);
            return true;
        }
        case EXPR_INVOKE: {
            if (!compile_expression(compiler, expression->as.invoke.object, error_message)) {
                return false;
            }
            size_t arg_count = expression->as.invoke.arguments.count;
            if (arg_count >= UINT8_MAX) {
                compiler_errorf(error_message, "Too many arguments in method call.");
                return false;
            }
            for (size_t i = 0; i < arg_count; ++i) {
                if (!compile_expression(compiler, expression->as.invoke.arguments.items[i], error_message)) {
                    return false;
                }
            }
            uint16_t name_index = 0;
            if (!make_string_constant(compiler, expression->as.invoke.name, &name_index, error_message)) {
                return false;
            }
            int object_reg = stack_top_register(compiler, (int)arg_count);
            uint8_t arg_registers[UINT8_MAX];
            for (size_t i = 0; i < arg_count; ++i) {
                int distance = (int)(arg_count - 1 - i);
                arg_registers[i] = (uint8_t)stack_top_register(compiler, distance);
            }
            emit_op_invoke(compiler, object_reg, object_reg, name_index, (uint8_t)arg_count, arg_registers);
            pop_stack_slots(compiler, (int)arg_count);
            return true;
        }
    }
    compiler_errorf(error_message, "Unknown expression type.");
    return false;
}

static bool compile_assignment(Compiler *compiler, const Expression *expression, char **error_message) {
    const char *name = expression->as.assignment.name;
    if (!compile_expression(compiler, expression->as.assignment.value, error_message)) {
        return false;
    }
    int local = resolve_local(compiler, name, true, error_message);
    if (local >= 0) {
        int value_reg = stack_top_register(compiler, 0);
        emit_op_move(compiler, compiler->locals[local].reg, value_reg);
        return true;
    }
    int global = global_table_find(compiler->globals, name);
    if (global < 0) {
        compiler_errorf(error_message, "Undefined variable '%s'.", name);
        return false;
    }
    int value_reg = stack_top_register(compiler, 0);
    emit_op_set_global(compiler, value_reg, (uint16_t)global);
    return true;
}

static bool compile_call(Compiler *compiler, const Expression *expression, char **error_message) {
    if (!compile_expression(compiler, expression->as.call.callee, error_message)) {
        return false;
    }
    if (expression->as.call.arguments.count > UINT8_MAX) {
        compiler_errorf(error_message, "Too many arguments in function call.");
        return false;
    }
    size_t arg_count = expression->as.call.arguments.count;
    for (size_t i = 0; i < arg_count; ++i) {
        if (!compile_expression(compiler, expression->as.call.arguments.items[i], error_message)) {
            return false;
        }
    }
    int callee_reg = stack_top_register(compiler, (int)arg_count);
    uint8_t arg_registers[UINT8_MAX];
    for (size_t i = 0; i < arg_count; ++i) {
        int distance = (int)(arg_count - 1 - i);
        arg_registers[i] = (uint8_t)stack_top_register(compiler, distance);
    }
    emit_op_call(compiler, callee_reg, callee_reg, (uint8_t)arg_count, arg_registers);
    pop_stack_slots(compiler, (int)arg_count);
    return true;
}

static bool compile_array_literal(Compiler *compiler, const Expression *expression, char **error_message) {
    size_t element_count = expression->as.array_literal.elements.count;
    if (element_count > UINT8_MAX) {
        compiler_errorf(error_message, "Array literal has too many elements.");
        return false;
    }

    if (element_count == 0) {
        int dest = 0;
        if (!push_stack_slot(compiler, error_message, &dest)) {
            return false;
        }
        emit_op_build_array(compiler, dest, 0, NULL);
        return true;
    }

    uint8_t element_registers[UINT8_MAX];
    for (size_t i = 0; i < element_count; ++i) {
        Expression *element = expression->as.array_literal.elements.items[i];
        if (!compile_expression(compiler, element, error_message)) {
            return false;
        }
        element_registers[i] = (uint8_t)stack_top_register(compiler, 0);
    }

    int dest = element_registers[0];
    emit_op_build_array(compiler, dest, (uint8_t)element_count, element_registers);
    pop_stack_slots(compiler, (int)element_count - 1);
    return true;
}

static bool compile_index_expression(Compiler *compiler, const Expression *expression, char **error_message) {
    if (!compile_expression(compiler, expression->as.index.array, error_message)) {
        return false;
    }
    if (!compile_expression(compiler, expression->as.index.index, error_message)) {
        return false;
    }
    int index_reg = stack_top_register(compiler, 0);
    int array_reg = stack_top_register(compiler, 1);
    emit_op_array_get(compiler, array_reg, array_reg, index_reg);
    pop_stack_slots(compiler, 1);
    return true;
}

static bool compile_block(Compiler *compiler, const StatementList *statements, char **error_message) {
    if (!statements) {
        return true;
    }
    for (size_t i = 0; i < statements->count; ++i) {
        if (!compile_statement(compiler, statements->items[i], error_message)) {
            return false;
        }
    }
    return true;
}

static bool compile_let_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    const char *name = statement->as.let_statement.name;
    bool has_initializer = statement->as.let_statement.has_initializer;
    if (compiler->scope_depth > 0) {
        for (int i = compiler->local_count - 1; i >= 0; --i) {
            Local *local = &compiler->locals[i];
            if (local->depth != -1 && local->depth < compiler->scope_depth) {
                break;
            }
            if (local->name && strcmp(local->name, name) == 0) {
                compiler_errorf(error_message, "Variable '%s' already declared in this scope.", name);
                return false;
            }
        }
        int slot = add_local(compiler, name, error_message);
        if (slot < 0) {
            return false;
        }
        Local *local = &compiler->locals[slot];
        if (has_initializer) {
            if (!compile_expression(compiler, statement->as.let_statement.initializer, error_message)) {
                return false;
            }
            int value_reg = stack_top_register(compiler, 0);
            emit_op_move(compiler, local->reg, value_reg);
            pop_stack_slots(compiler, 1);
        } else {
            emit_op_load_null(compiler, local->reg);
        }
        local->depth = compiler->scope_depth;
        local->is_initialized = true;
        return true;
    }

    uint16_t index = 0;
    if (!global_table_add(compiler->globals, name, &index, error_message)) {
        return false;
    }
    if (has_initializer) {
        if (!compile_expression(compiler, statement->as.let_statement.initializer, error_message)) {
            return false;
        }
        int value_reg = stack_top_register(compiler, 0);
        emit_op_define_global(compiler, value_reg, index);
        pop_stack_slots(compiler, 1);
    } else {
        int dest = 0;
        if (!push_stack_slot(compiler, error_message, &dest)) {
            return false;
        }
        emit_op_load_null(compiler, dest);
        emit_op_define_global(compiler, dest, index);
        pop_stack_slots(compiler, 1);
    }
    return true;
}

static bool compile_expression_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    if (compiler->scope_depth == 0 && !compiler->enclosing) {
        if (compiler->has_pending_expression) {
            pop_stack_slots(compiler, 1);
            compiler->has_pending_expression = false;
            compiler->pending_has_value = false;
        }
        if (!compile_expression(compiler, statement->as.expression_statement.expression, error_message)) {
            return false;
        }
        compiler->has_pending_expression = true;
        compiler->pending_has_value = true;
        compiler->pending_value = register_result_make(stack_top_register(compiler, 0), true);
        return true;
    }
    if (!compile_expression(compiler, statement->as.expression_statement.expression, error_message)) {
        return false;
    }
    pop_stack_slots(compiler, 1);
    return true;
}

static bool compile_if_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    if (!compile_expression(compiler, statement->as.if_statement.condition, error_message)) {
        return false;
    }
    int condition_reg = stack_top_register(compiler, 0);
    int then_jump = emit_jump_if_false(compiler, condition_reg);
    pop_stack_slots(compiler, 1);

    if (!compile_statement(compiler, statement->as.if_statement.then_branch, error_message)) {
        return false;
    }

    int else_jump = emit_jump_unconditional(compiler);
    if (!patch_jump(compiler, then_jump, error_message)) {
        return false;
    }

    if (statement->as.if_statement.else_branch) {
        if (!compile_statement(compiler, statement->as.if_statement.else_branch, error_message)) {
            return false;
        }
    }

    if (!patch_jump(compiler, else_jump, error_message)) {
        return false;
    }
    return true;
}

static bool compile_while_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    int loop_start = current_chunk(compiler)->count;
    if (!compile_expression(compiler, statement->as.while_statement.condition, error_message)) {
        return false;
    }
    int condition_reg = stack_top_register(compiler, 0);
    int exit_jump = emit_jump_if_false(compiler, condition_reg);
    pop_stack_slots(compiler, 1);

    if (!compile_statement(compiler, statement->as.while_statement.body, error_message)) {
        return false;
    }
    if (!emit_loop_instruction(compiler, loop_start, error_message)) {
        return false;
    }
    if (!patch_jump(compiler, exit_jump, error_message)) {
        return false;
    }
    return true;
}

static bool compile_function_body(Compiler *compiler, const Statement *body, char **error_message) {
    if (!body) {
        return emit_return(compiler, error_message);
    }
    if (body->type != STMT_BLOCK) {
        compiler_errorf(error_message, "Function body must be a block.");
        return false;
    }
    if (!begin_scope(compiler)) {
        return false;
    }
    if (!compile_block(compiler, &body->as.block_statement.statements, error_message)) {
        return false;
    }
    end_scope(compiler);
    return emit_return(compiler, error_message);
}

static bool compile_method(Compiler *compiler, const ClassMethod *method, int class_reg, char **error_message) {
    size_t required_arity = method->parameter_count + 1;
    if (required_arity > UINT8_MAX) {
        compiler_errorf(error_message, "Method '%s' has too many parameters.", method->name ? method->name : "<anonymous>");
        return false;
    }

    ObjFunction *function = obj_function_new(compiler->vm, method->name, (int)required_arity);
    vm_push(compiler->vm, value_make_function(function));

    FunctionType type = method->is_constructor ? FUNCTION_TYPE_INITIALIZER : FUNCTION_TYPE_METHOD;
    Compiler child;
    compiler_init(&child, compiler->compilation, compiler, function, compiler->program, type);

    int this_slot = add_local(&child, "this", error_message);
    if (this_slot < 0) {
        vm_pop(compiler->vm);
        return false;
    }
    child.locals[this_slot].depth = 0;
    child.locals[this_slot].is_initialized = true;

    for (size_t i = 0; i < method->parameter_count; ++i) {
        const char *param = method->parameters[i];
        int slot = add_local(&child, param, error_message);
        if (slot < 0) {
            vm_pop(compiler->vm);
            return false;
        }
        child.locals[slot].depth = 0;
        child.locals[slot].is_initialized = true;
    }

    if (!compile_function_body(&child, method->body, error_message)) {
        vm_pop(compiler->vm);
        return false;
    }

    int dest = 0;
    if (!push_stack_slot(compiler, error_message, &dest)) {
        vm_pop(compiler->vm);
        return false;
    }
    if (!emit_op_load_constant(compiler, dest, value_make_function(function), error_message)) {
        vm_pop(compiler->vm);
        pop_stack_slots(compiler, 1);
        return false;
    }
    vm_pop(compiler->vm);

    uint16_t name_index = 0;
    if (!make_string_constant(compiler, method->name, &name_index, error_message)) {
        pop_stack_slots(compiler, 1);
        return false;
    }
    emit_op_method(compiler, class_reg, name_index, dest);
    pop_stack_slots(compiler, 1);
    return true;
}

static bool compile_function_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    const char *name = statement->as.function_statement.name;
    size_t arity = statement->as.function_statement.parameter_count;
    if (arity > UINT8_MAX) {
        compiler_errorf(error_message, "Function '%s' has too many parameters.", name ? name : "<anonymous>");
        return false;
    }

    uint16_t global_index = 0;
    int local_slot = -1;
    bool is_global = (compiler->enclosing == NULL && compiler->scope_depth == 0);
    if (is_global) {
        if (!global_table_add(compiler->globals, name, &global_index, error_message)) {
            return false;
        }
    } else {
        local_slot = add_local(compiler, name, error_message);
        if (local_slot < 0) {
            return false;
        }
        compiler->locals[local_slot].depth = compiler->scope_depth;
        compiler->locals[local_slot].is_initialized = true;
    }

    ObjFunction *function = obj_function_new(compiler->vm, name, (int)arity);
    vm_push(compiler->vm, value_make_function(function));

    Compiler child;
    compiler_init(&child, compiler->compilation, compiler, function, compiler->program, FUNCTION_TYPE_FUNCTION);

    for (size_t i = 0; i < arity; ++i) {
        const char *param = statement->as.function_statement.parameters[i];
        int slot = add_local(&child, param, error_message);
        if (slot < 0) {
            vm_pop(compiler->vm);
            return false;
        }
        child.locals[slot].depth = 0;
        child.locals[slot].is_initialized = true;
    }

    bool ok = compile_function_body(&child, statement->as.function_statement.body, error_message);
    if (!ok) {
        vm_pop(compiler->vm);
        return false;
    }

    int dest = 0;
    if (!push_stack_slot(compiler, error_message, &dest)) {
        vm_pop(compiler->vm);
        return false;
    }
    if (!emit_op_load_constant(compiler, dest, value_make_function(function), error_message)) {
        vm_pop(compiler->vm);
        return false;
    }
    vm_pop(compiler->vm);

    if (is_global) {
        emit_op_define_global(compiler, dest, global_index);
    } else {
        emit_op_move(compiler, compiler->locals[local_slot].reg, dest);
    }
    pop_stack_slots(compiler, 1);
    return true;
}

static bool compile_return_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    discard_pending_expression(compiler);
    if (compiler->type == FUNCTION_TYPE_INITIALIZER && statement->as.return_statement.has_value) {
        compiler_errorf(error_message, "Cannot return a value from constructor.");
        return false;
    }
    if (statement->as.return_statement.has_value) {
        if (!compile_expression(compiler, statement->as.return_statement.value, error_message)) {
            return false;
        }
        int value_reg = stack_top_register(compiler, 0);
        emit_return_value(compiler, value_reg);
        pop_stack_slots(compiler, 1);
    } else {
        if (!emit_return(compiler, error_message)) {
            return false;
        }
    }
    compiler->has_pending_expression = false;
    compiler->pending_has_value = false;
    return true;
}

static void discard_pending_expression(Compiler *compiler) {
    if (!compiler->enclosing && compiler->scope_depth == 0 && compiler->has_pending_expression) {
        pop_stack_slots(compiler, 1);
        compiler->has_pending_expression = false;
        compiler->pending_has_value = false;
    }
}

static bool compile_class_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    const char *name = statement->as.class_statement.name;
    uint16_t name_constant = 0;
    if (!make_string_constant(compiler, name, &name_constant, error_message)) {
        return false;
    }

    bool is_global = (compiler->enclosing == NULL && compiler->scope_depth == 0);
    uint16_t global_index = 0;
    int local_slot = -1;
    if (is_global) {
        if (!global_table_add(compiler->globals, name, &global_index, error_message)) {
            return false;
        }
    } else {
        local_slot = add_local(compiler, name, error_message);
        if (local_slot < 0) {
            return false;
        }
    }

    int class_reg = 0;
    if (!push_stack_slot(compiler, error_message, &class_reg)) {
        return false;
    }
    emit_op_class(compiler, class_reg, name_constant);

    if (is_global) {
        emit_op_define_global(compiler, class_reg, global_index);
    } else {
        Local *local = &compiler->locals[local_slot];
        local->depth = compiler->scope_depth;
        local->is_initialized = true;
        emit_op_move(compiler, local->reg, class_reg);
    }

    for (size_t i = 0; i < statement->as.class_statement.method_count; ++i) {
        if (!compile_method(compiler, &statement->as.class_statement.methods[i], class_reg, error_message)) {
            return false;
        }
    }

    pop_stack_slots(compiler, 1);
    return true;
}

static bool compile_statement(Compiler *compiler, const Statement *statement, char **error_message) {
    if (!statement) {
        return true;
    }
    if (statement->type != STMT_EXPRESSION) {
        discard_pending_expression(compiler);
    }
    switch (statement->type) {
        case STMT_LET:
            return compile_let_statement(compiler, statement, error_message);
        case STMT_EXPRESSION:
            return compile_expression_statement(compiler, statement, error_message);
        case STMT_IF:
            return compile_if_statement(compiler, statement, error_message);
        case STMT_WHILE:
            return compile_while_statement(compiler, statement, error_message);
        case STMT_BLOCK:
            if (!begin_scope(compiler)) {
                return false;
            }
            if (!compile_block(compiler, &statement->as.block_statement.statements, error_message)) {
                return false;
            }
            end_scope(compiler);
            return true;
        case STMT_FUNCTION:
            return compile_function_statement(compiler, statement, error_message);
        case STMT_RETURN:
            return compile_return_statement(compiler, statement, error_message);
        case STMT_CLASS:
            return compile_class_statement(compiler, statement, error_message);
    }
    compiler_errorf(error_message, "Unknown statement type.");
    return false;
}

ObjFunction *compiler_compile(VM *vm, const Program *program, char **error_message) {
    if (!vm || !program) {
        compiler_errorf(error_message, "Invalid arguments to compiler.");
        return NULL;
    }

    Compilation compilation;
    compilation.vm = vm;
    global_table_init(&compilation.globals);

    ObjFunction *function = obj_function_new(vm, "script", 0);
    vm_push(vm, value_make_function(function));

    Compiler compiler;
    compiler_init(&compiler, &compilation, NULL, function, program, FUNCTION_TYPE_SCRIPT);

    for (size_t i = 0; i < program->statements.count; ++i) {
        if (!compile_statement(&compiler, program->statements.items[i], error_message)) {
            vm_pop(vm);
            global_table_free(&compilation.globals);
            return NULL;
        }
    }

    if (!emit_return(&compiler, error_message)) {
        vm_pop(vm);
        global_table_free(&compilation.globals);
        return NULL;
    }

    vm_pop(vm);
    global_table_free(&compilation.globals);
    return function;
}

bool compiler_run_program(VM *vm, const Program *program, Value *result_out, char **error_message) {
    ObjFunction *function = compiler_compile(vm, program, error_message);
    if (!function) {
        return false;
    }
    Value result = value_make_null();
    InterpretResult status = vm_interpret(vm, function, &result);
    if (status != INTERPRET_OK) {
        compiler_errorf(error_message, "Runtime error during execution.");
        return false;
    }
    if (result_out) {
        *result_out = result;
    }
    return true;
}

bool compiler_run_source(VM *vm, const char *source, Value *result_out, char **error_message) {
    if (!vm || !source) {
        compiler_errorf(error_message, "Invalid arguments to run_source.");
        return false;
    }
    char *parse_error = NULL;
    Program *program = parser_parse(source, &parse_error);
    if (!program) {
        if (parse_error) {
            if (error_message && !*error_message) {
                *error_message = parse_error;
            } else {
                free(parse_error);
            }
        } else {
            compiler_errorf(error_message, "Parsing failed.");
        }
        return false;
    }

    bool ok = compiler_run_program(vm, program, result_out, error_message);
    program_free(program);
    return ok;
}

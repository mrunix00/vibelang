#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "table.h"
#include "vm.h"

static uint32_t hash_bytes(const char *chars, size_t length) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;
    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < length; ++i) {
        hash ^= (uint32_t)(unsigned char)chars[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static Obj *allocate_object(VM *vm, size_t size, ObjType type) {
    Obj *object = (Obj *)malloc(size);
    if (!object) {
        fprintf(stderr, "Failed to allocate object.\n");
        exit(EXIT_FAILURE);
    }
    object->type = type;
    object->marked = false;
    object->next = vm->objects;
    vm->objects = object;
    vm->bytes_allocated += size;
    return object;
}

static ObjString *allocate_string(VM *vm, char *chars, size_t length, uint32_t hash) {
    ObjString *string = (ObjString *)allocate_object(vm, sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->chars[length] = '\0';
    string->hash = hash;
    vm->bytes_allocated += length + 1;
    return string;
}

static bool array_resize(VM *vm, ValueArray *array, size_t new_capacity) {
    size_t old_bytes = array->capacity * sizeof(Value);
    Value *values = NULL;
    if (new_capacity > 0) {
        values = (Value *)realloc(array->values, new_capacity * sizeof(Value));
        if (!values) {
            return false;
        }
    } else {
        free(array->values);
    }
    array->values = values;
    array->capacity = new_capacity;
    vm->bytes_allocated += (new_capacity * sizeof(Value)) - old_bytes;
    return true;
}

static bool ensure_property_capacity(VM *vm, ObjProperty **entries, size_t *capacity, size_t required) {
    size_t current = *capacity;
    if (current >= required) {
        return true;
    }
    size_t new_capacity = current == 0 ? 4 : current;
    while (new_capacity < required) {
        new_capacity *= 2;
        if (new_capacity < current) {
            return false;
        }
    }
    ObjProperty *storage = (ObjProperty *)realloc(*entries, new_capacity * sizeof(ObjProperty));
    if (!storage) {
        return false;
    }
    for (size_t i = current; i < new_capacity; ++i) {
        storage[i].name = NULL;
        storage[i].value = value_make_null();
    }
    vm->bytes_allocated += (new_capacity - current) * sizeof(ObjProperty);
    *entries = storage;
    *capacity = new_capacity;
    return true;
}

static void array_ensure_capacity_or_die(VM *vm, ValueArray *array, size_t min_capacity) {
    if (array->capacity >= min_capacity) {
        return;
    }
    size_t new_capacity = array->capacity == 0 ? 8 : array->capacity;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
        if (new_capacity < array->capacity) {
            fprintf(stderr, "Array capacity overflow.\n");
            exit(EXIT_FAILURE);
        }
    }
    if (!array_resize(vm, array, new_capacity)) {
        fprintf(stderr, "Failed to grow array storage.\n");
        exit(EXIT_FAILURE);
    }
}

ObjArray *obj_array_new(VM *vm) {
    if (!vm) {
        return NULL;
    }
    ObjArray *array = (ObjArray *)allocate_object(vm, sizeof(ObjArray), OBJ_ARRAY);
    array->elements.values = NULL;
    array->elements.count = 0;
    array->elements.capacity = 0;
    return array;
}

ObjArray *obj_array_copy(VM *vm, const Value *values, size_t count) {
    ObjArray *array = obj_array_new(vm);
    if (!array) {
        return NULL;
    }
    if (count > 0) {
        array_ensure_capacity_or_die(vm, &array->elements, count);
        memcpy(array->elements.values, values, count * sizeof(Value));
        array->elements.count = count;
    }
    return array;
}

bool obj_array_append(VM *vm, ObjArray *array, Value value) {
    if (!vm || !array) {
        return false;
    }
    array_ensure_capacity_or_die(vm, &array->elements, array->elements.count + 1);
    array->elements.values[array->elements.count++] = value;
    return true;
}

bool obj_array_extend(VM *vm, ObjArray *array, const Value *values, size_t count) {
    if (!vm || !array || !values || count == 0) {
        if (count == 0) {
            return true;
        }
        return false;
    }
    size_t new_count = array->elements.count + count;
    array_ensure_capacity_or_die(vm, &array->elements, new_count);
    memcpy(array->elements.values + array->elements.count, values, count * sizeof(Value));
    array->elements.count = new_count;
    return true;
}

ObjClass *obj_class_new(VM *vm, ObjString *name) {
    if (!vm) {
        return NULL;
    }
    ObjClass *klass = (ObjClass *)allocate_object(vm, sizeof(ObjClass), OBJ_CLASS);
    klass->name = name;
    klass->methods = NULL;
    klass->method_count = 0;
    klass->method_capacity = 0;
    return klass;
}

bool obj_class_define_method(VM *vm, ObjClass *klass, ObjString *name, Value method) {
    if (!vm || !klass || !name) {
        return false;
    }
    for (size_t i = 0; i < klass->method_count; ++i) {
        if (klass->methods[i].name == name) {
            klass->methods[i].value = method;
            return true;
        }
    }
    if (!ensure_property_capacity(vm, &klass->methods, &klass->method_capacity, klass->method_count + 1)) {
        return false;
    }
    klass->methods[klass->method_count].name = name;
    klass->methods[klass->method_count].value = method;
    klass->method_count++;
    return true;
}

bool obj_class_find_method(ObjClass *klass, ObjString *name, Value *out) {
    if (!klass || !name) {
        return false;
    }
    for (size_t i = 0; i < klass->method_count; ++i) {
        if (klass->methods[i].name == name) {
            if (out) {
                *out = klass->methods[i].value;
            }
            return true;
        }
    }
    return false;
}

ObjInstance *obj_instance_new(VM *vm, ObjClass *klass) {
    if (!vm || !klass) {
        return NULL;
    }
    ObjInstance *instance = (ObjInstance *)allocate_object(vm, sizeof(ObjInstance), OBJ_INSTANCE);
    instance->klass = klass;
    instance->fields = NULL;
    instance->field_count = 0;
    instance->field_capacity = 0;
    return instance;
}

bool obj_instance_get_field(ObjInstance *instance, ObjString *name, Value *out) {
    if (!instance || !name) {
        return false;
    }
    for (size_t i = 0; i < instance->field_count; ++i) {
        if (instance->fields[i].name == name) {
            if (out) {
                *out = instance->fields[i].value;
            }
            return true;
        }
    }
    return false;
}

bool obj_instance_set_field(VM *vm, ObjInstance *instance, ObjString *name, Value value) {
    if (!vm || !instance || !name) {
        return false;
    }
    for (size_t i = 0; i < instance->field_count; ++i) {
        if (instance->fields[i].name == name) {
            instance->fields[i].value = value;
            return true;
        }
    }
    if (!ensure_property_capacity(vm, &instance->fields, &instance->field_capacity, instance->field_count + 1)) {
        return false;
    }
    instance->fields[instance->field_count].name = name;
    instance->fields[instance->field_count].value = value;
    instance->field_count++;
    return true;
}

ObjBoundMethod *obj_bound_method_new(VM *vm, Value receiver, ObjFunction *method) {
    if (!vm || !method) {
        return NULL;
    }
    ObjBoundMethod *bound = (ObjBoundMethod *)allocate_object(vm, sizeof(ObjBoundMethod), OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjString *obj_string_take(VM *vm, char *chars, size_t length) {
    if (!vm || !chars) {
        free(chars);
        return NULL;
    }
    uint32_t hash = hash_bytes(chars, length);
    ObjString *interned = table_find_string(&vm->strings, chars, length, hash);
    if (interned) {
        free(chars);
        return interned;
    }

    ObjString *string = allocate_string(vm, chars, length, hash);
    vm_push(vm, value_make_string(string));
    table_define(&vm->strings, string);
    vm_pop(vm);
    return string;
}

ObjString *obj_string_copy(VM *vm, const char *chars, size_t length) {
    if (!vm || (!chars && length > 0)) {
        return NULL;
    }
    if (!chars) {
        chars = "";
    }
    char *heap_chars = (char *)malloc(length + 1);
    if (!heap_chars) {
        fprintf(stderr, "Failed to allocate string characters.\n");
        exit(EXIT_FAILURE);
    }
    if (length > 0) {
        memcpy(heap_chars, chars, length);
    }
    heap_chars[length] = '\0';
    return obj_string_take(vm, heap_chars, length);
}

ObjFunction *obj_function_new(VM *vm, const char *name, int arity) {
    if (!vm) {
        return NULL;
    }
    ObjFunction *function = (ObjFunction *)allocate_object(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = arity;
    function->register_count = 0;
    function->name = NULL;
    chunk_init(&function->chunk);
    if (name) {
        size_t length = strlen(name);
        function->name = obj_string_copy(vm, name, length);
    }
    return function;
}

void obj_free(VM *vm, Obj *object) {
    if (!vm || !object) {
        return;
    }
    switch (object->type) {
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *)object;
            chunk_free(&function->chunk);
            vm->bytes_allocated -= sizeof(ObjFunction);
            free(function);
            break;
        }
        case OBJ_STRING: {
            ObjString *string = (ObjString *)object;
            vm->bytes_allocated -= sizeof(ObjString);
            vm->bytes_allocated -= string->length + 1;
            free(string->chars);
            free(string);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray *array = (ObjArray *)object;
            vm->bytes_allocated -= sizeof(ObjArray);
            vm->bytes_allocated -= array->elements.capacity * sizeof(Value);
            free(array->elements.values);
            free(array);
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *)object;
            vm->bytes_allocated -= sizeof(ObjClass);
            vm->bytes_allocated -= klass->method_capacity * sizeof(ObjProperty);
            free(klass->methods);
            free(klass);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *)object;
            vm->bytes_allocated -= sizeof(ObjInstance);
            vm->bytes_allocated -= instance->field_capacity * sizeof(ObjProperty);
            free(instance->fields);
            free(instance);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bound = (ObjBoundMethod *)object;
            vm->bytes_allocated -= sizeof(ObjBoundMethod);
            free(bound);
            break;
        }
        default:
            free(object);
            break;
    }
}


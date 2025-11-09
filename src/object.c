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
        default:
            free(object);
            break;
    }
}


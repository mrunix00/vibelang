#ifndef VIBELANG_OBJECT_H
#define VIBELANG_OBJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "chunk.h"

typedef struct VM VM;

typedef enum {
    OBJ_FUNCTION,
    OBJ_STRING,
    OBJ_ARRAY
} ObjType;

typedef struct Obj {
    ObjType type;
    bool marked;
    struct Obj *next;
} Obj;

typedef struct ObjString {
    Obj obj;
    size_t length;
    char *chars;
    uint32_t hash;
} ObjString;

typedef struct ObjFunction {
    Obj obj;
    int arity;
    int register_count;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef struct ObjArray {
    Obj obj;
    ValueArray elements;
} ObjArray;

static inline Value value_make_function(ObjFunction *function) {
    return value_make_obj((Obj *)function);
}

static inline Value value_make_string(ObjString *string) {
    return value_make_obj((Obj *)string);
}

static inline Value value_make_array(ObjArray *array) {
    return value_make_obj((Obj *)array);
}

static inline bool value_is_function(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_FUNCTION;
}

static inline bool value_is_string(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_STRING;
}

static inline bool value_is_array(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_ARRAY;
}

static inline ObjFunction *value_as_function(Value value) {
    return (ObjFunction *)value_as_obj(value);
}

static inline ObjString *value_as_string(Value value) {
    return (ObjString *)value_as_obj(value);
}

static inline ObjArray *value_as_array(Value value) {
    return (ObjArray *)value_as_obj(value);
}

ObjFunction *obj_function_new(VM *vm, const char *name, int arity);
ObjString *obj_string_copy(VM *vm, const char *chars, size_t length);
ObjString *obj_string_take(VM *vm, char *chars, size_t length);
ObjArray *obj_array_new(VM *vm);
ObjArray *obj_array_copy(VM *vm, const Value *values, size_t count);
bool obj_array_append(VM *vm, ObjArray *array, Value value);
bool obj_array_extend(VM *vm, ObjArray *array, const Value *values, size_t count);
void obj_free(VM *vm, Obj *object);

#endif

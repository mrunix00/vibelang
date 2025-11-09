#ifndef VIBELANG_OBJECT_H
#define VIBELANG_OBJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "chunk.h"

typedef struct VM VM;

typedef enum {
    OBJ_FUNCTION,
    OBJ_STRING
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

static inline Value value_make_function(ObjFunction *function) {
    return value_make_obj((Obj *)function);
}

static inline Value value_make_string(ObjString *string) {
    return value_make_obj((Obj *)string);
}

static inline bool value_is_function(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_FUNCTION;
}

static inline bool value_is_string(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_STRING;
}

static inline ObjFunction *value_as_function(Value value) {
    return (ObjFunction *)value_as_obj(value);
}

static inline ObjString *value_as_string(Value value) {
    return (ObjString *)value_as_obj(value);
}

ObjFunction *obj_function_new(VM *vm, const char *name, int arity);
ObjString *obj_string_copy(VM *vm, const char *chars, size_t length);
ObjString *obj_string_take(VM *vm, char *chars, size_t length);
void obj_free(VM *vm, Obj *object);

#endif

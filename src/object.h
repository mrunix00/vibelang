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
    OBJ_ARRAY,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD
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

typedef struct ObjProperty {
    ObjString *name;
    Value value;
} ObjProperty;

typedef struct ObjClass {
    Obj obj;
    ObjString *name;
    ObjProperty *methods;
    size_t method_count;
    size_t method_capacity;
} ObjClass;

typedef struct ObjInstance {
    Obj obj;
    ObjClass *klass;
    ObjProperty *fields;
    size_t field_count;
    size_t field_capacity;
} ObjInstance;

typedef struct ObjBoundMethod {
    Obj obj;
    Value receiver;
    ObjFunction *method;
} ObjBoundMethod;

static inline Value value_make_function(ObjFunction *function) {
    return value_make_obj((Obj *)function);
}

static inline Value value_make_string(ObjString *string) {
    return value_make_obj((Obj *)string);
}

static inline Value value_make_array(ObjArray *array) {
    return value_make_obj((Obj *)array);
}

static inline Value value_make_class(ObjClass *klass) {
    return value_make_obj((Obj *)klass);
}

static inline Value value_make_instance(ObjInstance *instance) {
    return value_make_obj((Obj *)instance);
}

static inline Value value_make_bound_method(ObjBoundMethod *bound) {
    return value_make_obj((Obj *)bound);
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

static inline bool value_is_class(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_CLASS;
}

static inline bool value_is_instance(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_INSTANCE;
}

static inline bool value_is_bound_method(Value value) {
    return value_is_obj(value) && value_as_obj(value)->type == OBJ_BOUND_METHOD;
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

static inline ObjClass *value_as_class(Value value) {
    return (ObjClass *)value_as_obj(value);
}

static inline ObjInstance *value_as_instance(Value value) {
    return (ObjInstance *)value_as_obj(value);
}

static inline ObjBoundMethod *value_as_bound_method(Value value) {
    return (ObjBoundMethod *)value_as_obj(value);
}

ObjFunction *obj_function_new(VM *vm, const char *name, int arity);
ObjString *obj_string_copy(VM *vm, const char *chars, size_t length);
ObjString *obj_string_take(VM *vm, char *chars, size_t length);
ObjArray *obj_array_new(VM *vm);
ObjArray *obj_array_copy(VM *vm, const Value *values, size_t count);
bool obj_array_append(VM *vm, ObjArray *array, Value value);
bool obj_array_extend(VM *vm, ObjArray *array, const Value *values, size_t count);
ObjClass *obj_class_new(VM *vm, ObjString *name);
bool obj_class_define_method(VM *vm, ObjClass *klass, ObjString *name, Value method);
bool obj_class_find_method(ObjClass *klass, ObjString *name, Value *out);
ObjInstance *obj_instance_new(VM *vm, ObjClass *klass);
bool obj_instance_get_field(ObjInstance *instance, ObjString *name, Value *out);
bool obj_instance_set_field(VM *vm, ObjInstance *instance, ObjString *name, Value value);
ObjBoundMethod *obj_bound_method_new(VM *vm, Value receiver, ObjFunction *method);
void obj_free(VM *vm, Obj *object);

#endif

#ifndef VIBELANG_VALUE_H
#define VIBELANG_VALUE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Obj Obj;
typedef struct ObjFunction ObjFunction;
typedef struct ObjString ObjString;

typedef enum {
    VAL_NULL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

static inline Value value_make_null(void) {
    Value value;
    value.type = VAL_NULL;
    return value;
}

static inline Value value_make_bool(bool boolean) {
    Value value;
    value.type = VAL_BOOL;
    value.as.boolean = boolean;
    return value;
}

static inline Value value_make_number(double number) {
    Value value;
    value.type = VAL_NUMBER;
    value.as.number = number;
    return value;
}

static inline Value value_make_obj(Obj *object) {
    Value value;
    value.type = VAL_OBJ;
    value.as.obj = object;
    return value;
}

static inline bool value_is_null(Value value) {
    return value.type == VAL_NULL;
}

static inline bool value_is_bool(Value value) {
    return value.type == VAL_BOOL;
}

static inline bool value_is_number(Value value) {
    return value.type == VAL_NUMBER;
}

static inline bool value_is_obj(Value value) {
    return value.type == VAL_OBJ;
}

static inline bool value_as_bool(Value value) {
    return value.as.boolean;
}

static inline double value_as_number(Value value) {
    return value.as.number;
}

static inline Obj *value_as_obj(Value value) {
    return value.as.obj;
}

bool value_equals(Value a, Value b);
bool value_is_truthy(Value value);

typedef struct {
    Value *values;
    size_t count;
    size_t capacity;
} ValueArray;

void value_array_init(ValueArray *array);
void value_array_free(ValueArray *array);
bool value_array_write(ValueArray *array, Value value);

#endif

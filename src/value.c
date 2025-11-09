#include "value.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"

static size_t grow_capacity(size_t capacity) {
    return capacity < 8 ? 8 : capacity * 2;
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
        case VAL_NULL:
            return true;
        case VAL_BOOL:
            return a.as.boolean == b.as.boolean;
        case VAL_NUMBER:
            return a.as.number == b.as.number;
        case VAL_OBJ:
            if (value_is_string(a) && value_is_string(b)) {
                ObjString *string_a = value_as_string(a);
                ObjString *string_b = value_as_string(b);
                if (string_a == string_b) {
                    return true;
                }
                if (string_a->length != string_b->length) {
                    return false;
                }
                return memcmp(string_a->chars, string_b->chars, string_a->length) == 0;
            }
            return value_as_obj(a) == value_as_obj(b);
    }
    return false;
}

bool value_is_truthy(Value value) {
    if (value.type == VAL_NULL) {
        return false;
    }
    if (value.type == VAL_BOOL) {
        return value.as.boolean;
    }
    return true;
}

void value_array_init(ValueArray *array) {
    if (!array) {
        return;
    }
    array->values = NULL;
    array->count = 0;
    array->capacity = 0;
}

static bool resize_array(ValueArray *array, size_t new_capacity) {
    Value *values = (Value *)realloc(array->values, new_capacity * sizeof(Value));
    if (!values) {
        return false;
    }
    array->values = values;
    array->capacity = new_capacity;
    return true;
}

bool value_array_write(ValueArray *array, Value value) {
    if (!array) {
        return false;
    }
    if (array->count == array->capacity) {
        size_t new_capacity = grow_capacity(array->capacity);
        if (!resize_array(array, new_capacity)) {
            return false;
        }
    }
    array->values[array->count++] = value;
    return true;
}

void value_array_free(ValueArray *array) {
    if (!array) {
        return;
    }
    free(array->values);
    array->values = NULL;
    array->count = 0;
    array->capacity = 0;
}

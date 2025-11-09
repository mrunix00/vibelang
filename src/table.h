#ifndef VIBELANG_TABLE_H
#define VIBELANG_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "object.h"

typedef struct {
    ObjString **keys;
    size_t count;
    size_t capacity;
} Table;

void table_init(Table *table);
void table_free(Table *table);
void table_define(Table *table, ObjString *key);
ObjString *table_find_string(Table *table, const char *chars, size_t length, uint32_t hash);
void table_remove_white(Table *table);


#endif

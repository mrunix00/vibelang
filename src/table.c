#include "table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ensure_capacity(Table *table, size_t required) {
    if (required <= table->capacity) {
        return true;
    }
    size_t new_capacity = table->capacity == 0 ? 8 : table->capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
        if (new_capacity < table->capacity) {
            return false;
        }
    }
    ObjString **keys = (ObjString **)realloc(table->keys, new_capacity * sizeof(ObjString *));
    if (!keys) {
        return false;
    }
    for (size_t i = table->capacity; i < new_capacity; ++i) {
        keys[i] = NULL;
    }
    table->keys = keys;
    table->capacity = new_capacity;
    return true;
}

static bool string_equals(const ObjString *string, const char *chars, size_t length, uint32_t hash) {
    if (!string) {
        return false;
    }
    if (string->hash != hash || string->length != length) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    return memcmp(string->chars, chars, length) == 0;
}

void table_init(Table *table) {
    if (!table) {
        return;
    }
    table->keys = NULL;
    table->count = 0;
    table->capacity = 0;
}

void table_free(Table *table) {
    if (!table) {
        return;
    }
    free(table->keys);
    table->keys = NULL;
    table->count = 0;
    table->capacity = 0;
}

void table_define(Table *table, ObjString *key) {
    if (!table || !key) {
        return;
    }
    if (table_find_string(table, key->chars, key->length, key->hash)) {
        return;
    }
    if (!ensure_capacity(table, table->count + 1)) {
        fprintf(stderr, "Failed to grow intern table.\n");
        exit(EXIT_FAILURE);
    }
    table->keys[table->count++] = key;
}

ObjString *table_find_string(Table *table, const char *chars, size_t length, uint32_t hash) {
    if (!table) {
        return NULL;
    }
    for (size_t i = 0; i < table->count; ++i) {
        ObjString *entry = table->keys[i];
        if (string_equals(entry, chars, length, hash)) {
            return entry;
        }
    }
    return NULL;
}

void table_remove_white(Table *table) {
    if (!table) {
        return;
    }
    size_t write_index = 0;
    for (size_t i = 0; i < table->count; ++i) {
        ObjString *entry = table->keys[i];
        if (entry && entry->obj.marked) {
            table->keys[write_index++] = entry;
        }
    }
    table->count = write_index;
}

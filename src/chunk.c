#include "chunk.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int grow_capacity(int capacity) {
    return capacity < 8 ? 8 : capacity * 2;
}

static bool ensure_capacity(Chunk *chunk, int needed) {
    if (chunk->capacity >= needed) {
        return true;
    }
    int new_capacity = grow_capacity(chunk->capacity);
    while (new_capacity < needed) {
        new_capacity = grow_capacity(new_capacity);
    }
    uint8_t *code = (uint8_t *)realloc(chunk->code, (size_t)new_capacity * sizeof(uint8_t));
    if (!code) {
        return false;
    }
    int *lines = (int *)realloc(chunk->lines, (size_t)new_capacity * sizeof(int));
    if (!lines) {
        free(code);
        return false;
    }
    chunk->code = code;
    chunk->lines = lines;
    chunk->capacity = new_capacity;
    return true;
}

void chunk_init(Chunk *chunk) {
    if (!chunk) {
        return;
    }
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    value_array_init(&chunk->constants);
}

void chunk_write(Chunk *chunk, uint8_t byte, int line) {
    if (!chunk) {
        return;
    }
    if (!ensure_capacity(chunk, chunk->count + 1)) {
        fprintf(stderr, "Out of memory while growing chunk.\n");
        exit(EXIT_FAILURE);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

uint16_t chunk_add_constant(Chunk *chunk, Value value) {
    if (!chunk) {
        return UINT16_MAX;
    }
    if (!value_array_write(&chunk->constants, value)) {
        fprintf(stderr, "Out of memory while adding constant.\n");
        exit(EXIT_FAILURE);
    }
    if (chunk->constants.count >= UINT16_MAX) {
        fprintf(stderr, "Too many constants in chunk.\n");
        exit(EXIT_FAILURE);
    }
    return (uint16_t)(chunk->constants.count - 1);
}

Value chunk_get_constant(const Chunk *chunk, uint16_t index) {
    if (!chunk || index >= chunk->constants.count) {
        fprintf(stderr, "Invalid constant index lookup: %u.\n", index);
        exit(EXIT_FAILURE);
    }
    return chunk->constants.values[index];
}

void chunk_free(Chunk *chunk) {
    if (!chunk) {
        return;
    }
    free(chunk->code);
    free(chunk->lines);
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    value_array_free(&chunk->constants);
}

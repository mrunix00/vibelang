#ifndef VIBELANG_CHUNK_H
#define VIBELANG_CHUNK_H

#include <stdint.h>

#include "value.h"

typedef enum {
    OP_LOAD_CONST,
    OP_LOAD_NULL,
    OP_LOAD_TRUE,
    OP_LOAD_FALSE,
    OP_MOVE,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_NOT,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_RETURN,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL
} OpCode;

typedef struct {
    uint8_t *code;
    int *lines;
    int count;
    int capacity;
    ValueArray constants;
} Chunk;

void chunk_init(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte, int line);
uint16_t chunk_add_constant(Chunk *chunk, Value value);
Value chunk_get_constant(const Chunk *chunk, uint16_t index);
void chunk_free(Chunk *chunk);

#endif

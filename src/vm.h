#ifndef VIBELANG_VM_H
#define VIBELANG_VM_H

#include <stdint.h>

#include "object.h"
#include "table.h"
#include "value.h"

typedef enum {
    INTERPRET_OK,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
    ObjFunction *function;
    const uint8_t *ip;
    Value *registers;
    Value *caller_registers;
    uint8_t return_reg;
} CallFrame;

typedef struct VM {
    CallFrame *frames;
    int frame_capacity;
    int frame_count;
    Value *stack;
    int stack_capacity;
    Value *stack_top;
    Value *globals;
    bool *global_defined;
    size_t global_count;
    size_t global_capacity;
    Table strings;
    Obj *objects;
    size_t bytes_allocated;
    size_t next_gc;
    Obj **gray_stack;
    int gray_count;
    int gray_capacity;
} VM;

void vm_init(VM *vm);
void vm_free(VM *vm);
InterpretResult vm_interpret(VM *vm, ObjFunction *function, Value *result_out);
void vm_collect_garbage(VM *vm);
void vm_push(VM *vm, Value value);
Value vm_pop(VM *vm);

#endif

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../libs/Unity/src/unity.h"

#include "chunk.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static uint16_t add_constant(Chunk *chunk, Value value, int line) {
    uint16_t index = chunk_add_constant(chunk, value);
    (void)line;
    return index;
}

static void ensure_register_count(ObjFunction *function, int required) {
    if (required > function->register_count) {
        function->register_count = required;
    }
}

static void write_load_const(Chunk *chunk, uint8_t dest, Value value, int line) {
    uint16_t index = add_constant(chunk, value, line);
    chunk_write(chunk, OP_LOAD_CONST, line);
    chunk_write(chunk, dest, line);
    chunk_write(chunk, (uint8_t)((index >> 8) & 0xFF), line);
    chunk_write(chunk, (uint8_t)(index & 0xFF), line);
}

static void write_load_bool(Chunk *chunk, uint8_t dest, bool value, int line) {
    chunk_write(chunk, value ? OP_LOAD_TRUE : OP_LOAD_FALSE, line);
    chunk_write(chunk, dest, line);
}

static void write_load_null(Chunk *chunk, uint8_t dest, int line) {
    chunk_write(chunk, OP_LOAD_NULL, line);
    chunk_write(chunk, dest, line);
}

static void write_binary(Chunk *chunk, OpCode opcode, uint8_t dest, uint8_t left, uint8_t right, int line) {
    chunk_write(chunk, opcode, line);
    chunk_write(chunk, dest, line);
    chunk_write(chunk, left, line);
    chunk_write(chunk, right, line);
}

static void write_unary(Chunk *chunk, OpCode opcode, uint8_t dest, uint8_t operand, int line) {
    chunk_write(chunk, opcode, line);
    chunk_write(chunk, dest, line);
    chunk_write(chunk, operand, line);
}

static int write_jump(Chunk *chunk, int line) {
    chunk_write(chunk, OP_JUMP, line);
    chunk_write(chunk, 0xFF, line);
    chunk_write(chunk, 0xFF, line);
    return chunk->count - 2;
}

static int write_jump_if_false(Chunk *chunk, uint8_t condition_reg, int line) {
    chunk_write(chunk, OP_JUMP_IF_FALSE, line);
    chunk_write(chunk, condition_reg, line);
    chunk_write(chunk, 0xFF, line);
    chunk_write(chunk, 0xFF, line);
    return chunk->count - 2;
}

static void patch_jump(Chunk *chunk, int jump_start) {
    int offset = chunk->count - (jump_start + 2);
    if (offset > UINT16_MAX) {
        TEST_FAIL_MESSAGE("Jump offset exceeds limit");
    }
    chunk->code[jump_start] = (uint8_t)((offset >> 8) & 0xFF);
    chunk->code[jump_start + 1] = (uint8_t)(offset & 0xFF);
}

static void write_loop(Chunk *chunk, int loop_start, int line) {
    chunk_write(chunk, OP_LOOP, line);
    chunk_write(chunk, 0, line);
    chunk_write(chunk, 0, line);
    int offset = chunk->count - loop_start;
    if (offset > UINT16_MAX) {
        TEST_FAIL_MESSAGE("Loop offset exceeds limit");
    }
    chunk->code[chunk->count - 2] = (uint8_t)((offset >> 8) & 0xFF);
    chunk->code[chunk->count - 1] = (uint8_t)(offset & 0xFF);
}

static void write_return(Chunk *chunk, uint8_t src, int line) {
    chunk_write(chunk, OP_RETURN, line);
    chunk_write(chunk, src, line);
}

static void write_call(Chunk *chunk, uint8_t dest, uint8_t callee, uint8_t arg_count, const uint8_t *args, int line) {
    chunk_write(chunk, OP_CALL, line);
    chunk_write(chunk, dest, line);
    chunk_write(chunk, callee, line);
    chunk_write(chunk, arg_count, line);
    for (uint8_t i = 0; i < arg_count; ++i) {
        chunk_write(chunk, args[i], line);
    }
}

static void write_get_global(Chunk *chunk, uint8_t dest, uint16_t slot, int line) {
    chunk_write(chunk, OP_GET_GLOBAL, line);
    chunk_write(chunk, dest, line);
    chunk_write(chunk, (uint8_t)((slot >> 8) & 0xFF), line);
    chunk_write(chunk, (uint8_t)(slot & 0xFF), line);
}

static void write_define_global(Chunk *chunk, uint8_t src, uint16_t slot, int line) {
    chunk_write(chunk, OP_DEFINE_GLOBAL, line);
    chunk_write(chunk, src, line);
    chunk_write(chunk, (uint8_t)((slot >> 8) & 0xFF), line);
    chunk_write(chunk, (uint8_t)(slot & 0xFF), line);
}

static Value make_string_value(VM *vm, const char *text) {
    size_t length = text ? strlen(text) : 0;
    ObjString *string = obj_string_copy(vm, text, length);
    return value_make_string(string);
}

static uint32_t hash_bytes(const char *chars, size_t length) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;
    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < length; ++i) {
        hash ^= (uint32_t)(unsigned char)chars[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static void assert_number_close(double expected, Value value) {
    TEST_ASSERT_TRUE(value_is_number(value));
    double delta = fabs(value_as_number(value) - expected);
    TEST_ASSERT_TRUE(delta < 1e-6);
}

static void assert_string_equal(const char *expected, Value value) {
    TEST_ASSERT_TRUE(value_is_string(value));
    ObjString *string = value_as_string(value);
    TEST_ASSERT_NOT_NULL(string->chars);
    TEST_ASSERT_EQUAL_STRING(expected ? expected : "", string->chars);
}

void test_vm_arithmetic_addition(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(1.0), 1);
    write_load_const(chunk, 1, value_make_number(2.0), 1);
    write_binary(chunk, OP_ADD, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(3.0, result);

    vm_free(&vm);
}

void test_vm_arithmetic_subtract(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(5.0), 1);
    write_load_const(chunk, 1, value_make_number(3.0), 1);
    write_binary(chunk, OP_SUBTRACT, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(2.0, result);

    vm_free(&vm);
}

void test_vm_arithmetic_multiply(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(4.0), 1);
    write_load_const(chunk, 1, value_make_number(3.0), 1);
    write_binary(chunk, OP_MULTIPLY, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(12.0, result);

    vm_free(&vm);
}

void test_vm_arithmetic_divide(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(10.0), 1);
    write_load_const(chunk, 1, value_make_number(2.0), 1);
    write_binary(chunk, OP_DIVIDE, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(5.0, result);

    vm_free(&vm);
}

void test_vm_comparison_greater(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(5.0), 1);
    write_load_const(chunk, 1, value_make_number(3.0), 1);
    write_binary(chunk, OP_GREATER, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_bool(result));
    TEST_ASSERT_TRUE(value_as_bool(result));

    vm_free(&vm);
}

void test_vm_comparison_less(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(3.0), 1);
    write_load_const(chunk, 1, value_make_number(5.0), 1);
    write_binary(chunk, OP_LESS, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_bool(result));
    TEST_ASSERT_TRUE(value_as_bool(result));

    vm_free(&vm);
}

void test_vm_comparison_equal(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(5.0), 1);
    write_load_const(chunk, 1, value_make_number(5.0), 1);
    write_binary(chunk, OP_EQUAL, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_bool(result));
    TEST_ASSERT_TRUE(value_as_bool(result));

    vm_free(&vm);
}

void test_vm_unary_not(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_bool(chunk, 0, false, 1);
    write_unary(chunk, OP_NOT, 0, 0, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_bool(result));
    TEST_ASSERT_TRUE(value_as_bool(result));

    vm_free(&vm);
}

void test_vm_unary_negate(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(7.0), 1);
    write_unary(chunk, OP_NEGATE, 0, 0, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(-7.0, result);

    vm_free(&vm);
}

void test_vm_boolean_literals(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_bool(chunk, 0, true, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_bool(result));
    TEST_ASSERT_TRUE(value_as_bool(result));

    vm_free(&vm);
}

void test_vm_null_literal(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_null(chunk, 0, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    TEST_ASSERT_TRUE(value_is_null(result));

    vm_free(&vm);
}

void test_vm_string_literal(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, make_string_value(&vm, "hello"), 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_string_equal("hello", result);

    vm_free(&vm);
}

void test_vm_string_concatenation(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, make_string_value(&vm, "foo"), 1);
    write_load_const(chunk, 1, make_string_value(&vm, "bar"), 1);
    write_binary(chunk, OP_ADD, 0, 0, 1, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_string_equal("foobar", result);

    vm_free(&vm);
}

void test_vm_function_call(void) {
    VM vm;
    vm_init(&vm);

    // Create add function: takes two args, adds them
    ObjFunction *add_func = obj_function_new(&vm, "add", 2);
    Chunk *add_chunk = &add_func->chunk;
    write_binary(add_chunk, OP_ADD, 0, 0, 1, 1);
    write_return(add_chunk, 0, 1);
    ensure_register_count(add_func, 2);

    // Main function
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;
    write_load_const(chunk, 0, value_make_function(add_func), 1);
    write_load_const(chunk, 1, value_make_number(3.0), 1);
    write_load_const(chunk, 2, value_make_number(4.0), 1);
    uint8_t args_regs[2] = {1, 2};
    write_call(chunk, 0, 0, 2, args_regs, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 3);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(7.0, result);

    vm_free(&vm);
}

void test_vm_if_else(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_bool(chunk, 0, true, 1);
    int else_jump = write_jump_if_false(chunk, 0, 1);
    write_load_const(chunk, 0, value_make_number(10.0), 1);
    int end_jump = write_jump(chunk, 1);
    patch_jump(chunk, else_jump);
    write_load_const(chunk, 0, value_make_number(20.0), 1);
    patch_jump(chunk, end_jump);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(10.0, result);

    vm_free(&vm);
}

void test_vm_runtime_error_call_non_function(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(42.0), 1);
    write_call(chunk, 0, 0, 0, NULL, 1);
    write_return(chunk, 0, 1);
    ensure_register_count(function, 1);

    InterpretResult status = vm_interpret(&vm, function, NULL);
    TEST_ASSERT_EQUAL_INT(INTERPRET_RUNTIME_ERROR, status);

    vm_free(&vm);
}

void test_vm_runtime_error_undefined_global(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "main", 0);
    Chunk *chunk = &function->chunk;

    write_get_global(chunk, 0, 0, 1);
    ensure_register_count(function, 1);

    InterpretResult status = vm_interpret(&vm, function, NULL);
    TEST_ASSERT_EQUAL_INT(INTERPRET_RUNTIME_ERROR, status);

    vm_free(&vm);
}

void test_vm_global_roundtrip(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "script", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, value_make_number(42.0), 1);
    write_define_global(chunk, 0, 0, 1);
    write_get_global(chunk, 1, 0, 1);
    write_return(chunk, 1, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(42.0, result);

    vm_free(&vm);
}

void test_vm_global_string_roundtrip(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "script", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 0, make_string_value(&vm, "alpha"), 1);
    write_define_global(chunk, 0, 0, 1);
    write_get_global(chunk, 1, 0, 1);
    write_return(chunk, 1, 1);
    ensure_register_count(function, 2);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_string_equal("alpha", result);

    vm_free(&vm);
}

void test_vm_locals_and_control_flow(void) {
    VM vm;
    vm_init(&vm);
    ObjFunction *function = obj_function_new(&vm, "loop", 0);
    Chunk *chunk = &function->chunk;

    write_load_const(chunk, 1, value_make_number(0.0), 1); // i
    write_load_const(chunk, 2, value_make_number(0.0), 1); // sum
    write_load_const(chunk, 3, value_make_number(3.0), 1); // limit
    write_load_const(chunk, 4, value_make_number(1.0), 1); // increment

    int loop_start = chunk->count;

    write_binary(chunk, OP_LESS, 5, 1, 3, 1);
    int exit_jump = write_jump_if_false(chunk, 5, 1);

    write_binary(chunk, OP_ADD, 2, 2, 1, 1);
    write_binary(chunk, OP_ADD, 1, 1, 4, 1);
    write_loop(chunk, loop_start, 1);
    patch_jump(chunk, exit_jump);

    write_return(chunk, 2, 1);
    ensure_register_count(function, 6);

    Value result = value_make_null();
    InterpretResult status = vm_interpret(&vm, function, &result);
    TEST_ASSERT_EQUAL_INT(INTERPRET_OK, status);
    assert_number_close(3.0, result);

    vm_free(&vm);
}

void test_vm_garbage_collection_reclaims_unreferenced_strings(void) {
    VM vm;
    vm_init(&vm);

    Value root_value = make_string_value(&vm, "rooted");
    vm_push(&vm, root_value);
    size_t before = vm.bytes_allocated;

    const char *temp_text = "ephemeral";
    size_t temp_length = strlen(temp_text);
    obj_string_copy(&vm, temp_text, temp_length);
    TEST_ASSERT_TRUE(vm.bytes_allocated > before);

    vm_collect_garbage(&vm);

    uint32_t temp_hash = hash_bytes(temp_text, temp_length);
    TEST_ASSERT_NULL(table_find_string(&vm.strings, temp_text, temp_length, temp_hash));
    TEST_ASSERT_TRUE(vm.bytes_allocated <= before);

    vm_pop(&vm);
    vm_free(&vm);
}

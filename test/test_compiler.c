#include "../libs/Unity/src/unity.h"

#include "compiler.h"
#include "value.h"
#include "object.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    VM vm;
    Value result;
} RunResult;

static RunResult run_source_or_fail(const char *source) {
    RunResult run;
    vm_init(&run.vm);
    run.result = value_make_null();
    char *error = NULL;
    bool ok = compiler_run_source(&run.vm, source, &run.result, &error);
    if (!ok) {
        if (error) {
            TEST_FAIL_MESSAGE(error);
        } else {
            TEST_FAIL_MESSAGE("compiler_run_source failed");
        }
    }
    return run;
}

static void assert_number(double expected, Value actual) {
    TEST_ASSERT_TRUE(value_is_number(actual));
    double diff = fabs(value_as_number(actual) - expected);
    TEST_ASSERT_TRUE(diff < 1e-9);
}

static void assert_string(const char *expected, Value actual) {
    TEST_ASSERT_TRUE(value_is_string(actual));
    ObjString *string = value_as_string(actual);
    TEST_ASSERT_NOT_NULL(string);
    TEST_ASSERT_NOT_NULL(string->chars);
    TEST_ASSERT_EQUAL_STRING(expected, string->chars);
}

void test_compile_arithmetic_script(void) {
    const char *source =
        "let x = 41;\n"
        "let y = 1;\n"
        "x + y;\n";
    RunResult run = run_source_or_fail(source);
    assert_number(42.0, run.result);
    vm_free(&run.vm);
}

void test_compile_if_else_script(void) {
    const char *source =
        "let x = 10;\n"
        "if (x > 5) {\n"
        "  x = x + 1;\n"
        "} else {\n"
        "  x = x - 1;\n"
        "}\n"
        "x;\n";
    RunResult run = run_source_or_fail(source);
    assert_number(11.0, run.result);
    vm_free(&run.vm);
}

void test_compile_function_call_script(void) {
    const char *source =
        "function add(a, b) {\n"
        "  return a + b;\n"
        "}\n"
        "add(3, 4);\n";
    RunResult run = run_source_or_fail(source);
    assert_number(7.0, run.result);
    vm_free(&run.vm);
}

void test_compile_while_loop_script(void) {
    const char *source =
        "let sum = 0;\n"
        "let i = 0;\n"
        "while (i < 4) {\n"
        "  sum = sum + i;\n"
        "  i = i + 1;\n"
        "}\n"
        "sum;\n";
    RunResult run = run_source_or_fail(source);
    assert_number(6.0, run.result);
    vm_free(&run.vm);
}

void test_compile_string_concatenation_script(void) {
    const char *source =
        "let a = \"foo\";\n"
        "let b = \"bar\";\n"
        "a + b;\n";
    RunResult run = run_source_or_fail(source);
    assert_string("foobar", run.result);
    vm_free(&run.vm);
}

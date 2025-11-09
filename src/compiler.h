#ifndef VIBELANG_COMPILER_H
#define VIBELANG_COMPILER_H

#include <stdbool.h>

#include "parser.h"
#include "vm.h"

/**
 * Compile an abstract syntax tree produced by the parser into an ObjFunction
 * that can be executed by the VM. On failure, returns NULL and, if
 * error_message is not NULL, stores a heap-allocated error description that the
 * caller must free.
 */
ObjFunction *compiler_compile(VM *vm, const Program *program, char **error_message);

/**
 * Convenience helper to compile a program AST and immediately execute it via
 * the VM. Returns true on success. On failure, returns false and, if
 * error_message is not NULL, stores a heap-allocated description.
 */
bool compiler_run_program(VM *vm, const Program *program, Value *result_out, char **error_message);

/**
 * Parse, compile, and execute the given source string. Returns true on
 * success. On failure, returns false and, if error_message is not NULL, stores
 * a heap-allocated description from either the parser or compiler stages.
 */
bool compiler_run_source(VM *vm, const char *source, Value *result_out, char **error_message);

#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "object.h"
#include "value.h"

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
}

static void print_value(Value value) {
    switch (value.type) {
        case VAL_NULL:
            printf("null\n");
            break;
        case VAL_BOOL:
            printf(value.as.boolean ? "true\n" : "false\n");
            break;
        case VAL_NUMBER:
            printf("%g\n", value.as.number);
            break;
        case VAL_OBJ:
            if (value_is_string(value)) {
                ObjString *string = value_as_string(value);
                printf("%s\n", string && string->chars ? string->chars : "");
            } else if (value_is_function(value)) {
                ObjFunction *function = value_as_function(value);
                const char *name = (function && function->name && function->name->chars) ? function->name->chars : "<fn>";
                printf("<function %s>\n", name);
            } else {
                printf("<object>\n");
            }
            break;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <script-file>\n", argc > 0 ? argv[0] : "vibelang");
        return EXIT_FAILURE;
    }
    char *source = read_file(argv[1]);
    if (!source) {
        fprintf(stderr, "Failed to read file '%s'.\n", argv[1]);
        return EXIT_FAILURE;
    }

    VM vm;
    vm_init(&vm);
    Value result = value_make_null();
    char *error = NULL;
    bool ok = compiler_run_source(&vm, source, &result, &error);
    if (!ok) {
        if (error) {
            fprintf(stderr, "%s\n", error);
            free(error);
        } else {
            fprintf(stderr, "Execution failed.\n");
        }
        vm_free(&vm);
        free(source);
        return EXIT_FAILURE;
    }

    print_value(result);
    vm_free(&vm);
    free(source);
    return EXIT_SUCCESS;
}
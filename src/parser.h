#ifndef VIBELANG_PARSER_H
#define VIBELANG_PARSER_H

#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Expression Expression;
typedef struct Statement Statement;
typedef struct ClassMethod ClassMethod;

typedef struct ExpressionList {
    Expression **items;
    size_t count;
    size_t capacity;
} ExpressionList;

typedef enum {
    EXPR_LITERAL_NUMBER,
    EXPR_LITERAL_STRING,
    EXPR_LITERAL_BOOL,
    EXPR_LITERAL_NULL,
    EXPR_IDENTIFIER,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_ASSIGNMENT,
    EXPR_CALL,
    EXPR_ARRAY,
    EXPR_INDEX,
    EXPR_THIS,
    EXPR_GET_PROPERTY,
    EXPR_SET_PROPERTY,
    EXPR_INVOKE
} ExpressionType;

struct Expression {
    ExpressionType type;
    union {
        struct {
            double value;
        } number_literal;
        struct {
            char *value;
        } string_literal;
        struct {
            bool value;
        } bool_literal;
        struct {
            char *name;
        } identifier;
        struct {
            TokenType operator_type;
            Expression *right;
        } unary;
        struct {
            Expression *left;
            TokenType operator_type;
            Expression *right;
        } binary;
        struct {
            char *name;
            Expression *value;
        } assignment;
        struct {
            Expression *callee;
            ExpressionList arguments;
        } call;
        struct {
            ExpressionList elements;
        } array_literal;
        struct {
            Expression *array;
            Expression *index;
        } index;
        struct {
        } this_expression;
        struct {
            Expression *object;
            char *name;
        } get_property;
        struct {
            Expression *object;
            char *name;
            Expression *value;
        } set_property;
        struct {
            Expression *object;
            char *name;
            ExpressionList arguments;
        } invoke;
    } as;
};

typedef struct StatementList {
    Statement **items;
    size_t count;
    size_t capacity;
} StatementList;

typedef enum {
    STMT_LET,
    STMT_EXPRESSION,
    STMT_IF,
    STMT_WHILE,
    STMT_BLOCK,
    STMT_FUNCTION,
    STMT_RETURN,
    STMT_CLASS
} StatementType;

struct ClassMethod {
    char *name;
    bool is_constructor;
    char **parameters;
    size_t parameter_count;
    size_t parameter_capacity;
    Statement *body;
};

struct Statement {
    StatementType type;
    union {
        struct {
            char *name;
            bool has_initializer;
            Expression *initializer;
        } let_statement;
        struct {
            Expression *expression;
        } expression_statement;
        struct {
            Expression *condition;
            Statement *then_branch;
            Statement *else_branch;
        } if_statement;
        struct {
            Expression *condition;
            Statement *body;
        } while_statement;
        struct {
            StatementList statements;
        } block_statement;
        struct {
            char *name;
            char **parameters;
            size_t parameter_count;
            size_t parameter_capacity;
            Statement *body;
        } function_statement;
        struct {
            bool has_value;
            Expression *value;
        } return_statement;
        struct {
            char *name;
            ClassMethod *methods;
            size_t method_count;
            size_t method_capacity;
        } class_statement;
    } as;
};

typedef struct Program {
    StatementList statements;
} Program;

Program *parser_parse(const char *source, char **error_message);
void program_free(Program *program);

#endif

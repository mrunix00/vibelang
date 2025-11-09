#include "../libs/Unity/src/unity.h"
#include "../src/parser.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static Program *parse_success(const char *source) {
    char *error = NULL;
    Program *program = parser_parse(source, &error);
    if (error) {
        TEST_FAIL_MESSAGE(error);
    }
    TEST_ASSERT_NOT_NULL(program);
    return program;
}

static void assert_double_equal(double expected, double actual) {
    double diff = fabs(expected - actual);
    TEST_ASSERT_TRUE_MESSAGE(diff < 1e-9, "Double mismatch");
}

void test_parse_variable_declarations(void) {
    Program *program = parse_success("let x = 5; let y;");

    TEST_ASSERT_EQUAL_UINT(2, (unsigned int)program->statements.count);

    Statement *first = program->statements.items[0];
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_INT(STMT_LET, first->type);
    TEST_ASSERT_TRUE(first->as.let_statement.has_initializer);
    TEST_ASSERT_EQUAL_STRING("x", first->as.let_statement.name);
    TEST_ASSERT_NOT_NULL(first->as.let_statement.initializer);
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, first->as.let_statement.initializer->type);
    assert_double_equal(5.0, first->as.let_statement.initializer->as.number_literal.value);

    Statement *second = program->statements.items[1];
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_INT(STMT_LET, second->type);
    TEST_ASSERT_FALSE(second->as.let_statement.has_initializer);
    TEST_ASSERT_NULL(second->as.let_statement.initializer);
    TEST_ASSERT_EQUAL_STRING("y", second->as.let_statement.name);

    program_free(program);
}

void test_parse_assignment_statement(void) {
    Program *program = parse_success("x = 10;");

    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)program->statements.count);
    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, stmt->type);

    Expression *expr = stmt->as.expression_statement.expression;
    TEST_ASSERT_NOT_NULL(expr);
    TEST_ASSERT_EQUAL_INT(EXPR_ASSIGNMENT, expr->type);
    TEST_ASSERT_EQUAL_STRING("x", expr->as.assignment.name);
    TEST_ASSERT_NOT_NULL(expr->as.assignment.value);
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, expr->as.assignment.value->type);
    assert_double_equal(10.0, expr->as.assignment.value->as.number_literal.value);

    program_free(program);
}

void test_parse_if_else(void) {
    const char *source =
        "if (x > 0) {\n"
        "  x = x - 1;\n"
        "} else {\n"
        "  x = 0;\n"
        "}";

    Program *program = parse_success(source);

    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)program->statements.count);
    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_IF, stmt->type);

    Expression *condition = stmt->as.if_statement.condition;
    TEST_ASSERT_NOT_NULL(condition);
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, condition->type);
    TEST_ASSERT_EQUAL_INT(TOKEN_GREATER, condition->as.binary.operator_type);

    Expression *left = condition->as.binary.left;
    TEST_ASSERT_EQUAL_INT(EXPR_IDENTIFIER, left->type);
    TEST_ASSERT_EQUAL_STRING("x", left->as.identifier.name);

    Expression *right = condition->as.binary.right;
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, right->type);
    assert_double_equal(0.0, right->as.number_literal.value);

    Statement *then_branch = stmt->as.if_statement.then_branch;
    TEST_ASSERT_NOT_NULL(then_branch);
    TEST_ASSERT_EQUAL_INT(STMT_BLOCK, then_branch->type);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)then_branch->as.block_statement.statements.count);

    Statement *then_stmt = then_branch->as.block_statement.statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, then_stmt->type);
    Expression *then_expr = then_stmt->as.expression_statement.expression;
    TEST_ASSERT_EQUAL_INT(EXPR_ASSIGNMENT, then_expr->type);

    Statement *else_branch = stmt->as.if_statement.else_branch;
    TEST_ASSERT_NOT_NULL(else_branch);
    TEST_ASSERT_EQUAL_INT(STMT_BLOCK, else_branch->type);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)else_branch->as.block_statement.statements.count);

    Statement *else_stmt = else_branch->as.block_statement.statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, else_stmt->type);
    Expression *else_expr = else_stmt->as.expression_statement.expression;
    TEST_ASSERT_EQUAL_INT(EXPR_ASSIGNMENT, else_expr->type);

    program_free(program);
}

void test_parse_while_loop(void) {
    Program *program = parse_success("while (x < 10) { x = x + 1; }");

    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)program->statements.count);
    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_WHILE, stmt->type);
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, stmt->as.while_statement.condition->type);

    Statement *body = stmt->as.while_statement.body;
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_INT(STMT_BLOCK, body->type);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)body->as.block_statement.statements.count);

    program_free(program);
}

void test_parse_function_declaration(void) {
    const char *source =
        "function add(a, b) {\n"
        "  return a + b;\n"
        "}";

    Program *program = parse_success(source);

    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)program->statements.count);
    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_FUNCTION, stmt->type);
    TEST_ASSERT_EQUAL_STRING("add", stmt->as.function_statement.name);
    TEST_ASSERT_EQUAL_UINT(2, (unsigned int)stmt->as.function_statement.parameter_count);

    Statement *body = stmt->as.function_statement.body;
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_INT(STMT_BLOCK, body->type);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)body->as.block_statement.statements.count);

    Statement *return_stmt = body->as.block_statement.statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_RETURN, return_stmt->type);
    TEST_ASSERT_TRUE(return_stmt->as.return_statement.has_value);
    Expression *value = return_stmt->as.return_statement.value;
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, value->type);

    program_free(program);
}

void test_expression_precedence(void) {
    Program *program = parse_success("let value = 1 + 2 * 3;");

    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_LET, stmt->type);
    Expression *init = stmt->as.let_statement.initializer;
    TEST_ASSERT_NOT_NULL(init);
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, init->type);
    TEST_ASSERT_EQUAL_INT(TOKEN_PLUS, init->as.binary.operator_type);

    Expression *right = init->as.binary.right;
    TEST_ASSERT_NOT_NULL(right);
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, right->type);
    TEST_ASSERT_EQUAL_INT(TOKEN_STAR, right->as.binary.operator_type);

    program_free(program);
}

void test_parse_call_expression(void) {
    Program *program = parse_success("print(\"hi\");");

    Statement *stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, stmt->type);
    Expression *expr = stmt->as.expression_statement.expression;
    TEST_ASSERT_EQUAL_INT(EXPR_CALL, expr->type);
    TEST_ASSERT_EQUAL_INT(EXPR_IDENTIFIER, expr->as.call.callee->type);
    TEST_ASSERT_EQUAL_STRING("print", expr->as.call.callee->as.identifier.name);
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)expr->as.call.arguments.count);
    Expression *arg = expr->as.call.arguments.items[0];
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_STRING, arg->type);
    TEST_ASSERT_EQUAL_STRING("hi", arg->as.string_literal.value);

    program_free(program);
}

void test_parse_array_literal_and_index(void) {
    const char *source =
        "let list = [1, 2, 3];\n"
        "list += 4;\n"
        "list[2];";

    Program *program = parse_success(source);

    TEST_ASSERT_EQUAL_UINT(3, (unsigned int)program->statements.count);

    Statement *let_stmt = program->statements.items[0];
    TEST_ASSERT_EQUAL_INT(STMT_LET, let_stmt->type);
    Expression *initializer = let_stmt->as.let_statement.initializer;
    TEST_ASSERT_NOT_NULL(initializer);
    TEST_ASSERT_EQUAL_INT(EXPR_ARRAY, initializer->type);
    TEST_ASSERT_EQUAL_UINT(3, (unsigned int)initializer->as.array_literal.elements.count);
    Expression *first_element = initializer->as.array_literal.elements.items[0];
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, first_element->type);
    assert_double_equal(1.0, first_element->as.number_literal.value);

    Statement *assign_stmt = program->statements.items[1];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, assign_stmt->type);
    Expression *assignment_expr = assign_stmt->as.expression_statement.expression;
    TEST_ASSERT_EQUAL_INT(EXPR_ASSIGNMENT, assignment_expr->type);
    TEST_ASSERT_EQUAL_STRING("list", assignment_expr->as.assignment.name);
    Expression *assigned_value = assignment_expr->as.assignment.value;
    TEST_ASSERT_EQUAL_INT(EXPR_BINARY, assigned_value->type);
    TEST_ASSERT_EQUAL_INT(TOKEN_PLUS, assigned_value->as.binary.operator_type);
    TEST_ASSERT_EQUAL_INT(EXPR_IDENTIFIER, assigned_value->as.binary.left->type);
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, assigned_value->as.binary.right->type);
    assert_double_equal(4.0, assigned_value->as.binary.right->as.number_literal.value);

    Statement *index_stmt = program->statements.items[2];
    TEST_ASSERT_EQUAL_INT(STMT_EXPRESSION, index_stmt->type);
    Expression *index_expr = index_stmt->as.expression_statement.expression;
    TEST_ASSERT_EQUAL_INT(EXPR_INDEX, index_expr->type);
    TEST_ASSERT_EQUAL_INT(EXPR_IDENTIFIER, index_expr->as.index.array->type);
    TEST_ASSERT_EQUAL_STRING("list", index_expr->as.index.array->as.identifier.name);
    TEST_ASSERT_EQUAL_INT(EXPR_LITERAL_NUMBER, index_expr->as.index.index->type);
    assert_double_equal(2.0, index_expr->as.index.index->as.number_literal.value);

    program_free(program);
}

void test_parser_reports_error(void) {
    char *error = NULL;
    Program *program = parser_parse("let x = ;", &error);

    TEST_ASSERT_NULL(program);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(strlen(error) > 0U);

    free(error);
}

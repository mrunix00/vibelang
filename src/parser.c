#include "parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    bool had_error;
    char *error_message;
} Parser;

static void expression_free_internal(Expression *expression);
static void expression_list_free(ExpressionList *list);
static void statement_free_internal(Statement *statement);
static void statement_list_free(StatementList *list);

static char *copy_string(const char *source) {
    if (!source) {
        return NULL;
    }
    size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    if (length > 0) {
        memcpy(copy, source, length);
    }
    copy[length] = '\0';
    return copy;
}

static void parser_error(Parser *parser, const char *message) {
    if (parser->had_error) {
        return;
    }
    parser->had_error = true;
    parser->error_message = copy_string(message ? message : "Parse error");
}

static void token_dispose(Token *token) {
    if (token && token->lexeme) {
        token_free(token);
    }
}

static void parser_init(Parser *parser, const char *source) {
    lexer_init(&parser->lexer, source);
    parser->previous.type = TOKEN_ERROR;
    parser->previous.lexeme = NULL;
    parser->previous.number_value = 0.0;
    parser->current = lexer_next_token(&parser->lexer);
    parser->had_error = false;
    parser->error_message = NULL;
    if (parser->current.type == TOKEN_ERROR) {
        parser_error(parser, parser->current.lexeme);
    }
}

static void advance(Parser *parser) {
    token_dispose(&parser->previous);
    parser->previous = parser->current;
    parser->current = lexer_next_token(&parser->lexer);
    if (parser->current.type == TOKEN_ERROR) {
        parser_error(parser, parser->current.lexeme);
    }
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) {
        return false;
    }
    advance(parser);
    return true;
}

static const Token *consume(Parser *parser, TokenType type, const char *message) {
    if (check(parser, type)) {
        advance(parser);
        return &parser->previous;
    }
    parser_error(parser, message);
    return NULL;
}

static bool statement_list_append(Parser *parser, StatementList *list, Statement *statement) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Statement **items = (Statement **)realloc(list->items, new_capacity * sizeof(Statement *));
        if (!items) {
            parser_error(parser, "Out of memory");
            return false;
        }
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = statement;
    return true;
}

static bool expression_list_append(Parser *parser, ExpressionList *list, Expression *expression) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Expression **items = (Expression **)realloc(list->items, new_capacity * sizeof(Expression *));
        if (!items) {
            parser_error(parser, "Out of memory");
            return false;
        }
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = expression;
    return true;
}

static void synchronize(Parser *parser) {
    if (!parser->had_error) {
        return;
    }
    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_SEMICOLON) {
            return;
        }
        switch (parser->current.type) {
            case TOKEN_KEYWORD_FUNCTION:
            case TOKEN_KEYWORD_LET:
            case TOKEN_KEYWORD_IF:
            case TOKEN_KEYWORD_WHILE:
            case TOKEN_KEYWORD_RETURN:
                return;
            default:
                break;
        }
        advance(parser);
    }
}

static Expression *allocate_expression(Parser *parser, ExpressionType type) {
    Expression *expression = (Expression *)calloc(1, sizeof(Expression));
    if (!expression) {
        parser_error(parser, "Out of memory");
        return NULL;
    }
    expression->type = type;
    return expression;
}

static Statement *allocate_statement(Parser *parser, StatementType type) {
    Statement *statement = (Statement *)calloc(1, sizeof(Statement));
    if (!statement) {
        parser_error(parser, "Out of memory");
        return NULL;
    }
    statement->type = type;
    return statement;
}

static Expression *parse_expression(Parser *parser);
static Expression *parse_assignment(Parser *parser);
static Expression *parse_equality(Parser *parser);
static Expression *parse_comparison(Parser *parser);
static Expression *parse_term(Parser *parser);
static Expression *parse_factor(Parser *parser);
static Expression *parse_unary(Parser *parser);
static Expression *parse_call(Parser *parser);
static Expression *parse_primary(Parser *parser);
static Statement *parse_declaration(Parser *parser);
static Statement *parse_statement(Parser *parser);
static Statement *parse_let_declaration(Parser *parser);
static Statement *parse_function_declaration(Parser *parser);
static Statement *parse_if_statement(Parser *parser);
static Statement *parse_while_statement(Parser *parser);
static Statement *parse_return_statement(Parser *parser);
static Statement *parse_block(Parser *parser);
static Statement *parse_expression_statement(Parser *parser);

static Expression *make_binary(Parser *parser, Expression *left, TokenType operator_type, Expression *right) {
    if (!left || !right) {
        expression_free_internal(left);
        expression_free_internal(right);
        return NULL;
    }
    Expression *expression = allocate_expression(parser, EXPR_BINARY);
    if (!expression) {
        expression_free_internal(left);
        expression_free_internal(right);
        return NULL;
    }
    expression->as.binary.left = left;
    expression->as.binary.operator_type = operator_type;
    expression->as.binary.right = right;
    return expression;
}

static Expression *finish_call(Parser *parser, Expression *callee) {
    Expression *call = allocate_expression(parser, EXPR_CALL);
    if (!call) {
        expression_free_internal(callee);
        return NULL;
    }
    call->as.call.callee = callee;
    call->as.call.arguments.items = NULL;
    call->as.call.arguments.count = 0;
    call->as.call.arguments.capacity = 0;

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            Expression *argument = parse_expression(parser);
            if (!argument) {
                expression_free_internal(call);
                return NULL;
            }
            if (!expression_list_append(parser, &call->as.call.arguments, argument)) {
                expression_free_internal(call);
                return NULL;
            }
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "Expect ')' after arguments.")) {
        expression_free_internal(call);
        return NULL;
    }

    return call;
}

static Expression *parse_primary(Parser *parser) {
    if (match(parser, TOKEN_KEYWORD_TRUE)) {
        Expression *expr = allocate_expression(parser, EXPR_LITERAL_BOOL);
        if (expr) {
            expr->as.bool_literal.value = true;
        }
        return expr;
    }
    if (match(parser, TOKEN_KEYWORD_FALSE)) {
        Expression *expr = allocate_expression(parser, EXPR_LITERAL_BOOL);
        if (expr) {
            expr->as.bool_literal.value = false;
        }
        return expr;
    }
    if (match(parser, TOKEN_KEYWORD_NULL)) {
        return allocate_expression(parser, EXPR_LITERAL_NULL);
    }
    if (match(parser, TOKEN_NUMBER)) {
        Expression *expr = allocate_expression(parser, EXPR_LITERAL_NUMBER);
        if (expr) {
            expr->as.number_literal.value = parser->previous.number_value;
        }
        return expr;
    }
    if (match(parser, TOKEN_STRING)) {
        Expression *expr = allocate_expression(parser, EXPR_LITERAL_STRING);
        if (expr) {
            expr->as.string_literal.value = copy_string(parser->previous.lexeme);
            if (!expr->as.string_literal.value) {
                parser_error(parser, "Out of memory");
            }
        }
        return expr;
    }
    if (match(parser, TOKEN_IDENTIFIER)) {
        Expression *expr = allocate_expression(parser, EXPR_IDENTIFIER);
        if (expr) {
            expr->as.identifier.name = copy_string(parser->previous.lexeme);
            if (!expr->as.identifier.name) {
                parser_error(parser, "Out of memory");
            }
        }
        return expr;
    }
    if (match(parser, TOKEN_LPAREN)) {
        Expression *expr = parse_expression(parser);
        if (!consume(parser, TOKEN_RPAREN, "Expect ')' after expression.")) {
            expression_free_internal(expr);
            return NULL;
        }
        return expr;
    }

    parser_error(parser, "Expect expression.");
    return NULL;
}

static Expression *parse_call(Parser *parser) {
    Expression *expr = parse_primary(parser);
    if (!expr) {
        return NULL;
    }

    for (;;) {
        if (match(parser, TOKEN_LPAREN)) {
            expr = finish_call(parser, expr);
            if (!expr) {
                return NULL;
            }
        } else {
            break;
        }
    }
    return expr;
}

static Expression *parse_unary(Parser *parser) {
    if (match(parser, TOKEN_BANG)) {
        Expression *right = parse_unary(parser);
        if (!right) {
            return NULL;
        }
        Expression *expr = allocate_expression(parser, EXPR_UNARY);
        if (!expr) {
            expression_free_internal(right);
            return NULL;
        }
        expr->as.unary.operator_type = TOKEN_BANG;
        expr->as.unary.right = right;
        return expr;
    }
    if (match(parser, TOKEN_MINUS)) {
        Expression *right = parse_unary(parser);
        if (!right) {
            return NULL;
        }
        Expression *expr = allocate_expression(parser, EXPR_UNARY);
        if (!expr) {
            expression_free_internal(right);
            return NULL;
        }
        expr->as.unary.operator_type = TOKEN_MINUS;
        expr->as.unary.right = right;
        return expr;
    }
    return parse_call(parser);
}

static Expression *parse_factor(Parser *parser) {
    Expression *expr = parse_unary(parser);
    if (!expr) {
        return NULL;
    }
    for (;;) {
        TokenType operator_type;
        bool matched = false;
        if (match(parser, TOKEN_STAR)) {
            operator_type = TOKEN_STAR;
            matched = true;
        } else if (match(parser, TOKEN_SLASH)) {
            operator_type = TOKEN_SLASH;
            matched = true;
        }
        if (!matched) {
            break;
        }
        Expression *right = parse_unary(parser);
        if (!right) {
            expression_free_internal(expr);
            return NULL;
        }
        expr = make_binary(parser, expr, operator_type, right);
        if (!expr) {
            return NULL;
        }
    }
    return expr;
}

static Expression *parse_term(Parser *parser) {
    Expression *expr = parse_factor(parser);
    if (!expr) {
        return NULL;
    }
    for (;;) {
        TokenType operator_type;
        bool matched = false;
        if (match(parser, TOKEN_PLUS)) {
            operator_type = TOKEN_PLUS;
            matched = true;
        } else if (match(parser, TOKEN_MINUS)) {
            operator_type = TOKEN_MINUS;
            matched = true;
        }
        if (!matched) {
            break;
        }
        Expression *right = parse_factor(parser);
        if (!right) {
            expression_free_internal(expr);
            return NULL;
        }
        expr = make_binary(parser, expr, operator_type, right);
        if (!expr) {
            return NULL;
        }
    }
    return expr;
}

static Expression *parse_comparison(Parser *parser) {
    Expression *expr = parse_term(parser);
    if (!expr) {
        return NULL;
    }
    for (;;) {
        TokenType operator_type;
        bool matched = false;
        if (match(parser, TOKEN_GREATER)) {
            operator_type = TOKEN_GREATER;
            matched = true;
        } else if (match(parser, TOKEN_GREATER_EQUAL)) {
            operator_type = TOKEN_GREATER_EQUAL;
            matched = true;
        } else if (match(parser, TOKEN_LESS)) {
            operator_type = TOKEN_LESS;
            matched = true;
        } else if (match(parser, TOKEN_LESS_EQUAL)) {
            operator_type = TOKEN_LESS_EQUAL;
            matched = true;
        }
        if (!matched) {
            break;
        }
        Expression *right = parse_term(parser);
        if (!right) {
            expression_free_internal(expr);
            return NULL;
        }
        expr = make_binary(parser, expr, operator_type, right);
        if (!expr) {
            return NULL;
        }
    }
    return expr;
}

static Expression *parse_equality(Parser *parser) {
    Expression *expr = parse_comparison(parser);
    if (!expr) {
        return NULL;
    }
    for (;;) {
        TokenType operator_type;
        bool matched = false;
        if (match(parser, TOKEN_EQUAL_EQUAL)) {
            operator_type = TOKEN_EQUAL_EQUAL;
            matched = true;
        } else if (match(parser, TOKEN_BANG_EQUAL)) {
            operator_type = TOKEN_BANG_EQUAL;
            matched = true;
        }
        if (!matched) {
            break;
        }
        Expression *right = parse_comparison(parser);
        if (!right) {
            expression_free_internal(expr);
            return NULL;
        }
        expr = make_binary(parser, expr, operator_type, right);
        if (!expr) {
            return NULL;
        }
    }
    return expr;
}

static Expression *parse_assignment(Parser *parser) {
    Expression *expr = parse_equality(parser);
    if (!expr) {
        return NULL;
    }
    if (match(parser, TOKEN_EQUAL)) {
        Expression *value = parse_assignment(parser);
        if (!value) {
            expression_free_internal(expr);
            return NULL;
        }
        if (expr->type != EXPR_IDENTIFIER) {
            parser_error(parser, "Invalid assignment target.");
            expression_free_internal(expr);
            expression_free_internal(value);
            return NULL;
        }
        char *name = expr->as.identifier.name;
        expr->type = EXPR_ASSIGNMENT;
        expr->as.assignment.name = name;
        expr->as.assignment.value = value;
    }
    return expr;
}

static Expression *parse_expression(Parser *parser) {
    return parse_assignment(parser);
}

static Statement *parse_expression_statement(Parser *parser) {
    Expression *expr = parse_expression(parser);
    if (!expr) {
        return NULL;
    }
    if (!consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.")) {
        expression_free_internal(expr);
        return NULL;
    }
    Statement *statement = allocate_statement(parser, STMT_EXPRESSION);
    if (!statement) {
        expression_free_internal(expr);
        return NULL;
    }
    statement->as.expression_statement.expression = expr;
    return statement;
}

static Statement *parse_let_declaration(Parser *parser) {
    const Token *name_token = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
    if (!name_token) {
        return NULL;
    }
    char *name = copy_string(name_token->lexeme);
    if (!name) {
        parser_error(parser, "Out of memory");
        return NULL;
    }

    Expression *initializer = NULL;
    bool has_initializer = false;
    if (match(parser, TOKEN_EQUAL)) {
        initializer = parse_expression(parser);
        if (!initializer) {
            free(name);
            return NULL;
        }
        has_initializer = true;
    }

    if (!consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration.")) {
        free(name);
        expression_free_internal(initializer);
        return NULL;
    }

    Statement *statement = allocate_statement(parser, STMT_LET);
    if (!statement) {
        free(name);
        expression_free_internal(initializer);
        return NULL;
    }
    statement->as.let_statement.name = name;
    statement->as.let_statement.has_initializer = has_initializer;
    statement->as.let_statement.initializer = initializer;
    return statement;
}

static Statement *parse_if_statement(Parser *parser) {
    if (!consume(parser, TOKEN_LPAREN, "Expect '(' after 'if'.")) {
        return NULL;
    }
    Expression *condition = parse_expression(parser);
    if (!condition) {
        return NULL;
    }
    if (!consume(parser, TOKEN_RPAREN, "Expect ')' after condition.")) {
        expression_free_internal(condition);
        return NULL;
    }

    Statement *then_branch = parse_statement(parser);
    if (!then_branch) {
        expression_free_internal(condition);
        return NULL;
    }

    Statement *else_branch = NULL;
    if (match(parser, TOKEN_KEYWORD_ELSE)) {
        else_branch = parse_statement(parser);
        if (!else_branch) {
            expression_free_internal(condition);
            statement_free_internal(then_branch);
            return NULL;
        }
    }

    Statement *statement = allocate_statement(parser, STMT_IF);
    if (!statement) {
        expression_free_internal(condition);
        statement_free_internal(then_branch);
        statement_free_internal(else_branch);
        return NULL;
    }
    statement->as.if_statement.condition = condition;
    statement->as.if_statement.then_branch = then_branch;
    statement->as.if_statement.else_branch = else_branch;
    return statement;
}

static Statement *parse_while_statement(Parser *parser) {
    if (!consume(parser, TOKEN_LPAREN, "Expect '(' after 'while'.")) {
        return NULL;
    }
    Expression *condition = parse_expression(parser);
    if (!condition) {
        return NULL;
    }
    if (!consume(parser, TOKEN_RPAREN, "Expect ')' after condition.")) {
        expression_free_internal(condition);
        return NULL;
    }
    Statement *body = parse_statement(parser);
    if (!body) {
        expression_free_internal(condition);
        return NULL;
    }

    Statement *statement = allocate_statement(parser, STMT_WHILE);
    if (!statement) {
        expression_free_internal(condition);
        statement_free_internal(body);
        return NULL;
    }
    statement->as.while_statement.condition = condition;
    statement->as.while_statement.body = body;
    return statement;
}

static Statement *parse_return_statement(Parser *parser) {
    Expression *value = NULL;
    bool has_value = false;
    if (!check(parser, TOKEN_SEMICOLON)) {
        value = parse_expression(parser);
        if (!value) {
            return NULL;
        }
        has_value = true;
    }
    if (!consume(parser, TOKEN_SEMICOLON, "Expect ';' after return statement.")) {
        expression_free_internal(value);
        return NULL;
    }

    Statement *statement = allocate_statement(parser, STMT_RETURN);
    if (!statement) {
        expression_free_internal(value);
        return NULL;
    }
    statement->as.return_statement.has_value = has_value;
    statement->as.return_statement.value = value;
    return statement;
}

static Statement *parse_block(Parser *parser) {
    Statement *block = allocate_statement(parser, STMT_BLOCK);
    if (!block) {
        return NULL;
    }
    block->as.block_statement.statements.items = NULL;
    block->as.block_statement.statements.count = 0;
    block->as.block_statement.statements.capacity = 0;

    while (!check(parser, TOKEN_RBRACE) && parser->current.type != TOKEN_EOF) {
        Statement *decl = parse_declaration(parser);
        if (!decl) {
            synchronize(parser);
            if (check(parser, TOKEN_RBRACE)) {
                break;
            }
            continue;
        }
        if (!statement_list_append(parser, &block->as.block_statement.statements, decl)) {
            statement_free_internal(decl);
            statement_free_internal(block);
            return NULL;
        }
    }

    if (!consume(parser, TOKEN_RBRACE, "Expect '}' after block.")) {
        statement_free_internal(block);
        return NULL;
    }

    return block;
}

static Statement *parse_function_declaration(Parser *parser) {
    const Token *name_token = consume(parser, TOKEN_IDENTIFIER, "Expect function name.");
    if (!name_token) {
        return NULL;
    }
    char *name = copy_string(name_token->lexeme);
    if (!name) {
        parser_error(parser, "Out of memory");
        return NULL;
    }

    if (!consume(parser, TOKEN_LPAREN, "Expect '(' after function name.")) {
        free(name);
        return NULL;
    }

    size_t parameter_capacity = 0;
    size_t parameter_count = 0;
    char **parameters = NULL;

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            const Token *param_token = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
            if (!param_token) {
                break;
            }
            if (parameter_count == parameter_capacity) {
                size_t new_capacity = parameter_capacity == 0 ? 4 : parameter_capacity * 2;
                char **new_params = (char **)realloc(parameters, new_capacity * sizeof(char *));
                if (!new_params) {
                    parser_error(parser, "Out of memory");
                    break;
                }
                parameters = new_params;
                parameter_capacity = new_capacity;
            }
            if (parser->had_error) {
                break;
            }
            char *param_name = copy_string(param_token->lexeme);
            if (!param_name) {
                parser_error(parser, "Out of memory");
                break;
            }
            parameters[parameter_count++] = param_name;
        } while (match(parser, TOKEN_COMMA));
    }

    if (parser->had_error) {
        for (size_t i = 0; i < parameter_count; ++i) {
            free(parameters[i]);
        }
        free(parameters);
        free(name);
        return NULL;
    }

    if (!consume(parser, TOKEN_RPAREN, "Expect ')' after parameters.")) {
        for (size_t i = 0; i < parameter_count; ++i) {
            free(parameters[i]);
        }
        free(parameters);
        free(name);
        return NULL;
    }

    if (!consume(parser, TOKEN_LBRACE, "Expect '{' before function body.")) {
        for (size_t i = 0; i < parameter_count; ++i) {
            free(parameters[i]);
        }
        free(parameters);
        free(name);
        return NULL;
    }

    Statement *body = parse_block(parser);
    if (!body) {
        for (size_t i = 0; i < parameter_count; ++i) {
            free(parameters[i]);
        }
        free(parameters);
        free(name);
        return NULL;
    }

    Statement *statement = allocate_statement(parser, STMT_FUNCTION);
    if (!statement) {
        statement_free_internal(body);
        for (size_t i = 0; i < parameter_count; ++i) {
            free(parameters[i]);
        }
        free(parameters);
        free(name);
        return NULL;
    }
    statement->as.function_statement.name = name;
    statement->as.function_statement.parameters = parameters;
    statement->as.function_statement.parameter_count = parameter_count;
    statement->as.function_statement.parameter_capacity = parameter_capacity;
    statement->as.function_statement.body = body;
    return statement;
}

static Statement *parse_statement(Parser *parser) {
    if (match(parser, TOKEN_KEYWORD_IF)) {
        return parse_if_statement(parser);
    }
    if (match(parser, TOKEN_KEYWORD_WHILE)) {
        return parse_while_statement(parser);
    }
    if (match(parser, TOKEN_KEYWORD_RETURN)) {
        return parse_return_statement(parser);
    }
    if (match(parser, TOKEN_LBRACE)) {
        return parse_block(parser);
    }
    return parse_expression_statement(parser);
}

static Statement *parse_declaration(Parser *parser) {
    if (match(parser, TOKEN_KEYWORD_FUNCTION)) {
        return parse_function_declaration(parser);
    }
    if (match(parser, TOKEN_KEYWORD_LET)) {
        return parse_let_declaration(parser);
    }
    return parse_statement(parser);
}

Program *parser_parse(const char *source, char **error_message) {
    Parser parser;
    parser_init(&parser, source);

    Program *program = (Program *)calloc(1, sizeof(Program));
    if (!program) {
        if (error_message) {
            *error_message = copy_string("Out of memory");
        }
        token_dispose(&parser.current);
        token_dispose(&parser.previous);
        free(parser.error_message);
        return NULL;
    }

    while (parser.current.type != TOKEN_EOF && !parser.had_error) {
        Statement *decl = parse_declaration(&parser);
        if (!decl) {
            synchronize(&parser);
            continue;
        }
        if (!statement_list_append(&parser, &program->statements, decl)) {
            statement_free_internal(decl);
            break;
        }
    }

    if (parser.had_error) {
        if (error_message) {
            *error_message = copy_string(parser.error_message ? parser.error_message : "Parse error");
        }
        program_free(program);
        program = NULL;
    } else if (error_message) {
        *error_message = NULL;
    }

    token_dispose(&parser.current);
    token_dispose(&parser.previous);
    free(parser.error_message);

    return program;
}

static void expression_list_free(ExpressionList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        expression_free_internal(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void statement_list_free(StatementList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        statement_free_internal(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void expression_free_internal(Expression *expression) {
    if (!expression) {
        return;
    }
    switch (expression->type) {
        case EXPR_LITERAL_NUMBER:
        case EXPR_LITERAL_NULL:
            break;
        case EXPR_LITERAL_STRING:
            free(expression->as.string_literal.value);
            break;
        case EXPR_LITERAL_BOOL:
            break;
        case EXPR_IDENTIFIER:
            free(expression->as.identifier.name);
            break;
        case EXPR_UNARY:
            expression_free_internal(expression->as.unary.right);
            break;
        case EXPR_BINARY:
            expression_free_internal(expression->as.binary.left);
            expression_free_internal(expression->as.binary.right);
            break;
        case EXPR_ASSIGNMENT:
            free(expression->as.assignment.name);
            expression_free_internal(expression->as.assignment.value);
            break;
        case EXPR_CALL:
            expression_free_internal(expression->as.call.callee);
            expression_list_free(&expression->as.call.arguments);
            break;
    }
    free(expression);
}

static void statement_free_internal(Statement *statement) {
    if (!statement) {
        return;
    }
    switch (statement->type) {
        case STMT_LET:
            free(statement->as.let_statement.name);
            if (statement->as.let_statement.has_initializer) {
                expression_free_internal(statement->as.let_statement.initializer);
            }
            break;
        case STMT_EXPRESSION:
            expression_free_internal(statement->as.expression_statement.expression);
            break;
        case STMT_IF:
            expression_free_internal(statement->as.if_statement.condition);
            statement_free_internal(statement->as.if_statement.then_branch);
            statement_free_internal(statement->as.if_statement.else_branch);
            break;
        case STMT_WHILE:
            expression_free_internal(statement->as.while_statement.condition);
            statement_free_internal(statement->as.while_statement.body);
            break;
        case STMT_BLOCK:
            statement_list_free(&statement->as.block_statement.statements);
            break;
        case STMT_FUNCTION:
            free(statement->as.function_statement.name);
            for (size_t i = 0; i < statement->as.function_statement.parameter_count; ++i) {
                free(statement->as.function_statement.parameters[i]);
            }
            free(statement->as.function_statement.parameters);
            statement_free_internal(statement->as.function_statement.body);
            break;
        case STMT_RETURN:
            if (statement->as.return_statement.has_value) {
                expression_free_internal(statement->as.return_statement.value);
            }
            break;
    }
    free(statement);
}

void program_free(Program *program) {
    if (!program) {
        return;
    }
    statement_list_free(&program->statements);
    free(program);
}

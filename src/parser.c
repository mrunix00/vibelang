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
            case TOKEN_KEYWORD_CLASS:
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
static bool parse_argument_list(Parser *parser, ExpressionList *list);
static Expression *parse_array_literal(Parser *parser);
static Expression *parse_primary(Parser *parser);
static Statement *parse_declaration(Parser *parser);
static Statement *parse_statement(Parser *parser);
static Statement *parse_let_declaration(Parser *parser);
static Statement *parse_function_declaration(Parser *parser);
static Statement *parse_class_declaration(Parser *parser);
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

static bool parse_argument_list(Parser *parser, ExpressionList *list) {
    if (!list) {
        return false;
    }
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            Expression *argument = parse_expression(parser);
            if (!argument) {
                expression_list_free(list);
                return false;
            }
            if (!expression_list_append(parser, list, argument)) {
                expression_free_internal(argument);
                expression_list_free(list);
                return false;
            }
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "Expect ')' after arguments.")) {
        expression_list_free(list);
        return false;
    }
    return true;
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

    if (!parse_argument_list(parser, &call->as.call.arguments)) {
        expression_free_internal(call);
        return NULL;
    }
    return call;
}

static Expression *parse_array_literal(Parser *parser) {
    Expression *array_expr = allocate_expression(parser, EXPR_ARRAY);
    if (!array_expr) {
        return NULL;
    }
    array_expr->as.array_literal.elements.items = NULL;
    array_expr->as.array_literal.elements.count = 0;
    array_expr->as.array_literal.elements.capacity = 0;

    if (!check(parser, TOKEN_RBRACKET)) {
        do {
            Expression *element = parse_expression(parser);
            if (!element) {
                expression_free_internal(array_expr);
                return NULL;
            }
            if (!expression_list_append(parser, &array_expr->as.array_literal.elements, element)) {
                expression_free_internal(array_expr);
                return NULL;
            }
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RBRACKET, "Expect ']' after array literal.")) {
        expression_free_internal(array_expr);
        return NULL;
    }

    return array_expr;
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
    if (match(parser, TOKEN_KEYWORD_THIS)) {
        Expression *expr = allocate_expression(parser, EXPR_THIS);
        return expr;
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
    if (match(parser, TOKEN_LBRACKET)) {
        return parse_array_literal(parser);
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
        } else if (match(parser, TOKEN_LBRACKET)) {
            Expression *index = parse_expression(parser);
            if (!index) {
                expression_free_internal(expr);
                return NULL;
            }
            if (!consume(parser, TOKEN_RBRACKET, "Expect ']' after index.")) {
                expression_free_internal(expr);
                expression_free_internal(index);
                return NULL;
            }
            Expression *index_expr = allocate_expression(parser, EXPR_INDEX);
            if (!index_expr) {
                expression_free_internal(expr);
                expression_free_internal(index);
                return NULL;
            }
            index_expr->as.index.array = expr;
            index_expr->as.index.index = index;
            expr = index_expr;
        } else if (match(parser, TOKEN_DOT)) {
            const Token *name_token = consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'.");
            if (!name_token) {
                expression_free_internal(expr);
                return NULL;
            }
            char *name = copy_string(name_token->lexeme);
            if (!name) {
                parser_error(parser, "Out of memory");
                expression_free_internal(expr);
                return NULL;
            }
            if (match(parser, TOKEN_LPAREN)) {
                Expression *invoke = allocate_expression(parser, EXPR_INVOKE);
                if (!invoke) {
                    free(name);
                    expression_free_internal(expr);
                    return NULL;
                }
                invoke->as.invoke.object = expr;
                invoke->as.invoke.name = name;
                invoke->as.invoke.arguments.items = NULL;
                invoke->as.invoke.arguments.count = 0;
                invoke->as.invoke.arguments.capacity = 0;
                if (!parse_argument_list(parser, &invoke->as.invoke.arguments)) {
                    expression_free_internal(invoke);
                    return NULL;
                }
                expr = invoke;
            } else {
                Expression *get = allocate_expression(parser, EXPR_GET_PROPERTY);
                if (!get) {
                    free(name);
                    expression_free_internal(expr);
                    return NULL;
                }
                get->as.get_property.object = expr;
                get->as.get_property.name = name;
                expr = get;
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
    TokenType assignment_type = TOKEN_ERROR;
    if (match(parser, TOKEN_EQUAL)) {
        assignment_type = TOKEN_EQUAL;
    } else if (match(parser, TOKEN_PLUS_EQUAL)) {
        assignment_type = TOKEN_PLUS_EQUAL;
    }

    if (assignment_type != TOKEN_ERROR) {
        Expression *value = parse_assignment(parser);
        if (!value) {
            expression_free_internal(expr);
            return NULL;
        }
        if (expr->type == EXPR_IDENTIFIER) {
            if (assignment_type == TOKEN_PLUS_EQUAL) {
                Expression *identifier_copy = allocate_expression(parser, EXPR_IDENTIFIER);
                if (!identifier_copy) {
                    expression_free_internal(expr);
                    expression_free_internal(value);
                    return NULL;
                }
                identifier_copy->as.identifier.name = copy_string(expr->as.identifier.name);
                if (!identifier_copy->as.identifier.name) {
                    parser_error(parser, "Out of memory");
                    expression_free_internal(identifier_copy);
                    expression_free_internal(expr);
                    expression_free_internal(value);
                    return NULL;
                }
                Expression *binary = allocate_expression(parser, EXPR_BINARY);
                if (!binary) {
                    expression_free_internal(identifier_copy);
                    expression_free_internal(expr);
                    expression_free_internal(value);
                    return NULL;
                }
                binary->as.binary.left = identifier_copy;
                binary->as.binary.operator_type = TOKEN_PLUS;
                binary->as.binary.right = value;
                value = binary;
            }
            char *name = expr->as.identifier.name;
            expr->type = EXPR_ASSIGNMENT;
            expr->as.assignment.name = name;
            expr->as.assignment.value = value;
        } else if (expr->type == EXPR_GET_PROPERTY && assignment_type == TOKEN_EQUAL) {
            Expression *object = expr->as.get_property.object;
            char *name = expr->as.get_property.name;
            expr->type = EXPR_SET_PROPERTY;
            expr->as.set_property.object = object;
            expr->as.set_property.name = name;
            expr->as.set_property.value = value;
        } else {
            parser_error(parser, "Invalid assignment target.");
            expression_free_internal(expr);
            expression_free_internal(value);
            return NULL;
        }
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

static Statement *parse_class_declaration(Parser *parser) {
    const Token *name_token = consume(parser, TOKEN_IDENTIFIER, "Expect class name.");
    if (!name_token) {
        return NULL;
    }
    char *name = copy_string(name_token->lexeme);
    if (!name) {
        parser_error(parser, "Out of memory");
        return NULL;
    }

    if (!consume(parser, TOKEN_LBRACE, "Expect '{' before class body.")) {
        free(name);
        return NULL;
    }

    Statement *statement = allocate_statement(parser, STMT_CLASS);
    if (!statement) {
        free(name);
        return NULL;
    }
    statement->as.class_statement.name = name;
    statement->as.class_statement.methods = NULL;
    statement->as.class_statement.method_count = 0;
    statement->as.class_statement.method_capacity = 0;

    while (!check(parser, TOKEN_RBRACE) && parser->current.type != TOKEN_EOF) {
        ClassMethod method;
        method.name = NULL;
        method.is_constructor = false;
        method.parameters = NULL;
        method.parameter_count = 0;
        method.parameter_capacity = 0;
        method.body = NULL;

        if (match(parser, TOKEN_KEYWORD_CONSTRUCTOR)) {
            method.is_constructor = true;
            method.name = copy_string("constructor");
            if (!method.name) {
                parser_error(parser, "Out of memory");
                goto class_method_fail;
            }
        } else {
            const Token *method_name = consume(parser, TOKEN_IDENTIFIER, "Expect method name.");
            if (!method_name) {
                goto class_method_fail;
            }
            method.name = copy_string(method_name->lexeme);
            if (!method.name) {
                parser_error(parser, "Out of memory");
                goto class_method_fail;
            }
        }

        if (!consume(parser, TOKEN_LPAREN, "Expect '(' after method name.")) {
            goto class_method_fail;
        }

        if (!check(parser, TOKEN_RPAREN)) {
            do {
                const Token *param_token = consume(parser, TOKEN_IDENTIFIER, "Expect parameter name.");
                if (!param_token) {
                    goto class_method_fail;
                }
                if (method.parameter_count == method.parameter_capacity) {
                    size_t new_capacity = method.parameter_capacity == 0 ? 4 : method.parameter_capacity * 2;
                    char **new_params = (char **)realloc(method.parameters, new_capacity * sizeof(char *));
                    if (!new_params) {
                        parser_error(parser, "Out of memory");
                        goto class_method_fail;
                    }
                    method.parameters = new_params;
                    method.parameter_capacity = new_capacity;
                }
                char *param_name = copy_string(param_token->lexeme);
                if (!param_name) {
                    parser_error(parser, "Out of memory");
                    goto class_method_fail;
                }
                method.parameters[method.parameter_count++] = param_name;
            } while (match(parser, TOKEN_COMMA));
        }

        if (!consume(parser, TOKEN_RPAREN, "Expect ')' after parameters.")) {
            goto class_method_fail;
        }

        if (!consume(parser, TOKEN_LBRACE, "Expect '{' before method body.")) {
            goto class_method_fail;
        }

        method.body = parse_block(parser);
        if (!method.body) {
            goto class_method_fail;
        }

        if (statement->as.class_statement.method_count == statement->as.class_statement.method_capacity) {
            size_t new_capacity = statement->as.class_statement.method_capacity == 0 ? 4 : statement->as.class_statement.method_capacity * 2;
            ClassMethod *new_methods = (ClassMethod *)realloc(statement->as.class_statement.methods, new_capacity * sizeof(ClassMethod));
            if (!new_methods) {
                parser_error(parser, "Out of memory");
                goto class_method_fail;
            }
            statement->as.class_statement.methods = new_methods;
            statement->as.class_statement.method_capacity = new_capacity;
        }
        statement->as.class_statement.methods[statement->as.class_statement.method_count++] = method;
        continue;

class_method_fail:
        if (method.name) {
            free(method.name);
        }
        for (size_t i = 0; i < method.parameter_count; ++i) {
            free(method.parameters[i]);
        }
        free(method.parameters);
        if (method.body) {
            statement_free_internal(method.body);
        }
        statement_free_internal(statement);
        synchronize(parser);
        return NULL;
    }

    if (!consume(parser, TOKEN_RBRACE, "Expect '}' after class body.")) {
        statement_free_internal(statement);
        return NULL;
    }

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
    if (match(parser, TOKEN_KEYWORD_CLASS)) {
        return parse_class_declaration(parser);
    }
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
        case EXPR_ARRAY:
            expression_list_free(&expression->as.array_literal.elements);
            break;
        case EXPR_INDEX:
            expression_free_internal(expression->as.index.array);
            expression_free_internal(expression->as.index.index);
            break;
        case EXPR_THIS:
            break;
        case EXPR_GET_PROPERTY:
            expression_free_internal(expression->as.get_property.object);
            free(expression->as.get_property.name);
            break;
        case EXPR_SET_PROPERTY:
            expression_free_internal(expression->as.set_property.object);
            free(expression->as.set_property.name);
            expression_free_internal(expression->as.set_property.value);
            break;
        case EXPR_INVOKE:
            expression_free_internal(expression->as.invoke.object);
            free(expression->as.invoke.name);
            expression_list_free(&expression->as.invoke.arguments);
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
        case STMT_CLASS:
            free(statement->as.class_statement.name);
            for (size_t i = 0; i < statement->as.class_statement.method_count; ++i) {
                ClassMethod *method = &statement->as.class_statement.methods[i];
                free(method->name);
                for (size_t j = 0; j < method->parameter_count; ++j) {
                    free(method->parameters[j]);
                }
                free(method->parameters);
                statement_free_internal(method->body);
            }
            free(statement->as.class_statement.methods);
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

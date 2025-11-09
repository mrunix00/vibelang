#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_at_end(const Lexer *lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer *lexer) {
    char c = *lexer->current;
    if (!is_at_end(lexer)) {
        lexer->current++;
    }
    return c;
}

static char peek(const Lexer *lexer) {
    return *lexer->current;
}

static char peek_next(const Lexer *lexer) {
    if (is_at_end(lexer)) {
        return '\0';
    }
    return lexer->current[1];
}

static void skip_whitespace(Lexer *lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
            case '\n':
                advance(lexer);
                break;
            case '/':
                if (peek_next(lexer) == '/') {
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static char *duplicate_lexeme(const char *start, size_t length) {
    char *copy = (char *)malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    if (length > 0) {
        memcpy(copy, start, length);
    }
    copy[length] = '\0';
    return copy;
}

static Token make_token_from_range(TokenType type, const char *start, size_t length) {
    Token token;
    token.type = type;
    token.lexeme = duplicate_lexeme(start, length);
    token.number_value = 0.0;
    if (!token.lexeme) {
        token.type = TOKEN_ERROR;
        token.lexeme = duplicate_lexeme("Out of memory", strlen("Out of memory"));
    }
    return token;
}

static Token make_token(const Lexer *lexer, TokenType type) {
    size_t length = (size_t)(lexer->current - lexer->start);
    return make_token_from_range(type, lexer->start, length);
}

static Token make_error_token(const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.lexeme = duplicate_lexeme(message, strlen(message));
    token.number_value = 0.0;
    return token;
}

static bool match(Lexer *lexer, char expected) {
    if (is_at_end(lexer) || *lexer->current != expected) {
        return false;
    }
    lexer->current++;
    return true;
}

static Token string(Lexer *lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') {
            break;
        }
        advance(lexer);
    }

    if (is_at_end(lexer) || peek(lexer) != '"') {
        return make_error_token("Unterminated string literal");
    }

    advance(lexer); // Consume closing quote.

    size_t literal_length = (size_t)(lexer->current - lexer->start - 2);
    const char *literal_start = lexer->start + 1;
    return make_token_from_range(TOKEN_STRING, literal_start, literal_length);
}

static Token number(Lexer *lexer) {
    while (isdigit(peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
        advance(lexer);
        while (isdigit(peek(lexer))) {
            advance(lexer);
        }
    }

    Token token = make_token(lexer, TOKEN_NUMBER);
    if (token.type != TOKEN_ERROR && token.lexeme) {
        token.number_value = strtod(token.lexeme, NULL);
    }
    return token;
}

static TokenType identifier_type(const char *start, size_t length) {
    switch (start[0]) {
        case 'e':
            if (length == 4 && strncmp(start, "else", length) == 0) {
                return TOKEN_KEYWORD_ELSE;
            }
            break;
        case 'f':
            if (length == 5 && strncmp(start, "false", length) == 0) {
                return TOKEN_KEYWORD_FALSE;
            }
            if (length == 8 && strncmp(start, "function", length) == 0) {
                return TOKEN_KEYWORD_FUNCTION;
            }
            break;
        case 'i':
            if (length == 2 && strncmp(start, "if", length) == 0) {
                return TOKEN_KEYWORD_IF;
            }
            break;
        case 'l':
            if (length == 3 && strncmp(start, "let", length) == 0) {
                return TOKEN_KEYWORD_LET;
            }
            break;
        case 'n':
            if (length == 4 && strncmp(start, "null", length) == 0) {
                return TOKEN_KEYWORD_NULL;
            }
            break;
        case 'r':
            if (length == 6 && strncmp(start, "return", length) == 0) {
                return TOKEN_KEYWORD_RETURN;
            }
            break;
        case 't':
            if (length == 4 && strncmp(start, "true", length) == 0) {
                return TOKEN_KEYWORD_TRUE;
            }
            break;
        case 'w':
            if (length == 5 && strncmp(start, "while", length) == 0) {
                return TOKEN_KEYWORD_WHILE;
            }
            break;
        default:
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier(Lexer *lexer) {
    while (isalnum(peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }

    Token token = make_token(lexer, TOKEN_IDENTIFIER);
    if (token.type == TOKEN_ERROR) {
        return token;
    }

    size_t length = strlen(token.lexeme);
    TokenType type = identifier_type(token.lexeme, length);
    if (type == TOKEN_IDENTIFIER) {
        return token;
    }

    Token keyword_token = make_token_from_range(type, token.lexeme, length);
    token_free(&token);
    return keyword_token;
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->start = source;
    lexer->current = source;
}

Token lexer_next_token(Lexer *lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;

    if (is_at_end(lexer)) {
        return make_token_from_range(TOKEN_EOF, lexer->current, 0);
    }

    char c = advance(lexer);

    if (isalpha(c) || c == '_') {
        return identifier(lexer);
    }

    if (isdigit(c)) {
        return number(lexer);
    }

    switch (c) {
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case '{': return make_token(lexer, TOKEN_LBRACE);
        case '}': return make_token(lexer, TOKEN_RBRACE);
        case '[': return make_token(lexer, TOKEN_LBRACKET);
        case ']': return make_token(lexer, TOKEN_RBRACKET);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case ';': return make_token(lexer, TOKEN_SEMICOLON);
        case '+':
            return make_token(lexer, match(lexer, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '-': return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_STAR);
        case '/': return make_token(lexer, TOKEN_SLASH);
        case '!':
            return make_token(lexer, match(lexer, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return make_token(lexer, match(lexer, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '>':
            return make_token(lexer, match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '<':
            return make_token(lexer, match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '"':
            return string(lexer);
        default:
            break;
    }

    return make_error_token("Unexpected character");
}

void token_free(Token *token) {
    if (token && token->lexeme) {
        free(token->lexeme);
        token->lexeme = NULL;
    }
}

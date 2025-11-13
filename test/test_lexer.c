#include "../libs/Unity/src/unity.h"
#include "../src/lexer.h"

#include <math.h>
#include <string.h>

void setUp(void) {
    // Set up code here (runs before each test)
}

void tearDown(void) {
    // Tear down code here (runs after each test)
}

static void expect_simple_token(Lexer *lexer, TokenType expected_type, const char *expected_lexeme) {
    Token token = lexer_next_token(lexer);
    TEST_ASSERT_EQUAL_INT(expected_type, token.type);
    if (expected_lexeme != NULL) {
        TEST_ASSERT_NOT_NULL(token.lexeme);
        TEST_ASSERT_EQUAL_STRING(expected_lexeme, token.lexeme);
    }
    token_free(&token);
}

static void expect_number_token(Lexer *lexer, double expected_value, const char *expected_lexeme) {
    Token token = lexer_next_token(lexer);
    TEST_ASSERT_EQUAL_INT(TOKEN_NUMBER, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    if (expected_lexeme != NULL) {
        TEST_ASSERT_EQUAL_STRING(expected_lexeme, token.lexeme);
    }
    double diff = fabs(token.number_value - expected_value);
    TEST_ASSERT_TRUE_MESSAGE(diff < 1e-9, "Number literal mismatch");
    token_free(&token);
}

static void expect_string_token(Lexer *lexer, const char *expected_value) {
    Token token = lexer_next_token(lexer);
    TEST_ASSERT_EQUAL_INT(TOKEN_STRING, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    TEST_ASSERT_EQUAL_STRING(expected_value, token.lexeme);
    token_free(&token);
}

static void expect_eof(Lexer *lexer) {
    Token token = lexer_next_token(lexer);
    TEST_ASSERT_EQUAL_INT(TOKEN_EOF, token.type);
    TEST_ASSERT_NOT_NULL(token.lexeme);
    TEST_ASSERT_EQUAL_UINT(0U, (unsigned int)strlen(token.lexeme));
    token_free(&token);
}

void test_lex_simple_declaration(void) {
    const char *source = "let x = 5; // comment";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "x");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_number_token(&lexer, 5.0, "5");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_eof(&lexer);
}

void test_lex_string_literal(void) {
    const char *source = "let greeting = \"Hello World\";";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "greeting");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_string_token(&lexer, "Hello World");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_eof(&lexer);
}

void test_lex_control_flow(void) {
    const char *source =
        "if (x > 0) {\n"
        "  print(\"x is positive\");\n"
        "} else {\n"
        "  print(\"not positive\");\n"
        "}";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_IF, "if");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "x");
    expect_simple_token(&lexer, TOKEN_GREATER, ">");
    expect_number_token(&lexer, 0.0, "0");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "print");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_string_token(&lexer, "x is positive");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");
    expect_simple_token(&lexer, TOKEN_KEYWORD_ELSE, "else");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "print");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_string_token(&lexer, "not positive");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");
    expect_eof(&lexer);
}

void test_lex_function_declaration(void) {
    const char *source =
        "function add(a, b) {\n"
        "  return a + b;\n"
        "}";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_FUNCTION, "function");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "add");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "a");
    expect_simple_token(&lexer, TOKEN_COMMA, ",");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "b");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_KEYWORD_RETURN, "return");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "a");
    expect_simple_token(&lexer, TOKEN_PLUS, "+");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "b");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");
    expect_eof(&lexer);
}

void test_lex_boolean_and_null_literals(void) {
    const char *source =
        "let flag = true;\n"
        "let other = false;\n"
        "let nothing = null;";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "flag");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_KEYWORD_TRUE, "true");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "other");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_KEYWORD_FALSE, "false");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "nothing");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_KEYWORD_NULL, "null");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_eof(&lexer);
}

void test_lex_array_and_plus_equal(void) {
    const char *source =
        "let list = [1, 2, 3];\n"
        "list += 4;";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "list");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_LBRACKET, "[");
    expect_number_token(&lexer, 1.0, "1");
    expect_simple_token(&lexer, TOKEN_COMMA, ",");
    expect_number_token(&lexer, 2.0, "2");
    expect_simple_token(&lexer, TOKEN_COMMA, ",");
    expect_number_token(&lexer, 3.0, "3");
    expect_simple_token(&lexer, TOKEN_RBRACKET, "]");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "list");
    expect_simple_token(&lexer, TOKEN_PLUS_EQUAL, "+=");
    expect_number_token(&lexer, 4.0, "4");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_eof(&lexer);
}

void test_lex_class_and_member_access(void) {
    const char *source =
        "class Player {\n"
        "  constructor(start) {\n"
        "    this.value = start;\n"
        "  }\n"
        "  tick(step) {\n"
        "    this.value = this.value + step;\n"
        "  }\n"
        "}\n"
        "let p = Player(0);\n"
        "p.tick(1);\n"
        "p.value;\n";

    Lexer lexer;
    lexer_init(&lexer, source);

    expect_simple_token(&lexer, TOKEN_KEYWORD_CLASS, "class");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "Player");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_KEYWORD_CONSTRUCTOR, "constructor");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "start");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_KEYWORD_THIS, "this");
    expect_simple_token(&lexer, TOKEN_DOT, ".");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "value");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "start");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "tick");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "step");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_LBRACE, "{");
    expect_simple_token(&lexer, TOKEN_KEYWORD_THIS, "this");
    expect_simple_token(&lexer, TOKEN_DOT, ".");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "value");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_KEYWORD_THIS, "this");
    expect_simple_token(&lexer, TOKEN_DOT, ".");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "value");
    expect_simple_token(&lexer, TOKEN_PLUS, "+");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "step");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");
    expect_simple_token(&lexer, TOKEN_RBRACE, "}");

    expect_simple_token(&lexer, TOKEN_KEYWORD_LET, "let");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "p");
    expect_simple_token(&lexer, TOKEN_EQUAL, "=");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "Player");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_number_token(&lexer, 0.0, "0");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "p");
    expect_simple_token(&lexer, TOKEN_DOT, ".");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "tick");
    expect_simple_token(&lexer, TOKEN_LPAREN, "(");
    expect_number_token(&lexer, 1.0, "1");
    expect_simple_token(&lexer, TOKEN_RPAREN, ")");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");

    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "p");
    expect_simple_token(&lexer, TOKEN_DOT, ".");
    expect_simple_token(&lexer, TOKEN_IDENTIFIER, "value");
    expect_simple_token(&lexer, TOKEN_SEMICOLON, ";");
    expect_eof(&lexer);
}



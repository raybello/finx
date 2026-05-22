#pragma once
#include <string>
#include <vector>

enum class TokenType {
    NUMBER, IDENT,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    LPAREN, RPAREN, COMMA,
    END, ERROR
};

struct Token {
    TokenType   type = TokenType::ERROR;
    double      num  = 0.0;
    std::string text;
    int         col  = 0;
};

std::vector<Token> lex_expr(const std::string& input);

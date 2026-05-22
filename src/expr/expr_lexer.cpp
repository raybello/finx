#include "expr/expr_lexer.h"
#include <cctype>
#include <cstdlib>

std::vector<Token> lex_expr(const std::string& input) {
    std::vector<Token> tokens;
    int i = 0;
    int n = static_cast<int>(input.size());

    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(input[i]))) {
            ++i;
            continue;
        }

        Token tok;
        tok.col = i;
        char c = input[i];

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            char* end = nullptr;
            tok.num  = std::strtod(input.c_str() + i, &end);
            tok.type = TokenType::NUMBER;
            i        = static_cast<int>(end - input.c_str());
            tokens.push_back(tok);
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            int start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(input[i])) ||
                              input[i] == '_')) {
                ++i;
            }
            tok.type = TokenType::IDENT;
            tok.text = input.substr(start, i - start);
            tokens.push_back(tok);
            continue;
        }

        ++i;
        switch (c) {
            case '+': tok.type = TokenType::PLUS;    break;
            case '-': tok.type = TokenType::MINUS;   break;
            case '*': tok.type = TokenType::STAR;    break;
            case '/': tok.type = TokenType::SLASH;   break;
            case '%': tok.type = TokenType::PERCENT; break;
            case '(': tok.type = TokenType::LPAREN;  break;
            case ')': tok.type = TokenType::RPAREN;  break;
            case ',': tok.type = TokenType::COMMA;   break;
            default:
                tok.type = TokenType::ERROR;
                tok.text = std::string(1, c);
                break;
        }
        tokens.push_back(tok);
    }

    Token end_tok;
    end_tok.type = TokenType::END;
    end_tok.col  = n;
    tokens.push_back(end_tok);

    return tokens;
}

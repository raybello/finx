#include "expr/expr_parser.h"
#include "expr/expr_lexer.h"
#include <cstdio>

namespace {

struct Parser {
    const std::vector<Token>& toks;
    size_t pos = 0;

    const Token& peek() const { return toks[pos]; }

    Token consume() {
        Token t = toks[pos];
        if (toks[pos].type != TokenType::END) ++pos;
        return t;
    }

    bool at_end() const { return toks[pos].type == TokenType::END; }

    ParseResult make_err(const char* msg) {
        ParseResult r;
        r.ok    = false;
        r.error = msg;
        r.col   = toks[pos].col;
        return r;
    }

    ParseResult make_err(const std::string& msg) {
        ParseResult r;
        r.ok    = false;
        r.error = msg;
        r.col   = toks[pos].col;
        return r;
    }

    // expr := add_expr
    ParseResult parse_full() {
        auto res = parse_add();
        if (!res.ok) return res;
        if (!at_end()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "unexpected token at column %d", toks[pos].col);
            return make_err(buf);
        }
        return res;
    }

    // add_expr := mul_expr ( ('+' | '-') mul_expr )*
    ParseResult parse_add() {
        auto lhs = parse_mul();
        if (!lhs.ok) return lhs;

        while (peek().type == TokenType::PLUS ||
               peek().type == TokenType::MINUS) {
            Token op  = consume();
            auto  rhs = parse_mul();
            if (!rhs.ok) return rhs;

            ExprNode node;
            node.type = ExprNodeType::BINARY;
            node.name = (op.type == TokenType::PLUS) ? "+" : "-";
            node.children.push_back(std::move(lhs.root));
            node.children.push_back(std::move(rhs.root));
            lhs.root = std::move(node);
        }
        return lhs;
    }

    // mul_expr := unary_expr ( ('*' | '/' | '%') unary_expr )*
    ParseResult parse_mul() {
        auto lhs = parse_unary();
        if (!lhs.ok) return lhs;

        while (peek().type == TokenType::STAR   ||
               peek().type == TokenType::SLASH  ||
               peek().type == TokenType::PERCENT) {
            Token op  = consume();
            auto  rhs = parse_unary();
            if (!rhs.ok) return rhs;

            ExprNode node;
            node.type = ExprNodeType::BINARY;
            if      (op.type == TokenType::STAR)    node.name = "*";
            else if (op.type == TokenType::SLASH)   node.name = "/";
            else                                     node.name = "%";
            node.children.push_back(std::move(lhs.root));
            node.children.push_back(std::move(rhs.root));
            lhs.root = std::move(node);
        }
        return lhs;
    }

    // unary_expr := ('-' | '+') unary_expr | primary
    ParseResult parse_unary() {
        if (peek().type == TokenType::MINUS ||
            peek().type == TokenType::PLUS) {
            Token op  = consume();
            auto  res = parse_unary();
            if (!res.ok) return res;

            ExprNode node;
            node.type = ExprNodeType::UNARY;
            node.name = (op.type == TokenType::MINUS) ? "-" : "+";
            node.children.push_back(std::move(res.root));
            res.root = std::move(node);
            return res;
        }
        return parse_primary();
    }

    // primary := NUMBER | IDENT | IDENT '(' expr_list ')' | '(' expr ')'
    ParseResult parse_primary() {
        const Token& t = peek();

        if (t.type == TokenType::NUMBER) {
            consume();
            ExprNode node;
            node.type    = ExprNodeType::LITERAL;
            node.literal = t.num;
            ParseResult r;
            r.ok   = true;
            r.root = std::move(node);
            return r;
        }

        if (t.type == TokenType::IDENT) {
            Token ident = consume();
            if (peek().type == TokenType::LPAREN) {
                consume(); // eat '('
                ExprNode node;
                node.type = ExprNodeType::CALL;
                node.name = ident.text;

                if (peek().type != TokenType::RPAREN) {
                    while (true) {
                        auto arg = parse_add();
                        if (!arg.ok) return arg;
                        node.children.push_back(std::move(arg.root));
                        if (peek().type == TokenType::COMMA) {
                            consume();
                        } else {
                            break;
                        }
                    }
                }

                if (peek().type != TokenType::RPAREN) {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "expected ')' at column %d", peek().col);
                    return make_err(buf);
                }
                consume(); // eat ')'

                ParseResult r;
                r.ok   = true;
                r.root = std::move(node);
                return r;
            }

            ExprNode node;
            node.type = ExprNodeType::VARIABLE;
            node.name = ident.text;
            ParseResult r;
            r.ok   = true;
            r.root = std::move(node);
            return r;
        }

        if (t.type == TokenType::LPAREN) {
            consume();
            auto inner = parse_add();
            if (!inner.ok) return inner;
            if (peek().type != TokenType::RPAREN) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "expected ')' at column %d", peek().col);
                return make_err(buf);
            }
            consume();
            return inner;
        }

        if (t.type == TokenType::END) {
            return make_err("unexpected end of expression");
        }

        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "unexpected token '%s' at column %d",
                      t.text.empty() ? "?" : t.text.c_str(), t.col);
        return make_err(buf);
    }
};

} // anonymous namespace

ParseResult parse_expr(const std::string& text) {
    if (text.empty()) {
        ParseResult r;
        r.ok    = false;
        r.error = "empty expression";
        r.col   = 0;
        return r;
    }
    auto tokens = lex_expr(text);
    Parser p{tokens};
    return p.parse_full();
}

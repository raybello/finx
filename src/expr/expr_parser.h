#pragma once
#include <string>
#include <vector>

enum class ExprNodeType { LITERAL, VARIABLE, UNARY, BINARY, CALL };

struct ExprNode {
    ExprNodeType          type    = ExprNodeType::LITERAL;
    double                literal = 0.0;
    std::string           name;        // op for BINARY/UNARY, func for CALL, ident for VARIABLE
    std::vector<ExprNode> children;
};

struct ParseResult {
    ExprNode    root;
    bool        ok  = false;
    std::string error;
    int         col = 0;
};

ParseResult parse_expr(const std::string& text);

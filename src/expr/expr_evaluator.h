#pragma once
#include "expr/expr_parser.h"
#include <unordered_map>
#include <string>

using VarMap = std::unordered_map<std::string, double>;

// Evaluate a parsed expression node with the given variable bindings.
// Returns NaN on unknown variable, division by zero, or domain error. Never throws.
double eval_node(const ExprNode& node, const VarMap& vars);

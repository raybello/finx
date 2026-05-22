#pragma once
#include "expr/expr_parser.h"
#include <unordered_map>
#include <vector>
#include <string>

using VarMap  = std::unordered_map<std::string, double>;
// Pre-computed window function results keyed by "fn:alias:period"
using WinCols = std::unordered_map<std::string, std::vector<double>>;

// Scalar eval — used for ordinary formulas with no window functions.
double eval_node(const ExprNode& node, const VarMap& vars);

// Row-aware eval — resolves sma/ema/stddev/rmin/rmax/roc calls by looking up
// pre-computed result vectors in win_cols at row_idx. Falls back to scalar
// eval for all other node types.
double eval_node(const ExprNode& node, const VarMap& vars,
                 size_t row_idx, const WinCols& win_cols);

#include "expr/expr_evaluator.h"
#include <cmath>
#include <limits>
#include <string>

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

static bool is_window_fn(const std::string& name) {
    return name == "sma" || name == "ema"    || name == "stddev" ||
           name == "rmin" || name == "rmax"  || name == "roc";
}

double eval_node(const ExprNode& node, const VarMap& vars) {
    switch (node.type) {
        case ExprNodeType::LITERAL:
            return node.literal;

        case ExprNodeType::VARIABLE: {
            auto it = vars.find(node.name);
            return (it != vars.end()) ? it->second : kNaN;
        }

        case ExprNodeType::UNARY: {
            double v = eval_node(node.children[0], vars);
            return (node.name == "-") ? -v : v;
        }

        case ExprNodeType::BINARY: {
            double l = eval_node(node.children[0], vars);
            double r = eval_node(node.children[1], vars);
            if (node.name == "+") return l + r;
            if (node.name == "-") return l - r;
            if (node.name == "*") return l * r;
            if (node.name == "/") return (r == 0.0) ? kNaN : l / r;
            if (node.name == "%") return (r == 0.0) ? kNaN : std::fmod(l, r);
            return kNaN;
        }

        case ExprNodeType::CALL: {
            const std::string& fn = node.name;
            auto arg0 = [&]() { return eval_node(node.children[0], vars); };
            auto arg1 = [&]() { return eval_node(node.children[1], vars); };
            if (fn == "sqrt" && node.children.size() == 1) return std::sqrt(arg0());
            if (fn == "cos"  && node.children.size() == 1) return std::cos(arg0());
            if (fn == "sin"  && node.children.size() == 1) return std::sin(arg0());
            if (fn == "tan"  && node.children.size() == 1) return std::tan(arg0());
            if (fn == "abs"  && node.children.size() == 1) return std::fabs(arg0());
            if (fn == "log"  && node.children.size() == 1) return std::log(arg0());
            if (fn == "exp"  && node.children.size() == 1) return std::exp(arg0());
            if (fn == "pow"  && node.children.size() == 2) return std::pow(arg0(), arg1());
            return kNaN;
        }
    }
    return kNaN;
}

double eval_node(const ExprNode& node, const VarMap& vars,
                 size_t row_idx, const WinCols& win_cols) {
    switch (node.type) {
        case ExprNodeType::LITERAL:
            return node.literal;

        case ExprNodeType::VARIABLE: {
            auto it = vars.find(node.name);
            return (it != vars.end()) ? it->second : kNaN;
        }

        case ExprNodeType::UNARY: {
            double v = eval_node(node.children[0], vars, row_idx, win_cols);
            return (node.name == "-") ? -v : v;
        }

        case ExprNodeType::BINARY: {
            double l = eval_node(node.children[0], vars, row_idx, win_cols);
            double r = eval_node(node.children[1], vars, row_idx, win_cols);
            if (node.name == "+") return l + r;
            if (node.name == "-") return l - r;
            if (node.name == "*") return l * r;
            if (node.name == "/") return (r == 0.0) ? kNaN : l / r;
            if (node.name == "%") return (r == 0.0) ? kNaN : std::fmod(l, r);
            return kNaN;
        }

        case ExprNodeType::CALL: {
            const std::string& fn = node.name;
            // Window functions: look up pre-computed column at row_idx
            if (is_window_fn(fn) &&
                node.children.size() == 2 &&
                node.children[0].type == ExprNodeType::VARIABLE &&
                node.children[1].type == ExprNodeType::LITERAL) {
                std::string key = fn + ":" + node.children[0].name + ":" +
                                  std::to_string((int)node.children[1].literal);
                auto it = win_cols.find(key);
                if (it != win_cols.end() && row_idx < it->second.size())
                    return it->second[row_idx];
                return kNaN;
            }
            // Scalar functions — recurse with row-aware overload so nested
            // window calls (e.g. sqrt(sma(x, 5))) are handled correctly.
            auto arg0 = [&]() { return eval_node(node.children[0], vars, row_idx, win_cols); };
            auto arg1 = [&]() { return eval_node(node.children[1], vars, row_idx, win_cols); };
            if (fn == "sqrt" && node.children.size() == 1) return std::sqrt(arg0());
            if (fn == "cos"  && node.children.size() == 1) return std::cos(arg0());
            if (fn == "sin"  && node.children.size() == 1) return std::sin(arg0());
            if (fn == "tan"  && node.children.size() == 1) return std::tan(arg0());
            if (fn == "abs"  && node.children.size() == 1) return std::fabs(arg0());
            if (fn == "log"  && node.children.size() == 1) return std::log(arg0());
            if (fn == "exp"  && node.children.size() == 1) return std::exp(arg0());
            if (fn == "pow"  && node.children.size() == 2) return std::pow(arg0(), arg1());
            return kNaN;
        }
    }
    return kNaN;
}

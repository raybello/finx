#include "expr/expr_evaluator.h"
#include <cmath>
#include <limits>

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

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

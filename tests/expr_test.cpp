#include <gtest/gtest.h>
#include <cmath>
#include "expr/expr_parser.h"
#include "expr/expr_evaluator.h"

// Convenience: parse and immediately evaluate with a variable map
static double eval(const std::string& expr, VarMap vars = {}) {
    auto r = parse_expr(expr);
    if (!r.ok) return std::numeric_limits<double>::quiet_NaN();
    return eval_node(r.root, vars);
}

// ── Parser: valid expressions ─────────────────────────────────────────────────

TEST(ExprParser, Literal) {
    auto r = parse_expr("42");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, NegativeLiteral) {
    auto r = parse_expr("-7");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Variable) {
    auto r = parse_expr("x");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Addition) {
    auto r = parse_expr("a + b");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Subtraction) {
    auto r = parse_expr("10 - 3");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Multiplication) {
    auto r = parse_expr("x * y");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Division) {
    auto r = parse_expr("a / b");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Modulo) {
    auto r = parse_expr("n % 3");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, Parentheses) {
    auto r = parse_expr("(a + b) * c");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, NestedParentheses) {
    auto r = parse_expr("((a + b) * (c - d))");
    EXPECT_TRUE(r.ok);
}

TEST(ExprParser, FunctionCallSingleArg) {
    for (const char* fn : {"sqrt", "abs", "sin", "cos", "tan", "log", "exp"}) {
        auto r = parse_expr(std::string(fn) + "(x)");
        EXPECT_TRUE(r.ok) << "failed for: " << fn;
    }
}

TEST(ExprParser, FunctionCallTwoArgs) {
    auto r = parse_expr("pow(2, 10)");
    EXPECT_TRUE(r.ok);
}

// ── Parser: error cases ───────────────────────────────────────────────────────

TEST(ExprParser, EmptyExpression) {
    auto r = parse_expr("");
    EXPECT_FALSE(r.ok);
}

TEST(ExprParser, UnbalancedOpenParen) {
    auto r = parse_expr("(1 + 2");
    EXPECT_FALSE(r.ok);
}

TEST(ExprParser, TrailingOperator) {
    auto r = parse_expr("1 +");
    EXPECT_FALSE(r.ok);
}

// ── Evaluator: arithmetic ─────────────────────────────────────────────────────

TEST(ExprEval, IntegerLiteral) {
    EXPECT_DOUBLE_EQ(eval("5"), 5.0);
}

TEST(ExprEval, FloatLiteral) {
    EXPECT_DOUBLE_EQ(eval("3.14"), 3.14);
}

TEST(ExprEval, UnaryNegation) {
    EXPECT_DOUBLE_EQ(eval("-9"), -9.0);
}

TEST(ExprEval, Addition) {
    EXPECT_DOUBLE_EQ(eval("3 + 4"), 7.0);
}

TEST(ExprEval, Subtraction) {
    EXPECT_DOUBLE_EQ(eval("10 - 3"), 7.0);
}

TEST(ExprEval, Multiplication) {
    EXPECT_DOUBLE_EQ(eval("6 * 7"), 42.0);
}

TEST(ExprEval, Division) {
    EXPECT_DOUBLE_EQ(eval("10 / 4"), 2.5);
}

TEST(ExprEval, Modulo) {
    EXPECT_DOUBLE_EQ(eval("10 % 3"), 1.0);
}

TEST(ExprEval, OperatorPrecedence_MulBeforeAdd) {
    EXPECT_DOUBLE_EQ(eval("2 + 3 * 4"), 14.0);
}

TEST(ExprEval, OperatorPrecedence_DivBeforeSub) {
    EXPECT_DOUBLE_EQ(eval("10 - 6 / 2"), 7.0);
}

TEST(ExprEval, ParenthesesOverridePrecedence) {
    EXPECT_DOUBLE_EQ(eval("(2 + 3) * 4"), 20.0);
}

TEST(ExprEval, ChainedAddition) {
    EXPECT_DOUBLE_EQ(eval("1 + 2 + 3 + 4"), 10.0);
}

// ── Evaluator: variables ──────────────────────────────────────────────────────

TEST(ExprEval, SingleVariable) {
    EXPECT_DOUBLE_EQ(eval("x", {{"x", 5.0}}), 5.0);
}

TEST(ExprEval, MultipleVariables) {
    EXPECT_DOUBLE_EQ(eval("a + b", {{"a", 3.0}, {"b", 7.0}}), 10.0);
}

TEST(ExprEval, VariableInComplex) {
    // (births / total_pop) * 100 — the canonical formula stream use-case
    EXPECT_DOUBLE_EQ(eval("(a / b) * 100", {{"a", 50.0}, {"b", 1000.0}}), 5.0);
}

TEST(ExprEval, UnknownVariableReturnsNan) {
    double v = eval("undefined_var");
    EXPECT_TRUE(std::isnan(v));
}

// ── Evaluator: built-in functions ────────────────────────────────────────────

TEST(ExprEval, Sqrt) {
    EXPECT_DOUBLE_EQ(eval("sqrt(16)"), 4.0);
}

TEST(ExprEval, Abs_Negative) {
    EXPECT_DOUBLE_EQ(eval("abs(-7)"), 7.0);
}

TEST(ExprEval, Abs_Positive) {
    EXPECT_DOUBLE_EQ(eval("abs(7)"), 7.0);
}

TEST(ExprEval, Sin) {
    EXPECT_NEAR(eval("sin(0)"), 0.0, 1e-12);
}

TEST(ExprEval, Cos) {
    EXPECT_NEAR(eval("cos(0)"), 1.0, 1e-12);
}

TEST(ExprEval, Log_E) {
    EXPECT_NEAR(eval("log(2.718281828)"), 1.0, 1e-7);
}

TEST(ExprEval, Exp_Zero) {
    EXPECT_DOUBLE_EQ(eval("exp(0)"), 1.0);
}

TEST(ExprEval, Pow) {
    EXPECT_DOUBLE_EQ(eval("pow(2, 10)"), 1024.0);
}

TEST(ExprEval, NestedFunctions) {
    EXPECT_NEAR(eval("sqrt(pow(3, 2) + pow(4, 2))"), 5.0, 1e-12);
}

// ── Evaluator: edge cases ─────────────────────────────────────────────────────

TEST(ExprEval, DivisionByZero) {
    double v = eval("1 / 0");
    EXPECT_TRUE(std::isinf(v) || std::isnan(v));
}

TEST(ExprEval, SqrtNegative) {
    double v = eval("sqrt(-1)");
    EXPECT_TRUE(std::isnan(v));
}

// ── Window function eval (row-aware overload) ─────────────────────────────────

static double eval_win(const std::string& expr, const VarMap& vars,
                        size_t row, const WinCols& wc) {
    auto r = parse_expr(expr);
    if (!r.ok) return std::numeric_limits<double>::quiet_NaN();
    return eval_node(r.root, vars, row, wc);
}

TEST(ExprEvalWindow, ParsesWindowFunctionAsCall) {
    auto r = parse_expr("sma(close, 20)");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.root.type, ExprNodeType::CALL);
    EXPECT_EQ(r.root.name, "sma");
    ASSERT_EQ(r.root.children.size(), 2u);
    EXPECT_EQ(r.root.children[0].type, ExprNodeType::VARIABLE);
    EXPECT_EQ(r.root.children[1].type, ExprNodeType::LITERAL);
}

TEST(ExprEvalWindow, LooksUpPrecomputedResult) {
    WinCols wc;
    wc["sma:x:3"] = {std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::quiet_NaN(),
                     20.0, 30.0, 40.0};
    EXPECT_TRUE(std::isnan(eval_win("sma(x, 3)", {{"x", 1.0}}, 0, wc)));
    EXPECT_DOUBLE_EQ(eval_win("sma(x, 3)", {{"x", 30.0}}, 2, wc), 20.0);
    EXPECT_DOUBLE_EQ(eval_win("sma(x, 3)", {{"x", 40.0}}, 3, wc), 30.0);
}

TEST(ExprEvalWindow, WindowInArithmeticExpression) {
    // x - sma(x, 2): x=3.0, sma result at row 2 = 2.5 → 0.5
    WinCols wc;
    wc["sma:x:2"] = {std::numeric_limits<double>::quiet_NaN(), 1.5, 2.5, 3.5};
    double v = eval_win("x - sma(x, 2)", {{"x", 3.0}}, 2, wc);
    EXPECT_DOUBLE_EQ(v, 0.5);
}

TEST(ExprEvalWindow, NestedWindowInScalarFunction) {
    // abs(sma(x, 2)) at row 1 = abs(1.5)
    WinCols wc;
    wc["sma:x:2"] = {std::numeric_limits<double>::quiet_NaN(), 1.5, 2.5};
    double v = eval_win("abs(sma(x, 2))", {{"x", 2.0}}, 1, wc);
    EXPECT_DOUBLE_EQ(v, 1.5);
}

TEST(ExprEvalWindow, MissingKeyReturnsNaN) {
    WinCols wc; // empty
    double v = eval_win("sma(x, 5)", {{"x", 10.0}}, 0, wc);
    EXPECT_TRUE(std::isnan(v));
}

TEST(ExprEvalWindow, AllWindowFunctionsParse) {
    for (const char* expr : {"sma(x,5)", "ema(x,5)", "stddev(x,5)",
                              "rmin(x,5)", "rmax(x,5)", "roc(x,5)"}) {
        auto r = parse_expr(expr);
        EXPECT_TRUE(r.ok) << "failed to parse: " << expr;
    }
}

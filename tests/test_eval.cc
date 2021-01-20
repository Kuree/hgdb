#include "../src/eval.hh"
#include "gtest/gtest.h"

TEST(expr, symbol_parse) {  // NOLINT
    auto legal_symbols = {"a[0]", "a[0][0]", "__a", "$a", "a.b", "a0", "a[0].b", "a.b[0]", "a0$b0"};
    for (auto const *expr : legal_symbols) {
        hgdb::DebugExpression debug_expr(expr);
        EXPECT_TRUE(debug_expr.correct());
        EXPECT_EQ(debug_expr.size(), 1);
        EXPECT_NE(debug_expr.find(expr), debug_expr.end());
    }
    // test illegal legal_symbols
    auto illegal_symbols = {"0a", "="};
    for (auto const *expr : illegal_symbols) {
        hgdb::DebugExpression debug_expr(expr);
        EXPECT_FALSE(debug_expr.correct());
        EXPECT_TRUE(debug_expr.empty());
    }

    // test (symbol)
    hgdb::DebugExpression debug_expr_("(a)");
    EXPECT_TRUE(debug_expr_.correct());
    EXPECT_EQ(debug_expr_.size(), 1);
    EXPECT_NE(debug_expr_.find("a"), debug_expr_.end());
}

TEST(expr, expr_parse) {    // NOLINT
    const hgdb::expr::Expr *expr;
    auto const *expr1 = "1 + 2 * 3";
    hgdb::DebugExpression debug_expr1(expr1);
    EXPECT_TRUE(debug_expr1.correct());
    expr = debug_expr1.root();
    EXPECT_EQ(expr->op, hgdb::expr::Operator::Add);

    auto const *expr2 = "a * (b + c)";
    hgdb::DebugExpression debug_expr2(expr2);
    EXPECT_TRUE(debug_expr2.correct());
    expr = debug_expr2.root();
    EXPECT_EQ(expr->op, hgdb::expr::Operator::Multiply);
}

TEST(expr, expr_eval) { // NOLINT
    hgdb::ExpressionType result;

    auto const *expr1 = "1";
    hgdb::DebugExpression debug_expr1(expr1);
    EXPECT_TRUE(debug_expr1.correct());
    result = debug_expr1.eval({});
    EXPECT_EQ(result, 1);

    auto const *expr2 = "1 + a";
    hgdb::DebugExpression debug_expr2(expr2);
    EXPECT_TRUE(debug_expr2.correct());
    result = debug_expr2.eval({{"a", 41}});
    EXPECT_EQ(result, 42);
}
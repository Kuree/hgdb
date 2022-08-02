#include "../src/eval.hh"
#include "gtest/gtest.h"

TEST(expr, symbol_parse) {  // NOLINT
    auto legal_symbols = {"a[0]",   "a[0][0]", "__a",   "$a",     "a.b",    "a0",
                          "a[0].b", "a.b[0]",  "a0$b0", "a[0:0]", "a[2:3]", "a[b]"};
    for (auto const *expr : legal_symbols) {
        hgdb::DebugExpression debug_expr(expr);
        EXPECT_TRUE(debug_expr.correct());
        EXPECT_EQ(debug_expr.size(), 1);
        EXPECT_NE(debug_expr.find(expr), debug_expr.end());
    }
    // test illegal legal_symbols
    auto illegal_symbols = {"0a", "=", "a[:0]", "a[c:0]"};
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

TEST(expr, expr_parse) {  // NOLINT
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

    auto const *expr3 = "!a";
    hgdb::DebugExpression debug_expr3(expr3);
    EXPECT_TRUE(debug_expr3.correct());
    expr = debug_expr3.root();
    EXPECT_EQ(expr->op, hgdb::expr::Operator::Not);

    auto const *expr4 = "!a == 1";
    hgdb::DebugExpression debug_expr4(expr4);
    EXPECT_TRUE(debug_expr4.correct());
    expr = debug_expr4.root();
    EXPECT_EQ(expr->op, hgdb::expr::Operator::Eq);

    auto const *expr5 = "(a > 5) <= 1";
    hgdb::DebugExpression debug_expr5(expr5);
    EXPECT_TRUE(debug_expr5.correct());
    expr = debug_expr5.root();
    EXPECT_EQ(expr->op, hgdb::expr::Operator::LE);
}

TEST(expr, expr_const) {
    auto const *expr1 = "in1 == 1'ha";
    hgdb::DebugExpression debug_expr1(expr1);
    EXPECT_TRUE(debug_expr1.correct());
}

TEST(expr, expr_eval) {  // NOLINT
    hgdb::ExpressionType result;

    auto const *expr1 = "1";
    hgdb::DebugExpression debug_expr1(expr1);
    EXPECT_TRUE(debug_expr1.correct());
    result = debug_expr1.eval();
    EXPECT_EQ(result, 1);

    auto const *expr2 = "1 + a";
    hgdb::DebugExpression debug_expr2(expr2);
    EXPECT_TRUE(debug_expr2.correct());
    debug_expr2.set_value("a", 41);
    result = debug_expr2.eval();
    EXPECT_EQ(result, 42);

    auto const *expr3 = "a == 42";
    hgdb::DebugExpression debug_expr3(expr3);
    EXPECT_TRUE(debug_expr3.correct());
    debug_expr3.set_value("a", 42);
    result = debug_expr3.eval();
    EXPECT_EQ(result, 1);

    auto const *expr4 = "a==42&&b==1";  // no space
    hgdb::DebugExpression debug_expr4(expr4);
    EXPECT_TRUE(debug_expr4.correct());
    debug_expr4.set_value("a", 42);
    debug_expr4.set_value("b", 1);
    result = debug_expr4.eval();
    EXPECT_EQ(result, 1);
    debug_expr4.set_value("b", 2);
    result = debug_expr4.eval();
    EXPECT_EQ(result, 0);

    auto const *expr5 = "a + b * c - d % e";  // test tree rotate
    hgdb::DebugExpression debug_expr5(expr5);
    EXPECT_TRUE(debug_expr5.correct());
    debug_expr5.set_values({{"a", 1}, {"b", 2}, {"c", 4}, {"d", 5}, {"e", 3}});
    result = debug_expr5.eval();
    EXPECT_EQ(result, 1 + 2 * 4 - 5 % 3);

    auto const *expr6 = "(a + b) * (c - d) % e";  // with brackets
    hgdb::DebugExpression debug_expr6(expr6);
    EXPECT_TRUE(debug_expr6.correct());
    debug_expr6.set_values({{"a", 1}, {"b", 2}, {"c", 4}, {"d", 5}, {"e", 3}});
    result = debug_expr6.eval();
    EXPECT_EQ(result, (1 + 2) * (4 - 5) % 3);

    auto const *expr7 = "!a && b && ~c";
    hgdb::DebugExpression debug_expr7(expr7);
    EXPECT_TRUE(debug_expr7.correct());
    debug_expr7.set_values({{"a", 0}, {"b", 1}, {"c", 0}});
    result = debug_expr7.eval();
    EXPECT_EQ(result, 1);

    auto const *expr8 = "!!a && (~~a)";
    hgdb::DebugExpression debug_expr8(expr8);
    EXPECT_TRUE(debug_expr8.correct());
    debug_expr8.set_value("a", 1);
    result = debug_expr8.eval();
    EXPECT_EQ(result, 1);

    auto const *expr9 = "a < 10 && a > 5";
    hgdb::DebugExpression debug_expr9(expr9);
    EXPECT_TRUE(debug_expr9.correct());
    debug_expr9.set_value("a", 6);
    result = debug_expr9.eval();
    EXPECT_EQ(result, 1);
    debug_expr9.set_value("a", 4);
    result = debug_expr9.eval();
    EXPECT_EQ(result, 0);
}
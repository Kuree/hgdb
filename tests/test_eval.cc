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
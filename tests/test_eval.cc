#include "../src/eval.hh"
#include "gtest/gtest.h"

TEST(eval, get_tokens) {    // NOLINT
    const auto *expr = "a + b - c";
    auto tokens = hgdb::ExpressionHelper::get_expr_symbols(expr);
    EXPECT_EQ(tokens.size(), 3);
    for (auto const &s: {"a", "b", "c"}) {
        EXPECT_NE(tokens.find(s), tokens.end());
    }
}

TEST(eval, eval_expr) { // NOLINT
    const auto *expr_str = "a == 5 and b == 6";
    auto expr = hgdb::DebugExpression(expr_str);
    EXPECT_EQ(expr.symbols().size(), 2);
    std::unordered_map<std::string, int64_t> values{{"a", 5}, {"b", 6}};
    auto v = expr.eval(values);
    EXPECT_EQ(v, 1);
    values["a"] = 4;
    v = expr.eval(values);
    EXPECT_EQ(v, 0);
    // missing args
    values.erase("a");
    v = expr.eval(values);
    EXPECT_EQ(v, 0);
}
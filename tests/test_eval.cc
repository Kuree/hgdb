#include "../src/eval.hh"
#include "gtest/gtest.h"

TEST(expr, symbol_parse) {  // NOLINT
    bool r;
    std::unordered_set<std::string> symbols;

    auto legal_symbols = {"a[0]", "a[0][0]", "__a", "$a", "a.b", "a0", "a[0].b", "a.b[0]", "a0$b0"};
    for (auto const *expr : legal_symbols) {
        r = hgdb::parse(expr, symbols);
        EXPECT_TRUE(r);
        EXPECT_EQ(symbols.size(), 1);
        EXPECT_NE(symbols.find(expr), symbols.end());
        symbols.clear();
    }
    // test illegal legal_symbols
    auto illegal_symbols = {"0a", "="};
    for (auto const *expr : illegal_symbols) {
        r = hgdb::parse(expr, symbols);
        EXPECT_FALSE(r);
        EXPECT_TRUE(symbols.empty());
        symbols.clear();
    }

    // test (symbol)
    r = hgdb::parse("(a)", symbols);
    EXPECT_TRUE(r);
    EXPECT_EQ(symbols.size(), 1);
    EXPECT_NE(symbols.find("a"), symbols.end());
    symbols.clear();
}
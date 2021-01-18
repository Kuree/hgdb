#include "eval.hh"

namespace hgdb {

std::unique_ptr<exprtk::expression<ExpressionHelper::ExpressionType>>
ExpressionHelper::get_expression(const std::string &expression,
                                 exprtk::symbol_table<ExpressionHelper::ExpressionType> &table) {
    exprtk::parser<ExpressionHelper::ExpressionType> parser;
    auto expr = std::make_unique<exprtk::expression<ExpressionHelper::ExpressionType>>();
    expr->register_symbol_table(table);
    int r = parser.compile(expression, *expr);
    if (r) {
        return expr;
    } else {
        return nullptr;
    }
}

std::unordered_set<std::string> ExpressionHelper::get_expr_symbols(const std::string &expression) {
    std::unordered_set<std::string> result;
    exprtk::lexer::generator generator;
    generator.process(expression);
    // copied from exprtk implementation
    const static std::unordered_set<std::string> predefined_symbols = {
        "and",  "nand",  "or", "nor", "xor", "xnor", "in",
        "like", "ilike", "&",  "|",   "not", "~",    "!"};

    auto size = generator.size();
    for (auto i = 0u; i < size; i++) {
        auto token = generator[i];
        if (token.type == exprtk::lexer::token::token_type::e_symbol) {
            auto val = token.value;
            if (predefined_symbols.find(val) == predefined_symbols.end())
                result.emplace(token.value);
        }
    }
    return result;
}

DebugExpression::DebugExpression(const std::string &expression) : expression_(expression) {
    symbols_ = ExpressionHelper::get_expr_symbols(expression_);
    for (auto const &name : symbols_) {
        symbol_table_.emplace(name, 0);
        exprtk_table_.add_variable(name, symbol_table_.at(name));
    }
    // notice that we actually lex the expression twice here
    expr_ = ExpressionHelper::get_expression(expression, exprtk_table_);
}

int64_t DebugExpression::eval(const std::unordered_map<std::string, int64_t> &values) {
    if (!correct()) return 0;
    uint64_t hit = 0;
    for (auto const &[name, value] : values) {
        if (symbols_.find(name) != symbols_.end()) [[likely]] {
            hit++;
            symbol_table_[name] = value;
        }
    }
    if (hit != symbols_.size()) {
        // some values are missing
        return 0;
    } else {
        auto result = expr_->value();
        return static_cast<int64_t>(result);
    }
}

}  // namespace hgdb
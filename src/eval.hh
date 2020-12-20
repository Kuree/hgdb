#ifndef HGDB_EVAL_HH
#define HGDB_EVAL_HH

#include <exprtk.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace hgdb {

class ExpressionHelper {
public:
    // exprtk requires to use floats
    using ExpressionType = double;
    static std::unique_ptr<exprtk::expression<ExpressionType>> get_expression(
        const std::string &expression, exprtk::symbol_table<ExpressionHelper::ExpressionType>&table);
    static std::unordered_set<std::string> get_expr_symbols(const std::string &expression);
};

class DebugExpression {
public:
    explicit DebugExpression(const std::string &expression);
    [[nodiscard]] const std::unordered_set<std::string> &symbols() const { return symbols_; }
    int64_t eval(const std::unordered_map<std::string, int64_t> &values);

private:
    std::string expression_;
    std::unordered_set<std::string> symbols_;
    std::unordered_map<std::string, ExpressionHelper::ExpressionType> symbol_table_;

    std::unique_ptr<exprtk::expression<ExpressionHelper::ExpressionType>> expr_;
    exprtk::symbol_table<ExpressionHelper::ExpressionType> exprtk_table_;
};

}  // namespace hgdb

#endif  // HGDB_EVAL_HH

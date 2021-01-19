#ifndef HGDB_EVAL_HH
#define HGDB_EVAL_HH

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace hgdb {

bool parse(const std::string &value, std::unordered_set<std::string> &symbols);

class ExpressionHelper {
public:
    // exprtk requires to use floats
    using ExpressionType = double;
    static std::unordered_set<std::string> get_expr_symbols(const std::string &) { return {}; }
};

class DebugExpression {
public:
    explicit DebugExpression(const std::string &expression) {}
    [[nodiscard]] const std::unordered_set<std::string> &symbols() const { return symbols_; }
    int64_t eval(const std::unordered_map<std::string, int64_t> &) { return 0; }
    [[nodiscard]] bool correct() const { return true; }

    // no copy construction
    DebugExpression(const DebugExpression &) = delete;

    [[nodiscard]] const std::string &expression() const { return expression_; }

private:
    std::string expression_;
    std::unordered_set<std::string> symbols_;
    std::unordered_map<std::string, ExpressionHelper::ExpressionType> symbol_table_;
};

}  // namespace hgdb

#endif  // HGDB_EVAL_HH

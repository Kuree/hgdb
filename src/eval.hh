#ifndef HGDB_EVAL_HH
#define HGDB_EVAL_HH

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hgdb {

using ExpressionType = int64_t;

namespace expr {
enum class Operator {
    None,
    UAdd,
    UMinus,
    Add,
    Minus,
    Multiply,
    Divide,
    Mod,
    Eq,
    Neq,
    Not,
    Invert,
    And,
    Xor,
    Or,
    BAnd,
    BOr,
    LT,
    GT,
    LE,
    GE
};
class Expr {
public:
    explicit Expr(Operator op) : op(op) {}
    void set_value(ExpressionType value) { value_ = value; }

    Expr *left = nullptr;
    Expr *right = nullptr;
    Expr *unary = nullptr;

    Operator op = Operator::None;

    [[nodiscard]] ExpressionType eval() const;

private:
    ExpressionType value_{0};
};

class Symbol : public Expr {
public:
    explicit Symbol(std::string name) : Expr(Operator::None), name(std::move(name)) {}
    std::string name;
};
}  // namespace expr

class DebugExpression {
public:
    // unsigned int * is vpiHandle
    using vpiHandle = unsigned int *;
    explicit DebugExpression(const std::string &expression);

    // symbol table related functions
    [[nodiscard]] const std::unordered_set<std::string> &symbols() const { return symbols_str_; }
    [[nodiscard]] uint64_t size() const { return symbols_str_.size(); }
    auto find(const std::string &value) const { return symbols_str_.find(value); }
    auto end() const { return symbols_str_.end(); }
    [[nodiscard]] bool empty() const { return symbols_str_.empty(); }
    [[nodiscard]] int64_t eval() const;
    [[nodiscard]] bool correct() const { return correct_ && root_ != nullptr; }
    void set_error() { correct_ = false; }
    [[nodiscard]] const expr::Expr *root() const { return root_; }

    expr::Expr *add_expression(expr::Operator op);
    expr::Symbol *add_symbol(const std::string &name);

    // compute the required symbols. used to speed up runtime evaluation to avoid
    // querying db
    [[nodiscard]] std::unordered_set<std::string> get_required_symbols() const;
    void set_static_values(const std::unordered_map<std::string, int64_t> &static_values);
    void set_resolved_symbol_handle(const std::string &name, vpiHandle handle);
    [[nodiscard]] auto const &get_resolved_symbol_handles() const { return handles_; }
    void clear();

    // no copy construction
    DebugExpression(const DebugExpression &) = delete;

    [[nodiscard]] const std::string &expression() const { return expression_; }

    void set_value(const std::string &name, int64_t value);
    void set_values(const std::unordered_map<std::string, int64_t> &values);

private:
    std::string expression_;
    // only for strings for fast access during evaluation
    std::unordered_set<std::string> symbols_str_;
    std::unordered_map<std::string, expr::Symbol *> symbols_;
    // used for holding static values
    std::unordered_set<std::string> static_values_;
    std::unordered_map<std::string, int64_t> values_;
    std::unordered_map<std::string, vpiHandle> handles_;

    std::vector<std::unique_ptr<expr::Expr>> expressions_;

    bool correct_ = true;
    expr::Expr *root_ = nullptr;
};

}  // namespace hgdb

#endif  // HGDB_EVAL_HH

#include "eval.hh"

#include <stack>
#include <tao/pegtl.hpp>

#include "log.hh"

namespace hgdb {

// construct peg grammar

namespace expr {

using tao::pegtl::alpha;
using tao::pegtl::digit;
using tao::pegtl::eof;
using tao::pegtl::if_must;
using tao::pegtl::list;
using tao::pegtl::one;
using tao::pegtl::opt;
using tao::pegtl::pad;
using tao::pegtl::parse;
using tao::pegtl::plus;
using tao::pegtl::seq;
using tao::pegtl::sor;
using tao::pegtl::space;
using tao::pegtl::star;
using tao::pegtl::two;
using tao::pegtl::xdigit;

struct integer : plus<digit> {};
struct hex_integer : seq<plus<digit>, one<'\''>, one<'h'>, plus<xdigit>> {};
struct variable_head1 : plus<sor<alpha, one<'_'>, one<'$'>>> {};
struct variable_head2 : seq<variable_head1, star<sor<variable_head1, digit>>> {};
struct numeric_slice : seq<plus<digit, opt<one<':'>, plus<digit>>>> {};
struct variable_tail : if_must<one<'['>, sor<variable_head2, numeric_slice>, one<']'>> {};
struct variable2 : seq<variable_head2, star<variable_tail>> {};
struct variable : list<variable2, one<'.'>> {};

struct plus_ : one<'+'> {};
struct minus : one<'-'> {};
struct multiply : one<'*'> {};
struct divide : one<'/'> {};
struct mod : one<'%'> {};
struct eq : two<'='> {};
struct neq : seq<one<'!'>, one<'='>> {};
struct not_ : one<'!'> {};
struct flip : one<'~'> {};
struct b_and : one<'&'> {};
struct b_or : one<'|'> {};
struct xor_ : one<'^'> {};
struct and_ : two<'&'> {};
struct or_ : two<'|'> {};
struct lt : one<'<'> {};
struct gt : one<'>'> {};
struct le : seq<one<'<'>, one<'='>> {};
struct ge : seq<one<'>'>, one<'='>> {};

// https://en.cppreference.com/w/c/language/operator_precedence
// https://github.com/pointlander/peg/blob/master/grammars/c/c.peg
struct multiplicative_op : sor<multiply, divide, mod> {};
struct additive_op : sor<plus_, minus> {};
struct relational_op : sor<le, ge, lt, gt> {};
struct equality_op : sor<eq, neq> {};
struct band_op : sor<b_and> {};
struct xor_op : sor<xor_> {};
struct bor_op : sor<b_or> {};
struct land_op : sor<and_> {};
struct lor_op : sor<or_> {};

struct unary_op : sor<not_, flip, plus_, minus> {};

struct open_bracket : seq<one<'('>, star<space>> {};
struct close_bracket : seq<star<space>, one<')'>> {};

struct expression;
struct unary_expression;
struct bracketed : seq<open_bracket, expression, close_bracket> {};
struct value : pad<sor<hex_integer, integer, variable, bracketed>, space> {};
struct unary_expression0 : sor<seq<unary_op, value>, seq<unary_op, unary_expression>> {};
struct unary_expression : sor<value, pad<unary_expression0, space>> {};
struct multiplicative_expression0 : seq<multiplicative_op, unary_expression> {};
struct multiplicative_expression : seq<unary_expression, star<multiplicative_expression0>> {};
struct additive_expression0 : seq<additive_op, multiplicative_expression> {};
struct additive_expression : seq<multiplicative_expression, star<additive_expression0>> {};
struct relational_expression0 : seq<relational_op, additive_expression> {};
struct relational_expression : seq<additive_expression, star<relational_expression0>> {};
struct equality_expression0 : seq<equality_op, relational_expression> {};
struct equality_expression : seq<relational_expression, star<equality_expression0>> {};
struct band_expression0 : seq<band_op, equality_expression> {};
struct band_expression : seq<equality_expression, star<band_expression0>> {};
struct xor_expression0 : seq<xor_op, band_expression> {};
struct xor_expression : seq<band_expression, star<xor_expression0>> {};
struct bor_expression0 : seq<bor_op, xor_expression> {};
struct bor_expression : seq<xor_expression, star<bor_expression0>> {};
struct land_expression0 : seq<land_op, bor_expression> {};
struct land_expression : seq<bor_expression, star<land_expression0>> {};
struct lor_expression0 : seq<lor_op, land_expression> {};
struct lor_expression : seq<land_expression, star<lor_expression0>> {};

struct expression : lor_expression {};

struct grammar : seq<expression, eof> {};

// parser logic
class ParserStack {
public:
    explicit ParserStack(DebugExpression& debug) : debug_(debug) {}

    [[nodiscard]] bool push(const std::string& op_str) {
        const static std::unordered_map<std::string, Operator> op_mapping = {
            {"+", Operator::Add},    {"-", Operator::Minus}, {"*", Operator::Multiply},
            {"/", Operator::Divide}, {"%", Operator::Mod},   {"==", Operator::Eq},
            {"!=", Operator::Neq},   {"!", Operator::Not},   {"~", Operator::Invert},
            {"&&", Operator::And},   {"^", Operator::Xor},   {"||", Operator::Or},
            {"&", Operator::BAnd},   {"|", Operator::BOr},   {"<", Operator::LT},
            {">", Operator::GT},     {"<=", Operator::LE},   {">=", Operator::GE}};
        if (op_mapping.find(op_str) != op_mapping.end()) {
            auto op = op_mapping.at(op_str);
            push(op);
            return true;
        } else {
            return false;
        }
    }

    void push(Operator op) {
        static const std::unordered_set<Operator> unary_ops = {Operator::Not, Operator::Invert,
                                                               Operator::UAdd, Operator::UMinus};
        Expr* result;
        if (unary_ops.find(op) != unary_ops.end()) {
            // unary op
            auto* exp = exprs_.top();
            exprs_.pop();
            auto* expr = debug_.add_expression(op);
            expr->unary = exp;
            result = expr;
        } else {
            auto* right = exprs_.top();
            exprs_.pop();
            auto* left = exprs_.top();
            exprs_.pop();
            auto* expr = debug_.add_expression(op);
            expr->left = left;
            expr->right = right;

            result = expr;
        }
        exprs_.emplace(result);
    }

    void push(Expr* expr) { exprs_.emplace(expr); }

    [[nodiscard]] Expr* root() const {
        if (exprs_.size() != 1) {
            return nullptr;
        } else {
            // should be only one expr
            return exprs_.top();
        }
    }

private:
    std::stack<Operator> ops_;
    std::stack<Expr*> exprs_;

    DebugExpression& debug_;
};

class ParserState {
public:
    explicit ParserState(DebugExpression& debug) : debug_(debug) {
        stacks.emplace(ParserStack(debug_));
    }

    void push(const std::string& op) {
        auto r = stacks.top().push(op);
        if (!r) debug_.set_error();
    }

    void push(Operator op) { stacks.top().push(op); }

    void push(Expr* expr) { stacks.top().push(expr); }

    void open() { stacks.emplace(ParserStack(debug_)); }

    void close() {
        auto& stack = stacks.top();
        auto* expr = stack.root();
        stacks.pop();
        stacks.top().push(expr);
    }

    Expr* root() { return stacks.top().root(); }

    inline Symbol* add_symbol(const std::string& name) { return debug_.add_symbol(name); }
    inline Expr* add_expression(expr::Operator op) { return debug_.add_expression(op); }

private:
    // stack of stacks
    std::stack<ParserStack> stacks;

    DebugExpression& debug_;
};

template <typename Rule>
struct action {};

template <>
struct action<variable> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto name = in.string();
        auto* symbol = state.add_symbol(name);
        state.push(symbol);
    }
};

template <>
struct action<integer> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto* expr = state.add_expression(expr::Operator::None);
        std::stringstream ss(in.string());
        ExpressionType v;
        ss >> v;
        expr->set_value(v);
        state.push(expr);
    }
};

template <>
struct action<hex_integer> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto* expr = state.add_expression(expr::Operator::None);
        std::string value = in.string();
        auto pos = value.find_first_of('\'');
        std::stringstream ss;
        ss << std::hex << value.substr(pos + 2);
        ExpressionType v;
        ss >> v;
        expr->set_value(v);
        state.push(expr);
    }
};

template <>
struct action<multiplicative_expression0> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        state.push(s.substr(0, 1));
    }
};

template <>
struct action<additive_expression0> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        state.push(s.substr(0, 1));
    }
};

template <>
struct action<relational_expression0> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        auto ss = s.substr(0, 2);
        if (ss == ">=" || ss == "<=") {
            state.push(ss);
        } else {
            state.push(s.substr(0, 1));
        }
    }
};

template <>
struct action<equality_expression0> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        state.push(s.substr(0, 2));
    }
};

template <>
struct action<band_expression0> {
    [[maybe_unused]] static void apply0(ParserState& state) { state.push(Operator::BAnd); }
};

template <>
struct action<xor_expression0> {
    [[maybe_unused]] static void apply0(ParserState& state) { state.push(Operator::Xor); }
};

template <>
struct action<bor_expression0> {
    [[maybe_unused]] static void apply0(ParserState& state) { state.push(Operator::BOr); }
};

template <>
struct action<land_expression0> {
    [[maybe_unused]] static void apply0(ParserState& state) { state.push(Operator::And); }
};

template <>
struct action<lor_expression0> {
    [[maybe_unused]] static void apply0(ParserState& state) { state.push(Operator::Or); }
};

template <>
struct action<unary_expression0> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        auto c = s[0];
        switch (c) {
            case '!':
                state.push(Operator::Not);
                break;
            case '~':
                state.push(Operator::Invert);
                break;
            case '+':
                state.push(Operator::UAdd);
                break;
            case '-':
                state.push(Operator::UMinus);
                break;
            default:;
        }
    }
};

template <>
struct action<open_bracket> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput&, ParserState& state) {
        state.open();
    }
};

template <>
struct action<close_bracket> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput&, ParserState& state) {
        state.close();
    }
};

Expr* parse(const std::string& value, DebugExpression& debug_expression) {
    tao::pegtl::memory_input in(value, "");
    ParserState state(debug_expression);
    bool r = false;
    bool exception = false;
    try {
        r = tao::pegtl::parse<grammar, action>(in, state);
    } catch (tao::pegtl::parse_error& error) {
        // maybe disable exceptions in the compiler?
        log::log(log::log_level::error, error.message());
        exception = true;
    }
    if (!r) [[unlikely]] {
        if (!exception) {
            // something wrong with our grammar
            log::log(log::log_level::error, "Incorrect grammar. Unable to parse.");
        }

        debug_expression.set_error();
        return nullptr;
    } else {
        return state.root();
    }
}

ExpressionType Expr::eval() const {
    switch (op) {
        case Operator::None:
            return value_;
        case Operator::UAdd:
            return unary->eval();
        case Operator::UMinus:
            return -unary->eval();
        case Operator::Add:
            return left->eval() + right->eval();
        case Operator::Minus:
            return left->eval() - right->eval();
        case Operator::Multiply:
            return left->eval() * right->eval();
        case Operator::Divide:
            return left->eval() / right->eval();
        case Operator::Mod:
            return left->eval() % right->eval();
        case Operator::Eq:
            return left->eval() == right->eval();
        case Operator::Neq:
            return left->eval() != right->eval();
        case Operator::Not:
            return !unary->eval();
        case Operator::Invert:
            return ~unary->eval();
        case Operator::And:
            return left->eval() && right->eval();
        case Operator::Xor:
            return left->eval() ^ right->eval();
        case Operator::Or:
            return left->eval() || right->eval();
        case Operator::BAnd:
            return left->eval() & right->eval();
        case Operator::BOr:
            return left->eval() | right->eval();
        case Operator::LT:
            return left->eval() < right->eval();
        case Operator::GT:
            return left->eval() > right->eval();
        case Operator::LE:
            return left->eval() <= right->eval();
        case Operator::GE:
            return left->eval() >= right->eval();
    }
    return 0;
}

}  // namespace expr

DebugExpression::DebugExpression(const std::string& expression) : expression_(expression) {
    root_ = expr::parse(expression, *this);
}

expr::Expr* DebugExpression::add_expression(expr::Operator op) {
    auto expr = std::make_unique<expr::Expr>(op);
    expressions_.emplace_back(std::move(expr));
    return expressions_.back().get();
}

expr::Symbol* DebugExpression::add_symbol(const std::string& name) {
    if (symbols_str_.find(name) == symbols_str_.end()) {
        symbols_str_.emplace(name);
        expressions_.emplace_back(std::make_unique<expr::Symbol>(name));
        auto* ptr = reinterpret_cast<expr::Symbol*>(expressions_.back().get());
        symbols_.emplace(name, ptr);
    }

    return symbols_.at(name);
}

int64_t DebugExpression::eval() const {
    if (!root_) [[unlikely]]
        return 0;
    for (auto const& [name, value] : values_) {
        symbols_.at(name)->set_value(value);
    }
    return root_->eval();
}

void DebugExpression::set_static_values(
    const std::unordered_map<std::string, int64_t>& static_values) {
    for (auto const& [name, value] : static_values) {
        if (symbols_.find(name) != symbols_.end()) {
            symbols_.at(name)->set_value(value);
            static_values_.emplace(name);
        }
    }
}

std::unordered_set<std::string> DebugExpression::get_required_symbols() const {
    std::unordered_set<std::string> result;
    for (auto const& name : symbols_str_) {
        // only if we can't find the static value
        if (static_values_.find(name) == static_values_.end()) {
            result.emplace(name);
        }
    }
    return result;
}

void DebugExpression::set_resolved_symbol_handle(const std::string& name, vpiHandle handle) {
    if (symbols_str_.find(name) != symbols_str_.end()) {
        handles_.emplace(name, handle);
        values_.emplace(name, 0);
    }
}

void DebugExpression::clear() {
    handles_.clear();
    values_.clear();
    correct_ = true;
}

void DebugExpression::set_value(const std::string& name, int64_t value) { values_[name] = value; }

void DebugExpression::set_values(const std::unordered_map<std::string, int64_t>& values) {
    for (auto const& [name, value] : values) {
        set_value(name, value);
    }
}

}  // namespace hgdb
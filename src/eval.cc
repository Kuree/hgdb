#include "eval.hh"

#include <stack>
#include <tao/pegtl.hpp>

namespace hgdb {

// construct peg grammar

namespace expr {

using tao::pegtl::alpha;
using tao::pegtl::digit;
using tao::pegtl::eof;
using tao::pegtl::if_must;
using tao::pegtl::list;
using tao::pegtl::one;
using tao::pegtl::pad;
using tao::pegtl::parse;
using tao::pegtl::plus;
using tao::pegtl::seq;
using tao::pegtl::sor;
using tao::pegtl::space;
using tao::pegtl::star;
using tao::pegtl::two;

struct integer : plus<digit> {};
struct variable_head1 : plus<sor<alpha, one<'_'>, one<'$'>>> {};
struct variable_head2 : seq<variable_head1, star<sor<variable_head1, digit>>> {};
struct variable_tail : if_must<one<'['>, plus<digit>, one<']'>> {};
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

struct binary_op : sor<multiply, divide, b_and, b_or, xor_, and_, or_, plus_, minus, mod, eq, neq> {
};
struct binary_op_space : pad<binary_op, space> {};
struct unary_op : sor<not_, flip> {};

struct open_bracket : seq<one<'('>, star<space>> {};
struct close_bracket : seq<star<space>, one<')'>> {};

struct expression;
struct bracketed : seq<open_bracket, expression, close_bracket> {};
struct value : sor<integer, variable, bracketed> {};
struct expression3 : seq<unary_op, expression> {};
struct expression2 : sor<expression3, value> {};
struct expression : list<expression2, binary_op_space> {};

struct grammar : seq<expression, eof> {};

// parser logic
class ParserStack {
public:
    [[nodiscard]] bool push(const std::string& op_str) {
        const static std::unordered_map<std::string, Operator> op_mapping = {
            {"+", Operator::Add},    {"-", Operator::Minus}, {"*", Operator::Multiply},
            {"/", Operator::Divide}, {"%", Operator::Mod},   {"==", Operator::Eq},
            {"!=", Operator::Neq},   {"!", Operator::Not},   {"~", Operator::Invert},
            {"&&", Operator::And},   {"^", Operator::Xor},   {"||", Operator::Or},
            {"&", Operator::BAnd},   {"|", Operator::BOr}};
        if (op_mapping.find(op_str) != op_mapping.end()) {
            auto op = op_mapping.at(op_str);
            ops_.emplace(op);
            return true;
        } else {
            return false;
        }
    }
    void push(Expr* expr) { exprs_.emplace(expr); }

    Expr* reduce(DebugExpression& debug) {
        // TODO implement expresion rotation to account for op priority
        if (ops_.empty()) [[unlikely]] {
            if (exprs_.empty()) {
                return nullptr;
            }
        }
        while (!ops_.empty()) {
            auto op = ops_.top();
            ops_.pop();
            static const std::unordered_set<Operator> unary_ops = {Operator::Not, Operator::Invert};
            if (unary_ops.find(op) != unary_ops.end()) {
                // unary op
                auto* exp = exprs_.top();
                exprs_.pop();
                auto* expr = debug.add_expression(op);
                expr->left = exp;
                exprs_.emplace(expr);
            } else {
                auto* right = exprs_.top();
                exprs_.pop();
                auto* left = exprs_.top();
                exprs_.pop();
                auto* expr = debug.add_expression(op);
                expr->left = left;
                expr->right = right;
                // put it back in
                exprs_.emplace(expr);
            }
        }
        auto* root = exprs_.top();
        fix_precedence(root);
        return root;
    }

private:
    std::stack<Operator> ops_;
    std::stack<Expr*> exprs_;

    static void fix_precedence(Expr* expr) {
        // the map is from https://en.cppreference.com/w/c/language/operator_precedence
        static const std::unordered_map<Operator, uint32_t> precedence{
            {Operator::Multiply, 3}, {Operator::Divide, 3}, {Operator::Mod, 3},
            {Operator::Add, 4},      {Operator::Minus, 4},  {Operator::Eq, 7},
            {Operator::Neq, 7},      {Operator::BAnd, 8},   {Operator::Xor, 9},
            {Operator::BOr, 10},     {Operator::And, 11},   {Operator::Or, 12}};
        Expr* node = expr;
        while (true) {
            auto* next = node->right;
            if (!next) break;
            auto current_op = node->op;
            auto next_op = next->op;
            // if both ops exist in the map and we have low precedence before high precedence
            // do a tree rotation
            if (precedence.find(current_op) != precedence.end() &&
                precedence.find(next_op) != precedence.end() &&
                precedence.at(current_op) < precedence.at(next_op)) {
                /*
                 * before:
                 *     \
                 *      A
                 *     / \
                 *    B   C
                 *       / \
                 *      D   E
                 *
                 * after
                 *      \
                 *       C
                 *      / \
                 *     A   E
                 *    / \
                 *   B   D
                 */
                // We do everything in place
                // first swap op
                auto temp_op = next_op;
                next->op = node->op;
                node->op = temp_op;

                auto* b = node->left;
                auto* d = next->left;
                auto* e = next->right;

                // node is C now
                // and next is A
                auto* c = node;
                auto* a = next;
                c->left = a;
                c->right = e;
                a->left = b;
                a->right = d;
            }

            // scan the next expression node
            node = next;
        }
    }
};

class ParserState {
public:
    explicit ParserState(DebugExpression& debug) : debug_(debug) { stacks.emplace(ParserStack{}); }
    void push(const std::string& op) {
        auto r = stacks.top().push(op);
        if (!r) debug_.set_error();
    }

    void push(Expr* expr) { stacks.top().push(expr); }

    void open() { stacks.emplace(ParserStack{}); }

    void close() {
        auto& stack = stacks.top();
        auto* expr = stack.reduce(debug_);
        stacks.pop();
        stacks.top().push(expr);
    }

    Expr* reduce() { return stacks.top().reduce(debug_); }

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
        expr->set_holder_value(v);
        state.push(expr);
    }
};

template <>
struct action<binary_op> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, ParserState& state) {
        auto s = in.string();
        state.push(s);
    }
};

Expr* parse(const std::string& value, DebugExpression& debug_expression) {
    tao::pegtl::memory_input in(value, "");
    ParserState state(debug_expression);
    auto r = tao::pegtl::parse<grammar, action>(in, state);
    if (!r) {
        debug_expression.set_error();
        return nullptr;
    } else {
        return state.reduce();
    }
}

ExpressionType Expr::eval() const {
    switch (op) {
        case Operator::None:
            return value_;
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
            return !left->eval();
        case Operator::Invert:
            return ~left->eval();
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
        symbol_values_.emplace(name, 0);
        auto* ptr = reinterpret_cast<expr::Symbol*>(expressions_.back().get());
        symbols_.emplace(name, ptr);
        ptr->set_value(symbol_values_.at(name));
    }

    return symbols_.at(name);
}

}  // namespace hgdb
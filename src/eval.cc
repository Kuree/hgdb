#include "eval.hh"

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

struct plus_ : pad<one<'+'>, space> {};
struct minus : pad<one<'-'>, space> {};
struct multiply : pad<one<'*'>, space> {};
struct divide : pad<one<'/'>, space> {};
struct mod : pad<one<'%'>, space> {};
struct eq : pad<two<'='>, space> {};
struct neq : pad<seq<one<'!'>, one<'='>>, space> {};
struct not_ : pad<one<'!'>, space> {};
struct flip : pad<one<'~'>, space> {};
struct b_and : pad<one<'&'>, space> {};
struct b_or : pad<one<'|'>, space> {};
struct xor_ : pad<one<'^'>, space> {};
struct and_ : pad<two<'&'>, space> {};
struct or_ : pad<two<'|'>, space> {};

struct binary_op : sor<multiply, divide, b_and, b_or, xor_, and_, or_, plus_, minus, mod, eq, neq> {
};
struct unary_op : sor<not_, flip> {};

struct open_bracket : seq<one<'('>, star<space>> {};
struct close_bracket : seq<star<space>, one<')'>> {};

struct expression;
struct bracketed : seq<open_bracket, expression, close_bracket> {};
struct value : sor<integer, variable, bracketed> {};
struct expression3 : seq<unary_op, expression> {};
struct expression2 : sor<expression3, value> {};
struct expression : list<expression2, binary_op> {};

struct grammar : seq<expression, eof> {};

template <typename Rule>
struct action {};

template <>
struct action<variable> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in, DebugExpression& debug_expression) {
        auto name = in.string();
        debug_expression.add_symbol(name);
    }
};

bool parse(const std::string& value, DebugExpression& debug_expression) {
    tao::pegtl::memory_input in(value, "");
    auto r = tao::pegtl::parse<grammar, action>(in, debug_expression);
    return r;
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
        case Operator::Flip:
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
    correct_ = expr::parse(expression, *this);
}

expr::Expr* DebugExpression::add_expression(expr::Operator op) {
    auto expr = std::make_unique<expr::Expr>(op);
    expressions_.emplace_back(std::move(expr));
    return expressions_.back().get();
}

void DebugExpression::add_symbol(const std::string& name) {
    if (symbols_str_.find(name) != symbols_str_.end()) return;
    symbols_str_.emplace(name);
    expressions_.emplace_back(std::make_unique<expr::Symbol>(name));
    symbol_values_.emplace(name, 0);
    auto* ptr = reinterpret_cast<expr::Symbol*>(expressions_.back().get());
    symbols_.emplace(name, ptr);
    ptr->set_value(symbol_values_.at(name));
}

}  // namespace hgdb
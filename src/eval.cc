#include "eval.hh"

#include <tao/pegtl.hpp>

namespace hgdb {

// construct peg grammar

namespace pegtl = TAO_PEGTL_NAMESPACE;

using namespace pegtl;

struct integer : plus<digit> {};
struct variable_head1 : plus<sor<alpha, one<'_'>, one<'$'>>> {};
struct variable_head2 : seq<variable_head1, star<sor<variable_head1, digit>>> {};
struct variable_tail : if_must<one<'['>, plus<digit>, one<']'>> {};
struct variable2 : seq<variable_head2, star<variable_tail>> {};
struct variable : list<variable2, one<'.'>> {};

struct plus : pad<one<'+'>, space> {};
struct minus : pad<one<'-'>, space> {};
struct multiply : pad<one<'*'>, space> {};
struct divide : pad<one<'/'>, space> {};
struct eq : pad<two<'='>, space> {};
struct neq : pad<seq<one<'!'>, one<'='>>, space> {};
struct not_ : pad<one<'!'>, space> {};
struct flip : pad<one<'~'>, space> {};
struct b_and : pad<one<'&'>, space> {};
struct b_or : pad<one<'|'>, space> {};
struct xor_ : pad<one<'^'>, space> {};
struct and_ : pad<two<'&'>, space> {};
struct or_ : pad<two<'|'>, space> {};

struct binary_op : sor<multiply, divide, b_and, b_or, xor_, and_, or_, plus, minus, eq, neq> {};
struct unary_op : sor<not_, flip> {};

struct open_bracket : seq<one<'('>, star<space>> {};
struct close_bracket : seq<star<space>, one<')'>> {};

struct expression;
struct bracketed : seq<open_bracket, expression, close_bracket> {};
struct value : sor<integer, variable, bracketed> {};
struct expression3 : seq<unary_op, expression> {};
struct expression2 : sor<expression3, value> {};
struct expression : list<expression2, binary_op> {};

struct grammar : pegtl::seq<expression, pegtl::eof> {};

template <typename Rule>
struct action {};

template <>
struct action<variable> {
    template <typename ActionInput>
    [[maybe_unused]] static void apply(const ActionInput& in,
                                       std::unordered_set<std::string>& symbols) {
        auto name = in.string();
        symbols.emplace(name);
    }
};

bool parse(const std::string& value, std::unordered_set<std::string>& symbols) {
    memory_input in(value, "");
    auto r = pegtl::parse<grammar, action>(in, symbols);
    return r;
}

}  // namespace hgdb
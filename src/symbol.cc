#include "symbol.hh"

#include "db.hh"

auto constexpr TCP_SCHEMA = "tcp://";
auto constexpr WS_SCHEMA = "ws://";

namespace hgdb {
std::unique_ptr<SymbolTableProvider> create_symbol_table(const std::string &filename) {
    // we use some simple way to tell which schema it is
    if (filename.starts_with(TCP_SCHEMA)) {
        // not implemented
        throw std::runtime_error("TCP not implemented");
    } else if (filename.starts_with(WS_SCHEMA)) {
        throw std::runtime_error("WS not implemented");
    } else {
        return std::make_unique<DBSymbolTableProvider>(filename);
    }
}
}  // namespace hgdb

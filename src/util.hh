#ifndef HGDB_UTIL_HH
#define HGDB_UTIL_HH

#include <sstream>
#include <string>
#include <vector>

namespace hgdb::util {

std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);

template <typename T>
std::string join(T begin, T end, const std::string &sep) {
    std::ostringstream stream;
    // more efficient way will be using ostream_joiner
    // but it is still experimental
    auto it = begin;
    stream << *it;
    it++;
    while (it != end) {
        stream << sep;
        stream << *it;
        it++;
    }
    return stream.str();
}

}  // namespace hgdb::util

#endif  // HGDB_UTIL_HH

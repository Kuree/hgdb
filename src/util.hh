#ifndef HGDB_UTIL_HH
#define HGDB_UTIL_HH

#include <string>
#include <vector>

namespace hgdb::util {

std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);

}

#endif  // HGDB_UTIL_HH

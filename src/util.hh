#ifndef HGDB_UTIL_HH
#define HGDB_UTIL_HH

#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace hgdb::util {

std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);

template <typename T>
std::string join(T begin, T end, const std::string &sep) {
    std::ostringstream stream;
    // more efficient way will be using ostream_joiner,
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

[[maybe_unused]] std::optional<int64_t> stol(const std::string &value);
std::optional<uint64_t> stoul(const std::string &value);

std::optional<std::string> getenv(std::string_view name);

class Options {
public:
    [[maybe_unused]] void add_option(const std::string &option_name, [[maybe_unused]] bool *value);
    [[maybe_unused]] void add_option(const std::string &option_name, std::string *value);
    [[maybe_unused]] void add_option(const std::string &option_name, int64_t *value);
    [[maybe_unused]] void set_option(const std::string &option_name, bool value);
    [[maybe_unused]] void set_option(const std::string &option_name, const std::string &value);
    [[maybe_unused]] void set_option(const std::string &option_name, int64_t value);
    [[nodiscard]] std::map<std::string, std::string> get_options() const;

private:
    std::map<std::string, bool *> bool_options_;
    std::map<std::string, std::string *> string_options_;
    std::map<std::string, int64_t *> int_options_;
};

}  // namespace hgdb::util

#endif  // HGDB_UTIL_HH

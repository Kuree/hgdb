#include "util.hh"

namespace hgdb::util {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter) {
    std::vector<std::string> tokens;
    size_t prev = 0, pos;
    std::string token;
    // copied from https://stackoverflow.com/a/7621814
    while ((pos = line.find_first_of(delimiter, prev)) != std::string::npos) {
        if (pos > prev) {
            tokens.emplace_back(line.substr(prev, pos - prev));
        }
        prev = pos + 1;
    }
    // NOLINTNEXTLINE
    if (prev < line.length()) tokens.emplace_back(line.substr(prev, std::string::npos));
    // remove empty ones
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (auto const &t : tokens)
        if (!t.empty()) result.emplace_back(t);
    return result;
}

[[maybe_unused]] std::optional<int64_t> stol(const std::string &value) {
    try {
        return std::stol(value);
    } catch (std::exception &) {
        return {};
    }
}

std::optional<uint64_t> stoul(const std::string &value) {
    try {
        return std::stol(value);
    } catch (std::exception &) {
        return {};
    }
}

std::optional<std::string> getenv(std::string_view name) {
    auto *ptr = std::getenv(name.data());
    if (ptr) {
        return std::string(ptr);
    } else {
        return std::nullopt;
    }
}

[[maybe_unused]] void Options::add_option(const std::string &option_name, bool *value) {
    bool_options_.emplace(option_name, value);
}

[[maybe_unused]] void Options::add_option(const std::string &option_name, std::string *value) {
    string_options_.emplace(option_name, value);
}

void Options::add_option(const std::string &option_name, int64_t *value) {
    int_options_.emplace(option_name, value);
}

void Options::set_option(const std::string &option_name, bool value) {
    if (bool_options_.find(option_name) != bool_options_.end()) {
        *bool_options_.at(option_name) = value;
    }
}

void Options::set_option(const std::string &option_name, int64_t value) {
    if (int_options_.find(option_name) != int_options_.end()) {
        *int_options_.at(option_name) = value;
    }
}

void Options::set_option(const std::string &option_name, const std::string &value) {
    if (string_options_.find(option_name) != string_options_.end()) {
        *string_options_.at(option_name) = value;
    }
}

std::map<std::string, std::string> Options::get_options() const {
    std::map<std::string, std::string> result;
    for (auto const &[key, value] : bool_options_) {
        result.emplace(key, (*value) ? "true" : "false");
    }
    for (auto const &[key, value] : int_options_) {
        result.emplace(key, std::to_string(*value));
    }
    for (auto const &[key, value] : string_options_) {
        result.emplace(key, *value);
    }
    return result;
}

}  // namespace hgdb::util
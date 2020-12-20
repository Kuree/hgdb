#ifndef HGDB_PROTO_HH
#define HGDB_PROTO_HH

#include <fmt/format.h>

#include <string>
#include <type_traits>

#include "schema.hh"

namespace hgdb {
enum class status_code { success = 0, error = 1 };
class Response {
public:
    [[nodiscard]] virtual std::string str() const = 0;

private:
    status_code status_ = status_code::success;
};

class Request {
public:
    [[nodiscard]] status_code status() const { return status_code_; }
    [[nodiscard]] const std::string &error_reason() const { return error_reason_; }

protected:
    status_code status_code_;
    std::string error_reason_;

    template <typename T>
    bool check_member(T &document, const char *member_name, bool set_error = true) {
        if (!document.HasMember(member_name)) {
            if (set_error) error_reason_ = fmt::format("Unable to find member {0}", member_name);
            return false;
        }
        return true;
    }

    template <typename T, typename K>
    std::optional<K> check_type(T &document, const char *member_name, bool set_error = true) {
        if (!check_member(document, member_name, set_error)) return std::nullopt;
        if constexpr (std::is_same<K, std::string>::value) {
            if (document[member_name].IsString()) {
                return std::string(document[member_name].GetString());
            }
        } else if constexpr (std::is_integral<K>::value) {
            if (document[member_name].IsNumber()) {
                return document[member_name].template Get<K>();
            }
        }
        return std::nullopt;
    }
};

class BreakpointRequest : public Request {
public:
    explicit BreakpointRequest(const std::string &payload);
    [[nodiscard]] const std::optional<BreakPoint> &breakpoint() const { return bp_; }

private:
    std::optional<BreakPoint> bp_;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

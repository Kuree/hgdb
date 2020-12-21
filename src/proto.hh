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

    [[nodiscard]] static std::unique_ptr<Request> parse_request(const std::string &str);

    virtual ~Request() = default;

protected:
    status_code status_code_ = status_code::success;
    std::string error_reason_;

    template <typename T>
    static bool check_member(T &document, const char *member_name, std::string &error,
                             bool set_error = true) {
        if (!document.HasMember(member_name)) {
            if (set_error) error = fmt::format("Unable to find member {0}", member_name);
            return false;
        }
        return true;
    }

    template <typename T, typename K>
    static std::optional<K> get_member(T &document, const char *member_name, std::string &error,
                                       bool set_error = true, bool check_type = true) {
        if (!check_member(document, member_name, error, set_error)) return std::nullopt;
        if constexpr (std::is_same<K, std::string>::value) {
            if (document[member_name].IsString()) {
                return std::string(document[member_name].GetString());
            } else if (check_type) {
                error = fmt::format("Invalid type for {0}", member_name);
            }
        } else if constexpr (std::is_integral<K>::value && !std::is_same<K, bool>::value) {
            if (document[member_name].IsNumber()) {
                return document[member_name].template Get<K>();
            } else if (check_type) {
                error = fmt::format("Invalid type for {0}", member_name);
            }
        } else if constexpr (std::is_same<K, bool>::value) {
            if (document[member_name].IsBool()) {
                return document[member_name].GetBool();
            } else if (check_type) {
                error = fmt::format("Invalid type for {0}", member_name);
            }
        }
        return std::nullopt;
    }

    virtual void parse_payload(const std::string &payload) = 0;
};

class ErrorRequest : public Request {
public:
    explicit ErrorRequest(std::string reason);
    void parse_payload(const std::string &) override {}
};

class BreakpointRequest : public Request {
public:
    BreakpointRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] const std::optional<BreakPoint> &breakpoint() const { return bp_; }

private:
    std::optional<BreakPoint> bp_;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

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
    Response() = default;
    Response(status_code status) : status_(status) {}
    [[nodiscard]] virtual std::string str() const = 0;

protected:
    status_code status_ = status_code::success;
};

class GenericResponse : public Response {
public:
    GenericResponse(status_code status, std::string reason = "");
    std::string str() const override;

private:
    std::string reason_;
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

    virtual void parse_payload(const std::string &payload) = 0;
};

class ErrorRequest : public Request {
public:
    explicit ErrorRequest(std::string reason);
    void parse_payload(const std::string &) override {}
};

class BreakpointRequest : public Request {
public:
    enum class action { add, remove };
    BreakpointRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] const std::optional<BreakPoint> &breakpoint() const { return bp_; }
    [[nodiscard]] const std::optional<action> bp_action() const { return bp_action_; }

private:
    std::optional<BreakPoint> bp_;
    std::optional<action> bp_action_;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

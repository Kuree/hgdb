#ifndef HGDB_PROTO_HH
#define HGDB_PROTO_HH

#include <fmt/format.h>

#include <string>
#include <type_traits>
#include <utility>

#include "schema.hh"

namespace hgdb {

enum class status_code { success = 0, error = 1 };
enum class RequestType {
    error,
    breakpoint,
    breakpoint_id,
    connection,
    bp_location,
    command,
    debugger_info
};

[[nodiscard]] std::string to_string(RequestType type) noexcept;

class Request;

class Response {
public:
    Response() = default;
    explicit Response(status_code status) : status_(status) {}
    [[nodiscard]] virtual std::string str(bool pretty_print) const = 0;
    [[nodiscard]] virtual std::string type() const = 0;
    [[nodiscard]] const std::string &token() const { return token_; }
    void set_token(std::string token) { token_ = std::move(token); }

protected:
    status_code status_ = status_code::success;
    std::string token_;
};

class GenericResponse : public Response {
public:
    GenericResponse(status_code status, const Request &req, std::string reason = "");
    GenericResponse(status_code status, RequestType type, std::string reason = "");
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return "generic"; }

private:
    std::string request_type_;
    std::string reason_;
};

class BreakPointLocationResponse : public Response {
public:
    explicit BreakPointLocationResponse(std::vector<BreakPoint *> bps) : bps_(std::move(bps)) {}
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return "bp-location"; }

private:
    std::vector<BreakPoint *> bps_;
};

class BreakPointResponse : public Response {
public:
    BreakPointResponse(uint64_t time, std::string filename, uint64_t line_num,
                       uint64_t column_num = 0);
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return "breakpoint"; }

    struct Scope {
    public:
        uint64_t instance_id;
        uint64_t breakpoint_id;
        std::string instance_name;
        std::map<std::string, std::string> local_values;
        std::map<std::string, std::string> generator_values;

        Scope(uint64_t instance_id, std::string instance_name, uint64_t breakpoint_id);

        void add_local_value(const std::string &name, const std::string &value);
        void add_generator_value(const std::string &name, const std::string &value);
    };

    inline void add_scope(const Scope &scope) { scopes_.emplace_back(scope); }

private:
    uint64_t time_;
    std::string filename_;
    uint64_t line_num_;
    uint64_t column_num_;

    std::vector<Scope> scopes_;
};

class Request {
public:
    [[nodiscard]] status_code status() const { return status_code_; }
    [[nodiscard]] const std::string &error_reason() const { return error_reason_; }
    [[nodiscard]] virtual RequestType type() const = 0;
    inline void set_token(Response &resp) const { resp.set_token(token_); }
    [[nodiscard]] inline const std::string &get_token() const { return token_; }

    [[nodiscard]] static std::unique_ptr<Request> parse_request(const std::string &str);

    virtual ~Request() = default;

protected:
    status_code status_code_ = status_code::success;
    std::string error_reason_;
    std::string token_;

    virtual void parse_payload(const std::string &payload) = 0;
};

class ErrorRequest : public Request {
public:
    explicit ErrorRequest(std::string reason);
    void parse_payload(const std::string &) override {}
    [[nodiscard]] RequestType type() const override { return RequestType::error; }
};

class BreakPointRequest : public Request {
public:
    enum class action { add, remove };
    BreakPointRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] const auto &breakpoint() const { return bp_; }
    [[nodiscard]] auto bp_action() const { return bp_action_; }
    [[nodiscard]] RequestType type() const override { return RequestType::breakpoint; }

protected:
    BreakPoint bp_;
    action bp_action_ = action::add;
};

class BreakPointIDRequest : public BreakPointRequest {
public:
    BreakPointIDRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::breakpoint_id; }
};

class ConnectionRequest : public Request {
public:
    ConnectionRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::connection; }

    [[nodiscard]] const auto &db_filename() const { return db_filename_; }
    [[nodiscard]] const auto &path_mapping() const { return path_mapping_; };

private:
    std::string db_filename_;
    std::map<std::string, std::string> path_mapping_;
};

class BreakPointLocationRequest : public Request {
public:
    BreakPointLocationRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::bp_location; }

    [[nodiscard]] const auto &filename() const { return filename_; }
    [[nodiscard]] const auto &line_num() const { return line_num_; }
    [[nodiscard]] const auto &column_num() const { return column_num_; }

private:
    std::string filename_;
    std::optional<uint64_t> line_num_;
    std::optional<uint64_t> column_num_;
};

class CommandRequest : public Request {
public:
    enum class CommandType { continue_, step_over, stop };

    CommandRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::command; }

    [[nodiscard]] auto command_type() const { return command_type_; }

private:
    CommandType command_type_ = CommandType::continue_;
};

class DebuggerInformationRequest : public Request {
public:
    enum class CommandType { breakpoints, status };
    DebuggerInformationRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::debugger_info; }

    [[nodiscard]] auto const &command_type() const { return command_type_; }

private:
    CommandType command_type_;
};

class DebuggerInformationResponse : public Response {
public:
    explicit DebuggerInformationResponse(std::vector<BreakPoint *> bps);
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return "debugger-info"; }

private:
    DebuggerInformationRequest::CommandType command_type_;
    std::vector<BreakPoint *> bps_;
    std::string get_command_str() const;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

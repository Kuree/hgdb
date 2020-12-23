#ifndef HGDB_PROTO_HH
#define HGDB_PROTO_HH

#include <fmt/format.h>

#include <string>
#include <type_traits>
#include <utility>

#include "schema.hh"

namespace hgdb {
enum class status_code { success = 0, error = 1 };
class Response {
public:
    Response() = default;
    explicit Response(status_code status) : status_(status) {}
    [[nodiscard]] virtual std::string str(bool pretty_print) const = 0;
    [[nodiscard]] virtual std::string type() const = 0;

protected:
    status_code status_ = status_code::success;
};

class GenericResponse : public Response {
public:
    explicit GenericResponse(status_code status, std::string reason = "");
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return "generic"; }

private:
    std::string reason_;
};

class BreakPointLocationResponse : public Response {
public:
    explicit BreakPointLocationResponse(std::vector<BreakPoint *> bps) : Response(), bps_(std::move(bps)) {}
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

    void add_local_value(const std::string &name, const std::string &value);
    void add_generator_value(const std::string &name, const std::string &value);

private:
    uint64_t time_;
    std::string filename_;
    uint64_t line_num_;
    uint64_t column_num_;

    std::map<std::string, std::string> local_values_;
    std::map<std::string, std::string> generator_values_;
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
    [[nodiscard]] const auto &breakpoint() const { return bp_; }
    [[nodiscard]] auto bp_action() const { return bp_action_; }

private:
    std::optional<BreakPoint> bp_;
    std::optional<action> bp_action_;
};

class ConnectionRequest : public Request {
public:
    ConnectionRequest() = default;
    void parse_payload(const std::string &payload) override;

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

    [[nodiscard]] const auto &filename() const { return filename_; }
    [[nodiscard]] const auto &line_num() const { return line_num_; }
    [[nodiscard]] const auto &column_num() const { return column_num_; }

private:
    std::string filename_;
    std::optional<uint64_t> line_num_;
    std::optional<uint64_t> column_num_;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

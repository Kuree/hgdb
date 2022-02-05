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
    debugger_info,
    path_mapping,
    evaluation,
    option_change,
    monitor,
    set_value,
    symbol,
    data_breakpoint
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

    template <typename T>
    void set_value(const std::string &name, T value) {
        if constexpr (std::is_same<T, bool>::value) {
            bool_values_.emplace(name, value);
        } else if constexpr (std::is_arithmetic<T>::value) {
            int_values_.emplace(name, static_cast<int64_t>(value));
        } else if constexpr (std::is_same<T, const std::string &>::value ||
                             std::is_same<T, std::string>::value ||
                             std::is_same<T, const char *>::value) {
            string_values_.emplace(name, value);
        } else {
            static_assert(always_false_v<T>, "Unknown type");
        }
    }

private:
    std::string request_type_;
    std::string reason_;

    std::map<std::string, bool> bool_values_;
    std::map<std::string, int64_t> int_values_;
    std::map<std::string, std::string> string_values_;

    template <typename>
    inline static constexpr bool always_false_v = false;
};

class BreakPointLocationResponse : public Response {
public:
    explicit BreakPointLocationResponse(std::vector<BreakPoint *> bps) : bps_(std::move(bps)) {}
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return to_string(RequestType::bp_location); }

private:
    std::vector<BreakPoint *> bps_;
};

class BreakPointResponse : public Response {
public:
    BreakPointResponse(uint64_t time, std::string filename, uint64_t line_num,
                       uint64_t column_num = 0);
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return to_string(RequestType::breakpoint); }

    struct Scope {
    public:
        uint64_t instance_id;
        uint64_t breakpoint_id;
        std::string instance_name;
        std::string bp_type;
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
    BreakPointRequest(BreakPoint &bp, action bp_action)
        : bp_(std::move(bp)), bp_action_(bp_action) {}
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
    enum class CommandType { continue_, step_over, step_back, stop, reverse_continue, jump };

    CommandRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::command; }

    [[nodiscard]] auto command_type() const { return command_type_; }
    [[nodiscard]] auto time() const { return time_; }

private:
    CommandType command_type_ = CommandType::continue_;
    uint64_t time_ = 0;
};

class DebuggerInformationRequest : public Request {
public:
    enum class CommandType { breakpoints, status, options, design, filename };
    DebuggerInformationRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::debugger_info; }

    [[nodiscard]] auto const &command_type() const { return command_type_; }

private:
    CommandType command_type_ = CommandType::breakpoints;
};

class PathMappingRequest : public Request {
public:
    PathMappingRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::path_mapping; }

    [[nodiscard]] const std::map<std::string, std::string> &path_mapping() const {
        return path_mapping_;
    }

private:
    std::map<std::string, std::string> path_mapping_;
};

class EvaluationRequest : public Request {
public:
    EvaluationRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::evaluation; }

    [[nodiscard]] const std::string &scope() const { return scope_; }
    [[nodiscard]] const std::string &expression() const { return expression_; }
    [[nodiscard]] bool is_context() const { return is_context_; }

private:
    std::string scope_;
    std::string expression_;
    bool is_context_ = false;
};

class OptionChangeRequest : public Request {
public:
    OptionChangeRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::option_change; }

    [[nodiscard]] const std::map<std::string, bool> &bool_values() const { return bool_values_; }
    [[nodiscard]] const std::map<std::string, int64_t> &int_values() const { return int_values_; }
    [[nodiscard]] const std::map<std::string, std::string> &str_values() const {
        return str_values_;
    }

private:
    std::map<std::string, bool> bool_values_;
    std::map<std::string, int64_t> int_values_;
    std::map<std::string, std::string> str_values_;
};

class MonitorRequest : public Request {
public:
    enum class ActionType { add, remove };
    enum class MonitorType { breakpoint, clock_edge, changed, data };
    MonitorRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::monitor; }

    [[nodiscard]] ActionType action_type() const { return action_type_; }
    [[nodiscard]] MonitorType monitor_type() const { return monitor_type_; }
    [[nodiscard]] const std::string &var_name() const { return var_name_; }
    [[nodiscard]] const std::optional<uint64_t> &breakpoint_id() const { return breakpoint_id_; };
    [[nodiscard]] const std::optional<uint64_t> &instance_id() const { return instance_id_; }
    [[nodiscard]] uint64_t track_id() const { return track_id_; }

private:
    ActionType action_type_ = ActionType::add;
    MonitorType monitor_type_ = MonitorType::breakpoint;
    std::string var_name_;
    std::optional<uint64_t> breakpoint_id_;
    std::optional<uint64_t> instance_id_;
    uint64_t track_id_ = 0;
};

class SetValueRequest : public Request {
public:
    SetValueRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::set_value; }

    [[nodiscard]] int64_t value() const { return value_; }
    [[nodiscard]] const std::string &var_name() const { return var_name_; }
    [[nodiscard]] const std::optional<uint64_t> &instance_id() const { return instance_id_; }
    [[nodiscard]] const std::optional<uint64_t> &breakpoint_id() const { return breakpoint_id_; }

private:
    int64_t value_ = 0;
    std::string var_name_;
    std::optional<uint64_t> instance_id_;
    std::optional<uint64_t> breakpoint_id_;
};

class SymbolRequest : public Request {
public:
    enum class request_type {
        get_breakpoint,
        get_breakpoints,
        get_instance_name,
        get_instance_id,
        get_context_variables,
        get_generator_variables,
        get_instance_names,
        get_annotation_values,
        get_all_array_names,
        get_filenames,
        get_execution_bp_orders,
        get_assigned_breakpoints
    };

    explicit SymbolRequest(request_type req_type) : req_type_(req_type) {}
    // won't do anything since we don't expect parsing it from our side
    void parse_payload(const std::string &) override {}

    [[nodiscard]] RequestType type() const override { return RequestType::symbol; }
    [[nodiscard]] request_type req_type() const { return req_type_; }

    [[nodiscard]] std::string str() const;

    uint64_t instance_id = 0;
    uint64_t breakpoint_id = 0;
    std::string filename;
    uint32_t line_num = 0;
    uint32_t column_num = 0;
    std::string instance_name;
    std::string name;
    std::string scoped_name;

private:
    request_type req_type_;
};

class DataBreakpointRequest : public Request {
public:
    enum class Action { add, clear, remove, info };
    DataBreakpointRequest() = default;
    void parse_payload(const std::string &payload) override;
    [[nodiscard]] RequestType type() const override { return RequestType::data_breakpoint; }

    [[nodiscard]] uint64_t breakpoint_id() const { return breakpoint_id_; }
    [[nodiscard]] const std::string &var_name() const { return variable_name_; }
    [[nodiscard]] const std::string &condition() const { return condition_; }
    [[nodiscard]] Action action() const { return action_; }

private:
    uint64_t breakpoint_id_ = 0;
    std::string variable_name_;
    std::string condition_;
    Action action_ = Action::add;
};

struct DebugBreakPoint;
class DebuggerInformationResponse : public Response {
public:
    explicit DebuggerInformationResponse(std::string status);
    explicit DebuggerInformationResponse(std::vector<const DebugBreakPoint *> bps);
    explicit DebuggerInformationResponse(std::map<std::string, std::string> options);
    explicit DebuggerInformationResponse(std::unordered_map<std::string, std::string> design);
    explicit DebuggerInformationResponse(std::vector<std::string> filenames);

    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override {
        return to_string(RequestType::debugger_info);
    }

private:
    DebuggerInformationRequest::CommandType command_type_;
    std::string status_str_;
    std::vector<const DebugBreakPoint *> bps_;
    std::vector<std::string> filenames_;
    std::map<std::string, std::string> options_;
    std::unordered_map<std::string, std::string> design_;
    [[nodiscard]] std::string get_command_str() const;
};

class EvaluationResponse : public Response {
public:
    EvaluationResponse(std::string scope, std::string result);
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return to_string(RequestType::evaluation); }

private:
    std::string scope_;
    std::string result_;
};

class MonitorResponse : public Response {
public:
    MonitorResponse(uint64_t track_id, std::string value);
    [[nodiscard]] std::string str(bool pretty_print) const override;
    [[nodiscard]] std::string type() const override { return to_string(RequestType::monitor); }

private:
    uint64_t track_id_;
    std::string value_;
};

class SymbolResponse : public Response {
public:
    using ContextVariableInfo = std::pair<ContextVariable, Variable>;
    using GeneratorVariableInfo = std::pair<GeneratorVariable, Variable>;
    explicit SymbolResponse(SymbolRequest::request_type type) : type_(type) {}

    void parse(const std::string &str);

    [[nodiscard]] std::string str(bool pretty_print) const override { return {}; }
    [[nodiscard]] std::string type() const override { return to_string(RequestType::symbol); }

    std::optional<std::string> str_result;
    std::vector<BreakPoint> bp_results;
    std::optional<BreakPoint> bp_result;
    std::optional<uint64_t> uint64_t_result;
    std::vector<ContextVariableInfo> context_vars_result;
    std::vector<GeneratorVariableInfo> gen_vars_result;
    std::vector<std::string> str_results;
    std::unordered_map<std::string, int64_t> map_result;
    std::vector<uint32_t> uint64_t_results;
    std::vector<std::tuple<uint32_t, std::string, std::string>> var_result;

private:
    SymbolRequest::request_type type_;
};

}  // namespace hgdb

#endif  // HGDB_PROTO_HH

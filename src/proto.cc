#include "proto.hh"

#include <fmt/format.h>

#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace hgdb {

// need to migrate this specification to AsyncAPI once it is able to describe multiple
// message sharing the same channel

/*
 * Request structure
 * request: true
 * type: [required] - string
 * payload: [required] - object
 * token: [optional] - string -> used to identify a unique request/response
 *
 * Response structure
 * request: false
 * type: [required] - string
 * status: [required] - string: "success" or "error"
 * payload: [required] - object
 * token: [optional] - string -> used to identify a unique request/response
 *
 * Breakpoint Request
 * type: breakpoint
 * payload:
 *     filename: [required] - string
 *     line_num: [required - optional for remove] - uint64_t
 *     action: [required] - string: "add" or "remove"
 *     column_num: [optional] - uint64_t
 *     condition: [optional] - string
 *
 * Breakpoint ID Request
 * type: breakpoint-id
 * payload:
 *     id: [required] - uint64_t
 *     action: [required] - string: "add" or "remove"
 *     condition: [optional] - string
 *
 *
 * Connection Request
 * type: connection
 * payload:
 *     db_filename: [required] - string
 *     path-mapping: [optional] - map<string, string>
 *
 *
 * Breakpoint Location Request
 * type: bp-location
 * payload:
 *     filename: [required] - string
 *     line_num: [optional] - uint64_t
 *     column_num: [optional] - uint64_t
 *
 * Command Request
 * type: command
 * payload:
 *     command: [enum] - string [continue/stop/step_over]
 *
 * Debugger Information Request
 * type: debugger-info
 * payload:
 *     command: [enum] - string
 *
 * Path Mapping Request
 * type: path-mapping
 * payload:
 *     path-mapping: [required] - map<string, string>
 *
 * Evaluation Request
 * type: evaluation
 * payload:
 *     scope: [required] - string
 *     expression: [required] - string
 *     is_context: [required] - bool
 *
 * OptionChange Request
 * type: option-change
 * payload:
 *       Mar<string, value>
 *
 * Monitor Request
 * type: monitor
 * payload:
 *      action: [required] - [enum] string (add/remove)
 *      monitor_type: [required for add] - [enum] string
 *      var_name: [required for add] - string
 *      instance_id: [optional] - uint64_t
 *      breakpoint_id: [optional] - uint64_t
 *      track_id: [required for remove] - uint64_t
 * # notice that add request will get track_id in the generic response. clients are required
 * # to parse the value and use that as tracking id
 *
 * Set Value request
 * type: set-value
 * payload:
 *     var_name: [required] - string
 *     value: [required] - int64_t
 *     instance_id: [optional] - uint64_t
 *     breakpoint_id: [optional] - uint64_t
 * # instance_id and breakpoint_id can be used to scope var_name
 *
 * Data Breakpoint Request
 * type: data_breakpoint
 * payload:
 *     action: [required] - string, of "add", "clear"
 *     var_name: [required for add] - string
 *     breakpoint: [required for add] - uint64_t
 *
 * Generic Response
 * type: generic
 * payload:
 *     request-type: string
 *     reason: string [only exists if error]
 *     ... other information depends on the request
 *
 * Breakpoint Location Response
 * type: bp-location
 * payload:
 *     Array:
 *         id: string
 *         filename: string
 *         line_num: uint64_t
 *         column_num: uint64_t
 *
 * Breakpoint Response
 * type: breakpoint
 * payload:
 *     time: uint64_t
 *     instance_id: uint64_t
 *     instance_name: string
 *     breakpoint_id: uint64_t
 *     filename: string
 *     line_num: uint64_t
 *     column_num: uint64_t
 *     instances:
 *         instance_id: uint64_t
 *         instance_name: string
 *         breakpoint_id: uint64_t
 *         local: map<string, string> - name -> value
 *         generator: Array: map<string, string> - name -> value
 *
 *
 * Debugger Information Response
 * type: debugger-info
 * payload:
 *     command: [enum] string
 *     # depends on the type, only one field will be available
 *     breakpoints: Array<string, uint, uint> ->
 *
 * Evaluation Response
 * type: evaluation
 * payload:
 *     scope: string
 *     result: string
 *
 * Monitor Response
 * type: monitor
 * payload:
 *     track_id: uint64_t
 *     value: uint64_t
 *
 */

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
static std::optional<T> get_member(K &document, const char *member_name, std::string &error,
                                   bool set_error = true, bool check_type = true) {
    if (!check_member(document, member_name, error, set_error)) return std::nullopt;
    if constexpr (std::is_same<T, std::string>::value) {
        if (document[member_name].IsString()) {
            return std::string(document[member_name].GetString());
        } else if (check_type) {
            error = fmt::format("Invalid type for {0}", member_name);
        }
    } else if constexpr (std::is_integral<T>::value && !std::is_same<T, bool>::value) {
        if (document[member_name].IsNumber()) {
            return document[member_name].template Get<T>();
        } else if (check_type) {
            error = fmt::format("Invalid type for {0}", member_name);
        }
    } else if constexpr (std::is_same<T, bool>::value) {
        if (document[member_name].IsBool()) {
            return document[member_name].GetBool();
        } else if (check_type) {
            error = fmt::format("Invalid type for {0}", member_name);
        }
    } else if constexpr (std::is_same<T, std::map<std::string, std::string>>::value) {
        if (document[member_name].IsObject()) {
            std::map<std::string, std::string> result;
            for (auto const &[key, value] : document[member_name].GetObject()) {
                if (!value.IsString()) {
                    error = fmt::format("Invalid type for member {0}", member_name);
                    return std::nullopt;
                }
                result.emplace(key.GetString(), value.GetString());
            }
            return result;
        } else {
            error = fmt::format("Invalid type for {0}", member_name);
        }
    }
    return std::nullopt;
}

std::string to_string(RequestType type) noexcept {
    switch (type) {
        case RequestType::error:
            return "error";
        case RequestType::breakpoint:
            return "breakpoint";
        case RequestType::breakpoint_id:
            return "breakpoint-id";
        case RequestType::connection:
            return "connection";
        case RequestType::bp_location:
            return "bp-location";
        case RequestType::command:
            return "command";
        case RequestType::debugger_info:
            return "debugger-info";
        case RequestType::path_mapping:
            return "path-mapping";
        case RequestType::evaluation:
            return "evaluation";
        case RequestType::option_change:
            return "option-change";
        case RequestType::monitor:
            return "monitor";
        case RequestType::set_value:
            return "set-value";
        case RequestType::symbol:
            return "symbol";
        case RequestType::data_breakpoint:
            return "data-breakpoint";
    }
    return "error";
}

GenericResponse::GenericResponse(status_code status, const Request &req, std::string reason)
    : GenericResponse(status, req.type(), std::move(reason)) {
    token_ = req.get_token();
}

GenericResponse::GenericResponse(status_code status, RequestType type, std::string reason)
    : Response(status), request_type_(to_string(type)), reason_(std::move(reason)) {}

template <typename T, typename K, typename A>
void set_member(K &json_value, A &allocator, const char *name, const T &value) {
    rapidjson::Value key(name, allocator);  // NOLINT
    if constexpr (std::is_same<T, std::string>::value) {
        rapidjson::Value v(value.c_str(), allocator);
        json_value.AddMember(key, v, allocator);
    } else if constexpr (std::is_integral<T>::value) {
        json_value.AddMember(key.Move(), value, allocator);
    } else if constexpr (std::is_same<T, rapidjson::Value>::value) {
        rapidjson::Value v_copy(value, allocator);
        json_value.AddMember(key.Move(), v_copy.Move(), allocator);
    } else if constexpr (std::is_same<T, std::map<std::string, std::string>>::value) {
        rapidjson::Value v(rapidjson::kObjectType);
        for (auto const &[n, value_] : value) {
            set_member(v, allocator, n.c_str(), value_);
        }
        json_value.AddMember(key.Move(), v.Move(), allocator);
    } else {
        throw std::runtime_error(fmt::format("Unknown type for {0}", name));
    }
}

template <typename T>
void set_member(rapidjson::Document &document, const char *name, const T &value) {
    auto &allocator = document.GetAllocator();
    set_member(document, allocator, name, value);
}

void set_status(rapidjson::Document &document, status_code status) {
    std::string status_str = status == status_code::success ? "success" : "error";
    set_member(document, "status", status_str);
}

std::string to_string(rapidjson::Document &document, bool pretty_print) {
    using namespace rapidjson;
    StringBuffer buffer;
    if (pretty_print) {
        PrettyWriter w(buffer);
        document.Accept(w);
    } else {
        Writer w(buffer);
        document.Accept(w);
    }
    const auto *s = buffer.GetString();
    return s;
}

void set_response_header(rapidjson::Document &document, const Response *response) {
    set_member(document, "request", false);
    set_member(document, "type", response->type());
    if (!response->token().empty()) {
        set_member(document, "token", response->token());
    }
}

std::string GenericResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    // payload
    Value payload(kObjectType);
    set_member(payload, allocator, "request-type", request_type_);

    if (status_ == status_code::error) [[unlikely]] {
        set_member(payload, allocator, "reason", reason_);
    }
    for (auto const &[name, value] : bool_values_) {
        set_member(payload, allocator, name.c_str(), value);
    }
    for (auto const &[name, value] : int_values_) {
        set_member(payload, allocator, name.c_str(), value);
    }
    for (auto const &[name, value] : string_values_) {
        set_member(payload, allocator, name.c_str(), value);
    }

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

std::string BreakPointLocationResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    // set an array of elements
    Value values(kArrayType);

    for (auto const &bp_p : bps_) {
        auto &bp = *bp_p;
        Value value(kObjectType);
        set_member(value, allocator, "id", bp.id);
        set_member(value, allocator, "filename", bp.filename);
        set_member(value, allocator, "line_num", bp.line_num);
        set_member(value, allocator, "column_num", bp.column_num);
        values.PushBack(value.Move(), allocator);
    }
    set_member(document, "payload", values);

    return to_string(document, pretty_print);
}

BreakPointResponse::BreakPointResponse(uint64_t time, std::string filename, uint64_t line_num,
                                       uint64_t column_num)
    : time_(time), filename_(std::move(filename)), line_num_(line_num), column_num_(column_num) {}

std::string BreakPointResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    Value payload(kObjectType);
    set_member(payload, allocator, "time", time_);
    set_member(payload, allocator, "filename", filename_);
    set_member(payload, allocator, "line_num", line_num_);
    set_member(payload, allocator, "column_num", column_num_);

    Value instances(kArrayType);
    for (auto const &scope : scopes_) {
        Value entry(kObjectType);

        set_member(entry, allocator, "instance_id", scope.instance_id);
        set_member(entry, allocator, "instance_name", scope.instance_name);
        set_member(entry, allocator, "breakpoint_id", scope.breakpoint_id);
        set_member(entry, allocator, "local", scope.local_values);
        set_member(entry, allocator, "generator", scope.generator_values);

        instances.PushBack(entry, allocator);
    }
    set_member(payload, allocator, "instances", instances);

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

BreakPointResponse::Scope::Scope(uint64_t instance_id, std::string instance_name,
                                 uint64_t breakpoint_id)
    : instance_id(instance_id),
      breakpoint_id(breakpoint_id),
      instance_name(std::move(instance_name)) {}

void BreakPointResponse::Scope::add_local_value(const std::string &name, const std::string &value) {
    local_values.emplace(name, value);
}

void BreakPointResponse::Scope::add_generator_value(const std::string &name,
                                                    const std::string &value) {
    generator_values.emplace(name, value);
}

DebuggerInformationResponse::DebuggerInformationResponse(std::vector<BreakPoint *> bps)
    : command_type_(DebuggerInformationRequest::CommandType::breakpoints), bps_(std::move(bps)) {}

DebuggerInformationResponse::DebuggerInformationResponse(std::string status)
    : command_type_(DebuggerInformationRequest::CommandType::status),
      status_str_(std::move(status)) {}

DebuggerInformationResponse::DebuggerInformationResponse(std::map<std::string, std::string> options)
    : command_type_(DebuggerInformationRequest::CommandType::options),
      options_(std::move(options)) {}

DebuggerInformationResponse::DebuggerInformationResponse(
    std::unordered_map<std::string, std::string> design)
    : command_type_(DebuggerInformationRequest::CommandType::design), design_(std::move(design)) {}

std::string DebuggerInformationResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);  // NOLINT
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    Value payload(kObjectType);
    set_member(payload, allocator, "command", get_command_str());

    switch (command_type_) {
        case DebuggerInformationRequest::CommandType::breakpoints: {
            Value array(kArrayType);
            for (auto *bp : bps_) {
                Value entry(kObjectType);
                set_member(entry, allocator, "id", bp->id);
                set_member(entry, allocator, "filename", bp->filename);
                set_member(entry, allocator, "line_num", bp->line_num);
                set_member(entry, allocator, "column_num", bp->column_num);
                array.PushBack(entry, allocator);
            }
            set_member(payload, allocator, "breakpoints", array);
            break;
        }
        case DebuggerInformationRequest::CommandType::options: {
            Value v(kObjectType);
            for (auto const &[key, value] : options_) {
                if (value == "true" || value == "false") {
                    set_member(v, allocator, key.c_str(), value == "true");
                } else if (std::all_of(value.begin(), value.end(), isdigit)) {
                    int64_t i = std::stoll(value);
                    set_member(v, allocator, key.c_str(), i);
                } else {
                    set_member(v, allocator, key.c_str(), value);
                }
            }
            set_member(payload, allocator, "options", v);
            break;
        }
        case DebuggerInformationRequest::CommandType::status: {
            set_member(payload, allocator, "status", status_str_);
            break;
        }
        case DebuggerInformationRequest::CommandType::design: {
            // make it ordered here
            auto design = std::map<std::string, std::string>(design_.begin(), design_.end());
            set_member(payload, allocator, "design", design);
            break;
        }
    }

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

std::string DebuggerInformationResponse::get_command_str() const {
    switch (command_type_) {
        case DebuggerInformationRequest::CommandType::breakpoints: {
            return "breakpoints";
        }
        case DebuggerInformationRequest::CommandType::status: {
            return "status";
        }
        case DebuggerInformationRequest::CommandType::options: {
            return "options";
        }
        case DebuggerInformationRequest::CommandType::design: {
            return "design";
        }
    }
    return "";
}

EvaluationResponse::EvaluationResponse(std::string scope, std::string result)
    : scope_(std::move(scope)), result_(std::move(result)) {}

std::string EvaluationResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);  // NOLINT
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    Value payload(kObjectType);
    set_member(payload, allocator, "scope", scope_);
    set_member(payload, allocator, "result", result_);

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

MonitorResponse::MonitorResponse(uint64_t track_id, std::string value)
    : track_id_(track_id), value_(std::move(value)) {}

std::string MonitorResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);  // NOLINT
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    Value payload(kObjectType);
    set_member(payload, allocator, "track_id", track_id_);
    set_member(payload, allocator, "value", value_);

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

std::unique_ptr<Request> Request::parse_request(const std::string &str) {
    using namespace rapidjson;
    Document document;
    document.Parse(str.c_str());

    if (document.HasParseError()) return std::make_unique<ErrorRequest>("Invalid json object");

    std::string error;
    auto request = get_member<bool>(document, "request", error);
    if (!request || !(*request)) return std::make_unique<ErrorRequest>(error);

    auto type = get_member<std::string>(document, "type", error);
    if (!type) return std::make_unique<ErrorRequest>(error);
    // get payload
    auto payload = check_member(document, "payload", error);
    if (!payload) return std::make_unique<ErrorRequest>(error);
    // get token
    auto token = get_member<std::string>(document, "token", error, false, true);

    auto const &type_str = *type;
    std::unique_ptr<Request> result;
    if (type_str == "breakpoint") {
        result = std::make_unique<BreakPointRequest>();
    } else if (type_str == "breakpoint-id") {
        result = std::make_unique<BreakPointIDRequest>();
    } else if (type_str == "connection") {
        result = std::make_unique<ConnectionRequest>();
    } else if (type_str == "bp-location") {
        result = std::make_unique<BreakPointLocationRequest>();
    } else if (type_str == "command") {
        result = std::make_unique<CommandRequest>();
    } else if (type_str == "debugger-info") {
        result = std::make_unique<DebuggerInformationRequest>();
    } else if (type_str == "path-mapping") {
        result = std::make_unique<PathMappingRequest>();
    } else if (type_str == "evaluation") {
        result = std::make_unique<EvaluationRequest>();
    } else if (type_str == "option-change") {
        result = std::make_unique<OptionChangeRequest>();
    } else if (type_str == "monitor") {
        result = std::make_unique<MonitorRequest>();
    } else if (type_str == "set-value") {
        result = std::make_unique<SetValueRequest>();
    } else if (type_str == "data-breakpoint") {
        result = std::make_unique<DataBreakpointRequest>();
    } else {
        result = std::make_unique<ErrorRequest>("Unknown request");
    }

    // get payload string
    auto &payload_member = document["payload"];
    // notice that we str it again. ideally we shouldn't, but since the json parser is
    // fast, it should not be a major concern for our use cases
    StringBuffer buffer;
    Writer writer(buffer);
    payload_member.Accept(writer);
    std::string payload_str = buffer.GetString();
    result->parse_payload(payload_str);
    // set token if not empty
    if (token) result->token_ = *token;

    return result;
}

bool check_json(rapidjson::Document &document, status_code &status, std::string &error_reason) {
    if (document.HasParseError()) {
        status = status_code::error;
        error_reason = "Invalid JSON file";
        return false;
    }
    return true;
}

void BreakPointRequest::parse_payload(const std::string &payload) {
    // parse the breakpoint based on the API specification
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto filename = get_member<std::string>(document, "filename", error_reason_);
    auto bp_act = get_member<std::string>(document, "action", error_reason_);
    if (!filename || !bp_act) {
        status_code_ = status_code::error;
        return;
    }

    bp_ = BreakPoint{};
    bp_.filename = *filename;
    auto action_str = *bp_act;
    if (action_str == "add") {
        bp_action_ = action::add;
    } else if (action_str == "remove") {
        bp_action_ = action::remove;
    } else {
        status_code_ = status_code::error;
        return;
    }

    bool line_num_required = bp_action_ == action::add;
    auto line_num = get_member<uint64_t>(document, "line_num", error_reason_, line_num_required);
    if (line_num_required || line_num)
        bp_.line_num = *line_num;
    else
        bp_.line_num = 0;

    auto column_num = get_member<uint64_t>(document, "column_num", error_reason_, false);
    auto condition = get_member<std::string>(document, "condition", error_reason_, false);
    if (column_num)
        bp_.column_num = *column_num;
    else
        bp_.column_num = 0;
    if (condition) bp_.condition = *condition;
}

void BreakPointIDRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto id = get_member<uint64_t>(document, "id", error_reason_);
    auto bp_act = get_member<std::string>(document, "action", error_reason_);
    if (!id || !bp_act) {
        status_code_ = status_code::error;
        return;
    }

    bp_ = BreakPoint{};
    bp_.id = *id;
    auto action_str = *bp_act;
    if (action_str == "add") {
        bp_action_ = action::add;
    } else if (action_str == "remove") {
        bp_action_ = action::remove;
    } else {
        status_code_ = status_code::error;
        return;
    }

    auto condition = get_member<std::string>(document, "condition", error_reason_, false);
    if (condition) bp_.condition = *condition;
}

ErrorRequest::ErrorRequest(std::string reason) {
    status_code_ = status_code::error;
    error_reason_ = std::move(reason);
}

void ConnectionRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto db = get_member<std::string>(document, "db_filename", error_reason_);
    if (!db) {
        status_code_ = status_code::error;
        return;
    }
    db_filename_ = *db;

    // get optional mapping
    auto mapping = get_member<std::map<std::string, std::string>>(document, "path-mapping",
                                                                  error_reason_, false);
    if (mapping) {
        path_mapping_ = *mapping;
    }
}

void BreakPointLocationRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto filename = get_member<std::string>(document, "filename", error_reason_);
    if (!filename) {
        status_code_ = status_code::error;
        return;
    }
    filename_ = *filename;

    line_num_ = get_member<uint64_t>(document, "line_num", error_reason_);
    column_num_ = get_member<uint64_t>(document, "column_num", error_reason_);
}

void CommandRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto command_str = get_member<std::string>(document, "command", error_reason_);
    if (!command_str) {
        status_code_ = status_code::error;
        return;
    }
    auto const &command = *command_str;
    if (command == "continue") [[likely]] {
        command_type_ = CommandType::continue_;
    } else if (command == "step_over") {
        command_type_ = CommandType::step_over;
    } else if (command == "stop") {
        command_type_ = CommandType::stop;
    } else if (command == "step_back") {
        command_type_ = CommandType::step_back;
    } else if (command == "reverse_continue") {
        command_type_ = CommandType::reverse_continue;
    } else if (command == "jump") {
        command_type_ = CommandType::jump;
        // need to get time
        auto time = get_member<uint64_t>(document, "time", error_reason_);
        if (!time) {
            status_code_ = status_code::error;
            error_reason_ = "Unable to obtain jump time";
            return;
        }
        time_ = *time;
    } else {
        status_code_ = status_code::error;
        error_reason_ = "Unknown command type " + command;
    }
}

void DebuggerInformationRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto command_str = get_member<std::string>(document, "command", error_reason_);
    if (!command_str) {
        status_code_ = status_code::error;
        return;
    }

    auto const &command = *command_str;
    if (command == "breakpoints") {
        command_type_ = CommandType::breakpoints;
    } else if (command == "status") {
        command_type_ = CommandType::status;
    } else if (command == "options") {
        command_type_ = CommandType::options;
    } else if (command == "design") {
        command_type_ = CommandType::design;
    } else {
        status_code_ = status_code::error;
        error_reason_ = "Unknown command type " + command;
    }
}

void PathMappingRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto mapping =
        get_member<std::map<std::string, std::string>>(document, "path-mapping", error_reason_);
    if (!mapping) {
        status_code_ = status_code::error;
        return;
    }

    path_mapping_ = *mapping;
}

void EvaluationRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto scope = get_member<std::string>(document, "scope", error_reason_);
    auto expression = get_member<std::string>(document, "expression", error_reason_);
    auto is_context = get_member<bool>(document, "is_context", error_reason_);
    if (!scope || !expression || !is_context) {
        status_code_ = status_code::error;
        return;
    }
    scope_ = *scope;
    expression_ = *expression;
    is_context_ = *is_context;
}

void OptionChangeRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    // need to loop through the map by hand
    for (auto const &option : document.GetObject()) {
        std::string name = option.name.GetString();
        auto const &json_value = option.value;
        if (json_value.IsBool()) {
            bool_values_.emplace(name, json_value.GetBool());
        } else if (json_value.IsInt64()) {
            int_values_.emplace(name, json_value.GetInt64());
        } else if (json_value.IsString()) {
            str_values_.emplace(name, json_value.GetString());
        } else {
            error_reason_ = "Unsupported data type for " + name;
            status_code_ = status_code::error;
            break;
        }
    }
}

void MonitorRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto action_type = get_member<std::string>(document, "action_type", error_reason_);
    if (!action_type) {
        status_code_ = status_code::error;
        return;
    }

    if (*action_type == "add") {
        action_type_ = ActionType::add;
    } else if (*action_type == "remove") {
        action_type_ = ActionType::remove;
    } else {
        error_reason_ = "Unknown action type " + *action_type;
        return;
    }

    if (action_type_ == ActionType::add) {
        auto monitor_type = get_member<std::string>(document, "monitor_type", error_reason_);
        if (!monitor_type) {
            status_code_ = status_code::error;
            return;
        }

        if (*monitor_type == "breakpoint") {
            monitor_type_ = MonitorType::breakpoint;
        } else if (*monitor_type == "clock_edge") {
            monitor_type_ = MonitorType::clock_edge;
        } else {
            status_code_ = status_code::error;
            return;
        }

        auto name_ = get_member<std::string>(document, "var_name", error_reason_);
        if (!name_) {
            status_code_ = status_code::error;
            return;
        }
        var_name_ = *name_;

        instance_id_ = get_member<uint64_t>(document, "instance_id", error_reason_, false);
        breakpoint_id_ = get_member<uint64_t>(document, "breakpoint_id", error_reason_, false);
    } else {
        // only track_id is required
        auto track_id = get_member<uint64_t>(document, "track_id", error_reason_);
        if (!track_id) {
            status_code_ = status_code::error;
            return;
        }
        track_id_ = *track_id;
    }
}

void SetValueRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto variable_name = get_member<std::string>(document, "var_name", error_reason_);
    auto v = get_member<int64_t>(document, "value", error_reason_);

    if (!variable_name || !v) {
        status_code_ = status_code::error;
        return;
    }
    var_name_ = *variable_name;
    value_ = *v;

    // optional values
    instance_id_ = get_member<uint64_t>(document, "instance_id", error_reason_, false);
    breakpoint_id_ = get_member<uint64_t>(document, "breakpoint_id", error_reason_, false);
}

std::string to_string(SymbolRequest::request_type type) {
    switch (type) {
        case SymbolRequest::request_type::get_breakpoint:
            return "get_breakpoint";
        case SymbolRequest::request_type::get_breakpoints:
            return "get_breakpoints";
        case SymbolRequest::request_type::get_instance_name:
            return "get_instance_name";
        case SymbolRequest::request_type::get_instance_name_from_bp:
            return "get_instance_name_from_bp";
        case SymbolRequest::request_type::get_instance_id:
            return "get_instance_id";
        case SymbolRequest::request_type::get_context_variables:
            return "get_context_variables";
        case SymbolRequest::request_type::get_generator_variables:
            return "get_generator_variables";
        case SymbolRequest::request_type::get_instance_names:
            return "get_instance_names";
        case SymbolRequest::request_type::get_annotation_values:
            return "get_annotation_values";
        case SymbolRequest::request_type::get_context_static_values:
            return "get_context_static_values";
        case SymbolRequest::request_type::get_all_array_names:
            return "get_all_array_names";
        case SymbolRequest::request_type::resolve_scoped_name_breakpoint:
            return "resolve_scoped_name_breakpoint";
        case SymbolRequest::request_type::resolve_scoped_name_instance:
            return "resolve_scoped_name_instance";
        case SymbolRequest::request_type::get_execution_bp_orders:
            return "get_execution_bp_orders";
    }
    throw std::runtime_error("Invalid request type");
}

std::string SymbolRequest::str() const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);  // NOLINT
    auto &allocator = document.GetAllocator();
    set_member(document, "request", true);
    set_member(document, "type", std::string("symbol"));

    Value payload(kObjectType);

    set_member(payload, allocator, "type", to_string(req_type_));

    switch (req_type_) {
        case request_type::get_breakpoints:
            set_member(payload, allocator, "filename", filename);
            set_member(payload, allocator, "line_num", line_num);
            set_member(payload, allocator, "col_num", column_num);
            break;
        case request_type::get_instance_name:
        case request_type::get_generator_variables:
            set_member(payload, allocator, "instance_id", instance_id);
            break;
        case request_type::get_breakpoint:
        case request_type::get_instance_name_from_bp:
        case request_type::get_context_variables:
        case request_type ::get_context_static_values:
            set_member(payload, allocator, "breakpoint_id", breakpoint_id);
            break;
        case request_type::get_instance_id: {
            if (instance_name.empty()) {
                set_member(payload, allocator, "breakpoint_id", breakpoint_id);
            } else {
                set_member(payload, allocator, "instance_name", instance_name);
            }
            break;
        }
        case request_type::get_instance_names:
        case request_type::get_all_array_names:
        case request_type::get_execution_bp_orders:
            // nothing
            break;
        case request_type::get_annotation_values:
            set_member(payload, allocator, "name", name);
            break;
        case request_type::resolve_scoped_name_breakpoint:
            set_member(payload, allocator, "scoped_name", scoped_name);
            set_member(payload, allocator, "breakpoint_id", breakpoint_id);
            break;
        case request_type::resolve_scoped_name_instance:
            set_member(payload, allocator, "scoped_name", scoped_name);
            set_member(payload, allocator, "instance_id", instance_id);
            break;
    }
    set_member(document, "payload", payload);
    return to_string(document, false);
}

void DataBreakpointRequest::parse_payload(const std::string &payload) {
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());
    if (!check_json(document, status_code_, error_reason_)) return;

    auto action = get_member<std::string>(document, "action", error_reason_);
    if (!action) {
        status_code_ = status_code::error;
        error_reason_ = "action not defined";
        return;
    }

    if (action == "clear") {
        action_ = Action::clear;
    } else {
        if (action != "add") {
            status_code_ = status_code::error;
            error_reason_ = "Only 'add' or 'clear' allowed";
            return;
        }

        auto var_name_opt = get_member<std::string>(document, "var_name", error_reason_);
        auto bp_id_opt = get_member<uint64_t>(document, "breakpoint_id", error_reason_);
        if (!bp_id_opt || !var_name_opt) {
            status_code_ = status_code::error;
            return;
        }
        variable_name_ = *var_name_opt;
        breakpoint_id_ = *bp_id_opt;
    }
}

template <typename T>
bool get_value(const rapidjson::Value &value, const char *name, T &str) {
    std::string error;
    auto v = get_member<T>(value, name, error, false, true);
    if (v) str = *v;
    return v.has_value();
}

std::optional<BreakPoint> parse_breakpoint(const rapidjson::Value &value) {
    BreakPoint bp;
    if (!get_value(value, "id", bp.id)) return std::nullopt;

    uint32_t instance_id;
    if (!get_value(value, "instance_id", instance_id)) return std::nullopt;
    bp.instance_id = std::make_unique<uint32_t>(instance_id);

    if (!get_value(value, "filename", bp.filename)) return std::nullopt;

    if (!get_value(value, "line_num", bp.line_num)) return std::nullopt;

    if (!get_value(value, "column_num", bp.column_num)) return std::nullopt;

    if (!get_value(value, "condition", bp.condition)) return std::nullopt;

    if (!get_value(value, "trigger", bp.trigger)) return std::nullopt;

    return std::move(bp);
}

std::optional<Variable> parse_variable(const rapidjson::Value &value) {
    Variable v;

    if (!get_value(value, "id", v.id)) return std::nullopt;
    if (!get_value(value, "value", v.value)) return std::nullopt;
    if (!get_value(value, "is_rtl", v.is_rtl)) return std::nullopt;

    return std::move(v);
}

std::optional<ContextVariable> parse_context_variable(const rapidjson::Value &value) {
    ContextVariable v;

    if (!get_value(value, "name", v.name)) return std::nullopt;
    uint32_t id;
    if (!get_value(value, "breakpoint_id", id)) return std::nullopt;
    v.breakpoint_id = std::make_unique<uint32_t>(id);
    if (!get_value(value, "variable_id", id)) return std::nullopt;
    v.variable_id = std::make_unique<uint32_t>(id);

    return std::move(v);
}

std::optional<GeneratorVariable> parse_generator_variable(const rapidjson::Value &value) {
    GeneratorVariable v;

    if (!get_value(value, "name", v.name)) return std::nullopt;
    uint32_t id;
    if (!get_value(value, "instance_id", id)) return std::nullopt;
    v.instance_id = std::make_unique<uint32_t>(id);
    if (!get_value(value, "variable_id", id)) return std::nullopt;
    v.variable_id = std::make_unique<uint32_t>(id);
    // annotation is optional
    get_value(value, "annotation", v.annotation);

    return std::move(v);
}

// NOLINTNEXTLINE
void SymbolResponse::parse(const std::string &str) {
    using namespace rapidjson;
    Document document;
    document.Parse(str.c_str());
    if (document.HasParseError()) return;

    if (!document.HasMember("result")) return;
    auto const &result = document["result"];

    // we ignore type since it's a single thread model
    switch (type_) {
        case SymbolRequest::request_type::get_breakpoint: {
            if (!result.IsObject()) return;
            auto bp = parse_breakpoint(result);
            bp_result = std::move(bp);
            break;
        }
        case SymbolRequest::request_type::get_breakpoints: {
            if (!result.IsArray()) return;
            for (auto const &entry : result.GetArray()) {
                auto bp = parse_breakpoint(entry);
                if (bp) {
                    bp_results.emplace_back(std::move(*bp));
                }
            }
            break;
        }
        case SymbolRequest::request_type::resolve_scoped_name_breakpoint:
        case SymbolRequest::request_type::resolve_scoped_name_instance:
        case SymbolRequest::request_type::get_instance_name:
        case SymbolRequest::request_type::get_instance_name_from_bp: {
            if (!result.IsString()) return;
            str_result = result.GetString();
            break;
        }
        case SymbolRequest::request_type::get_instance_id: {
            if (!result.IsNumber()) return;
            uint64_t_result = result.GetUint64();
            break;
        }
        case SymbolRequest::request_type::get_context_variables: {
            if (!result.IsArray()) return;
            for (auto const &entry : result.GetArray()) {
                if (!entry.IsArray()) return;
                if (entry.GetArray().Size() != 2) return;
                auto const &c = entry[0];
                auto const &v = entry[1];
                auto c_opt = parse_context_variable(c);
                if (!c_opt) return;
                auto v_opt = parse_variable(v);
                if (!v_opt) return;
                context_vars_result.emplace_back(std::make_pair(std::move(*c_opt), *v_opt));
            }
            break;
        }
        case SymbolRequest::request_type::get_generator_variables: {
            if (!result.IsArray()) return;
            for (auto const &entry : result.GetArray()) {
                if (!entry.IsArray()) return;
                if (entry.GetArray().Size() != 2) return;
                auto const &g = entry[0];
                auto const &v = entry[1];
                auto g_opt = parse_generator_variable(g);
                if (!g_opt) return;
                auto v_opt = parse_variable(v);
                if (!v_opt) return;
                gen_vars_result.emplace_back(std::make_pair(std::move(*g_opt), *v_opt));
            }
            break;
        }
        case SymbolRequest::request_type::get_all_array_names:
        case SymbolRequest::request_type::get_annotation_values:
        case SymbolRequest::request_type::get_instance_names: {
            if (!result.IsArray()) return;
            for (auto const &entry : result.GetArray()) {
                if (!entry.IsString()) return;
                str_results.emplace_back(entry.GetString());
            }
            break;
        }
        case SymbolRequest::request_type::get_execution_bp_orders: {
            if (!result.IsArray()) return;
            for (auto const &entry : result.GetArray()) {
                if (!entry.IsNumber()) return;
                uint64_t_results.emplace_back(entry.GetUint64());
            }
            break;
        }
        case SymbolRequest::request_type::get_context_static_values: {
            if (result.IsObject()) return;
            for (auto const &[name, value] : result.GetObject()) {
                if (!value.IsNumber()) return;
                auto v = value.GetInt64();
                map_result.emplace(name.GetString(), v);
            }
        }
    }
}

}  // namespace hgdb
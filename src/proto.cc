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
 *     line_num: [required] - uint64_t
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
 *     path_mapping: [optional] - map<string, string>
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
 * Generic Response
 * type: generic
 * payload:
 *     request-type: string
 *     reason: string [only exists if error]
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
 *     filename: string
 *     line_num: uint64_t
 *     column_num: uint64_t
 *     values:
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
 */

static bool check_member(rapidjson::Document &document, const char *member_name, std::string &error,
                         bool set_error = true) {
    if (!document.HasMember(member_name)) {
        if (set_error) error = fmt::format("Unable to find member {0}", member_name);
        return false;
    }
    return true;
}

template <typename T>
static std::optional<T> get_member(rapidjson::Document &document, const char *member_name,
                                   std::string &error, bool set_error = true,
                                   bool check_type = true) {
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

GenericResponse::GenericResponse(status_code status, const Request &req, std::string reason)
    : GenericResponse(status, req.type(), std::move(reason)) {}

GenericResponse::GenericResponse(status_code status, RequestType type, std::string reason)
    : Response(status), reason_(std::move(reason)) {
    switch (type) {
        case RequestType::error:
            request_type_ = "error";
            break;
        case RequestType::breakpoint:
            request_type_ = "breakpoint";
            break;
        case RequestType::breakpoint_id:
            request_type_ = "breakpoint-id";
            break;
        case RequestType::connection:
            request_type_ = "connection";
            break;
        case RequestType::bp_location:
            request_type_ = "bp_location";
            break;
        case RequestType::command:
            request_type_ = "command";
            break;
        case RequestType::debugger_info:
            request_type_ = "debugger_info";
            break;
    }
}

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
        for (auto const &[name, value_] : value) {
            set_member(v, allocator, name.c_str(), value_);
        }
        json_value.AddMember(key.Move(), v.Move(), allocator);
    } else {
        throw std::runtime_error(fmt::format("Unable type for {0}", name));
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

    if (status_ == status_code::error) [[unlikely]] {  // NOLINT
        set_member(payload, allocator, "reason", reason_);
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

    Value values(kObjectType);
    // set the local and generator values
    set_member(values, allocator, "local", local_values_);
    set_member(values, allocator, "generator", generator_values_);
    set_member(payload, allocator, "values", values);

    set_member(document, "payload", payload);

    return to_string(document, pretty_print);
}

void BreakPointResponse::add_local_value(const std::string &name, const std::string &value) {
    local_values_.emplace(name, value);
}

void BreakPointResponse::add_generator_value(const std::string &name, const std::string &value) {
    generator_values_.emplace(name, value);
}

DebuggerInformationResponse::DebuggerInformationResponse(std::vector<BreakPoint *> bps)
    : command_type_(DebuggerInformationRequest::CommandType::breakpoints), bps_(std::move(bps)) {}

std::string DebuggerInformationResponse::str(bool pretty_print) const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);  // NOLINT
    auto &allocator = document.GetAllocator();
    set_response_header(document, this);
    set_status(document, status_);

    Value payload(kObjectType);
    set_member(payload, allocator, "command", get_command_str());

    if (command_type_ == DebuggerInformationRequest::CommandType::breakpoints) {
        Value array(kArrayType);
        for (auto *bp : bps_) {
            Value entry(kObjectType);
            set_member(entry, allocator, "filename", bp->filename);
            set_member(entry, allocator, "line_num", bp->line_num);
            set_member(entry, allocator, "column_num", bp->column_num);
            array.PushBack(entry, allocator);
        }
        set_member(payload, allocator, "breakpoints", array);
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
        default:
            return "";
    }
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
    } else {
        result = std::make_unique<ErrorRequest>("Unknown request");
    }

    // get payload string
    auto &payload_member = document["payload"];
    // notice that we serialize it again. ideally we shouldn't, but since the json parser is
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
    auto line_num = get_member<uint64_t>(document, "line_num", error_reason_);
    auto bp_act = get_member<std::string>(document, "action", error_reason_);
    if (!filename || !line_num || !bp_act) {
        status_code_ = status_code::error;
        return;
    }
    bp_ = BreakPoint{};
    bp_.filename = *filename;
    bp_.line_num = *line_num;
    auto action_str = *bp_act;
    if (action_str == "add") {
        bp_action_ = action::add;
    } else if (action_str == "remove") {
        bp_action_ = action::remove;
    } else {
        status_code_ = status_code::error;
        return;
    }
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
    auto mapping =
        get_member<std::map<std::string, std::string>>(document, "path_mapping", error_reason_);
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
    if (command == "continue") [[likely]] {  // NOLINT
        command_type_ = CommandType::continue_;
    } else if (command == "step_over") {
        command_type_ = CommandType::step_over;
    } else if (command == "stop") {
        command_type_ = CommandType::stop;
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
    } else {
        status_code_ = status_code::error;
        error_reason_ = "Unknown command type " + command;
    }
}

}  // namespace hgdb
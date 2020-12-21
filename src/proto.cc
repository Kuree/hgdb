#include "proto.hh"

#include <fmt/format.h>

#include <utility>

#include "rapidjson/document.h"
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
 *
 * Response structure
 * request: false
 * type: [required] - string
 * payload: [required] - object
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
    }
    return std::nullopt;
}

GenericResponse::GenericResponse(status_code status, std::string reason)
    : Response(status), reason_(std::move(reason)) {}

template <typename T>
void set_member(rapidjson::Document &document, const char *name, const T &value) {
    auto &allocator = document.GetAllocator();
    rapidjson::Value key(name, allocator);
    if constexpr (std::is_same<T, std::string>::value) {
        rapidjson::Value v(value.c_str(), allocator);
        document.AddMember(key, v, allocator);
    } else {
        document.AddMember(name, value, allocator);
    }
}

void set_status(rapidjson::Document &document, status_code status) {
    std::string status_str = status == status_code::success ? "success" : "error";
    set_member(document, "status", status_str);
}

std::string GenericResponse::str() const {
    using namespace rapidjson;
    Document document(rapidjson::kObjectType);
    auto &allocator = document.GetAllocator();
    set_status(document, status_);
    if (status_ == status_code::error) {
        set_member(document, "reason", reason_);
    }

    StringBuffer buffer;
    Writer w(buffer);
    document.Accept(w);
    auto s = buffer.GetString();
    return s;
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

    auto const &type_str = *type;
    std::unique_ptr<Request> result;
    if (type_str == "breakpoint") {
        result = std::make_unique<BreakpointRequest>();
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
    return result;
}

void BreakpointRequest::parse_payload(const std::string &payload) {
    // parse the breakpoint based on the API specification
    // we use linux style error handling logic
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());

    auto filename = get_member<std::string>(document, "filename", error_reason_);
    auto line_num = get_member<uint64_t>(document, "line_num", error_reason_);
    auto bp_act = get_member<std::string>(document, "action", error_reason_);
    if (!filename || !line_num || !bp_act) {
        status_code_ = status_code::error;
        return;
    }
    bp_ = BreakPoint{};
    bp_->filename = *filename;
    bp_->line_num = *line_num;
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
        bp_->column_num = *column_num;
    else
        bp_->column_num = 0;
    if (condition)
        bp_->condition = *condition;
    else
        bp_->condition = "";
}

ErrorRequest::ErrorRequest(std::string reason) {
    status_code_ = status_code::error;
    error_reason_ = std::move(reason);
}

}  // namespace hgdb
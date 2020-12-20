#include "proto.hh"

#include <fmt/format.h>
#include <rapidjson/document.h>

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
 * payload:
 *     filename: [required] - string
 *     line_num: [required] - uint64_t
 *     column_num: [optional] - uint64_t
 *     condition: [optional] - string
 *
 */

BreakpointRequest::BreakpointRequest(const std::string &payload) : Request() {
    // parse the breakpoint based on the API specification
    // we use linux style error handling logic
    using namespace rapidjson;
    Document document;
    document.Parse(payload.c_str());

    auto filename = get_member<Document, std::string>(document, "filename");
    auto line_num = get_member<Document, uint64_t>(document, "line_num");
    if (!filename || !line_num) {
        status_code_ = status_code::error;
        return;
    }
    bp_ = BreakPoint{};
    bp_->filename = *filename;
    bp_->line_num = *line_num;
    auto column_num = get_member<Document, uint64_t>(document, "column_num", false);
    auto condition = get_member<Document, std::string>(document, "condition", false);
    if (column_num)
        bp_->column_num = *column_num;
    else
        bp_->column_num = 0;
    if (condition)
        bp_->condition = *condition;
    else
        bp_->condition = "";
}

}  // namespace hgdb
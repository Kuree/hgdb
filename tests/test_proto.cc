#include "../src/proto.hh"
#include "gtest/gtest.h"

TEST(proto, token_passing) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "breakpoint",
    "token": "TEST_TOKEN",
    "payload": {
        "filename": "/tmp/abc",
        "line_num": 123,
        "action": "add"
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    auto resp = hgdb::GenericResponse(hgdb::status_code::success, hgdb::RequestType::breakpoint);
    r->set_token(resp);
    auto s = resp.str(false);
    EXPECT_EQ(s, R"({"request":false,"type":"generic","token":"TEST_TOKEN",)"
                 R"("status":"success","payload":{"request-type":"breakpoint"}})");
}

TEST(proto, breakpoint_request) {  // NOLINT
    const auto *req = R"(
{
    "filename": "/tmp/abc",
    "line_num": 123,
    "action": "add",
    "column_num": 42,
    "condition": "a"
}
)";
    hgdb::BreakPointRequest r;
    r.parse_payload(req);
    EXPECT_EQ(r.status(), hgdb::status_code::success);
    auto const &bp = r.breakpoint();
    EXPECT_EQ(bp.filename, "/tmp/abc");
    EXPECT_EQ(bp.line_num, 123);
    EXPECT_EQ(bp.column_num, 42);
    EXPECT_EQ(bp.condition, "a");
    EXPECT_EQ(r.bp_action(), hgdb::BreakPointRequest::action::add);
}

TEST(proto, breakpoint_request_remove_no_line_num) {  // NOLINT
    const auto *req = R"(
{
    "filename": "/tmp/abc",
    "action": "remove",
    "column_num": 42
}
)";
    hgdb::BreakPointRequest r;
    r.parse_payload(req);
    EXPECT_EQ(r.status(), hgdb::status_code::success);
    auto const &bp = r.breakpoint();
    EXPECT_EQ(bp.filename, "/tmp/abc");
    EXPECT_EQ(r.bp_action(), hgdb::BreakPointRequest::action::remove);
}

TEST(proto, breakpoint_id_request) {  // NOLINT
    const auto *req = R"(
{
    "id": 42,
    "action": "add",
    "condition": "a"
}
)";
    hgdb::BreakPointIDRequest r;
    r.parse_payload(req);
    EXPECT_EQ(r.status(), hgdb::status_code::success);
    auto const &bp = r.breakpoint();
    EXPECT_EQ(bp.id, 42);
    EXPECT_EQ(bp.condition, "a");
    EXPECT_EQ(r.bp_action(), hgdb::BreakPointRequest::action::add);
}

TEST(proto, breakpoint_request_malformed) {  // NOLINT
    const auto *req1 = R"(
{
    "line_num": 123,
    "column_num": 42,
    "action": "remove",
    "condition": "a"
}
)";

    const auto *req2 = R"(
{
    "filename": "/tmp/abc",
    "action": "remove_all",
}
)";
    hgdb::BreakPointRequest r;
    r.parse_payload(req1);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    r = {};
    r.parse_payload(req2);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    EXPECT_FALSE(r.error_reason().empty());
}

TEST(proto, request_parse_breakpoint) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "breakpoint",
    "payload": {
        "filename": "/tmp/abc",
        "line_num": 123,
        "action": "add"
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto *br = dynamic_cast<hgdb::BreakPointRequest *>(r.get());
    EXPECT_NE(br, nullptr);
    EXPECT_EQ(br->breakpoint().filename, "/tmp/abc");
}

TEST(proto, request_parse_connection) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "connection",
    "payload": {
        "db_filename": "/tmp/abc.db",
        "path-mapping": {
            "a": "/tmp/a",
            "b": "/tmp/b"
        }
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto *conn = dynamic_cast<hgdb::ConnectionRequest *>(r.get());
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(conn->db_filename(), "/tmp/abc.db");
    EXPECT_EQ(conn->path_mapping().size(), 2);
    EXPECT_EQ(conn->path_mapping().at("a"), "/tmp/a");
    EXPECT_EQ(conn->path_mapping().at("b"), "/tmp/b");
}

TEST(proto, request_parse_bp_location) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "bp-location",
    "payload": {
        "filename": "/tmp/abc",
        "line_num": 42
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto *bp = dynamic_cast<hgdb::BreakPointLocationRequest *>(r.get());
    EXPECT_NE(bp, nullptr);
    EXPECT_EQ(bp->filename(), "/tmp/abc");
    EXPECT_EQ(*bp->line_num(), 42);
    EXPECT_FALSE(bp->column_num());
}

TEST(proto, request_parse_command) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "command",
    "payload": {
        "command": "continue"
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto *bp = dynamic_cast<hgdb::CommandRequest *>(r.get());
    EXPECT_NE(bp, nullptr);
    EXPECT_EQ(bp->command_type(), hgdb::CommandRequest::CommandType::continue_);
}

TEST(proto, request_parse_debugger) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "debugger-info",
    "payload": {
        "command": "breakpoints"
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto *bp = dynamic_cast<hgdb::DebuggerInformationRequest *>(r.get());
    EXPECT_NE(bp, nullptr);
    EXPECT_EQ(bp->command_type(), hgdb::DebuggerInformationRequest::CommandType::breakpoints);
}

TEST(proto, request_parse_path_mapping) {  // NOLINT
    const auto *req = R"(
{
    "request": true,
    "type": "path-mapping",
    "payload": {
        "path-mapping": {
            "/tmp/a": "/workspace/a",
            "/tmp/b": "/workspace/b"
        }
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto const *bp = dynamic_cast<hgdb::PathMappingRequest *>(r.get());
    auto const &mapping = bp->path_mapping();
    EXPECT_EQ(mapping.size(), 2);
    EXPECT_EQ(mapping.at("/tmp/a"), "/workspace/a");
    EXPECT_EQ(mapping.at("/tmp/b"), "/workspace/b");
}

TEST(proto, request_parse_evaluation) {  // NOLINT
    const auto *req = R"({
    "request": true,
    "type": "evaluation",
    "payload": {
        "scope": "test.scope",
        "expression": "a + 1",
        "is_context": true
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto const *eval = dynamic_cast<hgdb::EvaluationRequest *>(r.get());
    EXPECT_EQ(eval->scope(), "test.scope");
    EXPECT_EQ(eval->expression(), "a + 1");
    EXPECT_TRUE(eval->is_context());
}

TEST(proto, request_parse_option_change) {  // NOLINT
    const auto *req = R"({
    "request": true,
    "type": "option-change",
    "payload": {
        "a": true,
        "b": 42,
        "c": "d"
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto const *options = dynamic_cast<hgdb::OptionChangeRequest *>(r.get());
    EXPECT_EQ(options->bool_values().at("a"), true);
    EXPECT_EQ(options->int_values().at("b"), 42);
    EXPECT_EQ(options->str_values().at("c"), "d");
}

TEST(proto, request_parse_monitor) {  // NOLINT
    const auto *req1 = R"({
    "request": true,
    "type": "monitor",
    "payload": {
        "action_type": "add",
        "monitor_type": "breakpoint",
        "scoped_name": "hgdb",
        "breakpoint_id": 42
    }
}
)";
    auto r = hgdb::Request::parse_request(req1);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto const *req = dynamic_cast<hgdb::MonitorRequest *>(r.get());
    EXPECT_EQ(req->scope_name(), "hgdb");
    EXPECT_EQ(*req->breakpoint_id(), 42);
    EXPECT_EQ(req->monitor_type(), hgdb::MonitorRequest::MonitorType::breakpoint);

    const auto *req2 = R"({
    "request": true,
    "type": "monitor",
    "payload": {
        "action_type": "add",
        "monitor_type": "clock_edge",
        "scoped_name": "hgdb",
        "instance_id": 42
    }
}
)";
    r = hgdb::Request::parse_request(req2);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    req = dynamic_cast<hgdb::MonitorRequest *>(r.get());
    EXPECT_EQ(req->scope_name(), "hgdb");
    EXPECT_EQ(*req->instance_id(), 42);

    // test remove
    const auto *req3 = R"({
    "request": true,
    "type": "monitor",
    "payload": {
        "action_type": "remove",
        "track_id": 42
    }
}
)";
    r = hgdb::Request::parse_request(req3);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    req = dynamic_cast<hgdb::MonitorRequest *>(r.get());
    EXPECT_EQ(req->action_type(), hgdb::MonitorRequest::ActionType::remove);
    EXPECT_EQ(req->track_id(), 42);

    // test out malformed requests
    const auto *req4 = R"({
    "request": true,
    "type": "monitor",
    "payload": {
        "action_type": "add",
        "monitor_type": "breakpoint",
        "scoped_name": "hgdb",
        "breakpoint_id": 42,
        "instance_id": 42
    }
}
)";
    r = hgdb::Request::parse_request(req4);
    EXPECT_EQ(r->status(), hgdb::status_code::error);

    const auto *req5 = R"({
    "request": true,
    "type": "monitor",
    "payload": {
        "action_type": "add",
        "monitor_type": "breakpoint",
        "scoped_name": "hgdb"
    }
}
)";
    r = hgdb::Request::parse_request(req5);
    EXPECT_EQ(r->status(), hgdb::status_code::error);
}

TEST(proto, generic_response) {  // NOLINT
    auto res =
        hgdb::GenericResponse(hgdb::status_code::error, hgdb::RequestType::error, "TEST_ERROR");
    auto s = res.str(false);
    EXPECT_EQ(s, R"({"request":false,"type":"generic","status":"error",)"
                 R"("payload":{"request-type":"error","reason":"TEST_ERROR"}})");
    res = hgdb::GenericResponse(hgdb::status_code::success, hgdb::RequestType::breakpoint);
    s = res.str(false);
    EXPECT_EQ(s, R"({"request":false,"type":"generic","status":"success",)"
                 R"("payload":{"request-type":"breakpoint"}})");
    // fields
    res = hgdb::GenericResponse(hgdb::status_code::success, hgdb::RequestType::monitor);
    res.set_value("a", false);
    res.set_value("b", 42);
    res.set_value("c", "42");
    s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "generic",
    "status": "success",
    "payload": {
        "request-type": "monitor",
        "a": false,
        "b": 42,
        "c": "42"
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, bp_location_response) {  // NOLINT
    std::vector<std::unique_ptr<hgdb::BreakPoint>> bps;
    for (auto i = 0; i < 2; i++) {
        auto bp = std::make_unique<hgdb::BreakPoint>();
        bp->filename = "/tmp/a";
        bp->line_num = i;
        bp->column_num = 0;
        bp->id = i;
        bps.emplace_back(std::move(bp));
    }
    std::vector<hgdb::BreakPoint *> values = {bps[0].get(), bps[1].get()};
    auto res = hgdb::BreakPointLocationResponse(values);
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "bp-location",
    "status": "success",
    "payload": [
        {
            "id": 0,
            "filename": "/tmp/a",
            "line_num": 0,
            "column_num": 0
        },
        {
            "id": 1,
            "filename": "/tmp/a",
            "line_num": 1,
            "column_num": 0
        }
    ]
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, breakpoint_response) {  // NOLINT
    auto res = hgdb::BreakPointResponse(1, "a", 2, 3);
    auto scope = hgdb::BreakPointResponse::Scope(42, "mod", 43);
    scope.add_generator_value("c", "4");
    scope.add_local_value("d", "5");
    res.add_scope(scope);
    auto s = res.str(true);
    printf("%s\n", s.c_str());
    constexpr auto expected_value = R"({
    "request": false,
    "type": "breakpoint",
    "status": "success",
    "payload": {
        "time": 1,
        "filename": "a",
        "line_num": 2,
        "column_num": 3,
        "instances": [
            {
                "instance_id": 42,
                "instance_name": "mod",
                "breakpoint_id": 43,
                "local": {
                    "d": "5"
                },
                "generator": {
                    "c": "4"
                }
            }
        ]
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, debugger_info_response_breakpoint) {  // NOLINT
    auto bp = hgdb::BreakPoint{.id = 42, .filename = "/tmp/a", .line_num = 1, .column_num = 1};
    std::vector<hgdb::BreakPoint *> bps = {&bp};
    auto res = hgdb::DebuggerInformationResponse(bps);
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "debugger-info",
    "status": "success",
    "payload": {
        "command": "breakpoints",
        "breakpoints": [
            {
                "id": 42,
                "filename": "/tmp/a",
                "line_num": 1,
                "column_num": 1
            }
        ]
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, debugger_info_response_options) {  // NOLINT
    std::map<std::string, std::string> options = {{"a", "true"}, {"b", "1"}, {"c", "d"}};
    auto res = hgdb::DebuggerInformationResponse(options);
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "debugger-info",
    "status": "success",
    "payload": {
        "command": "options",
        "options": {
            "a": true,
            "b": 1,
            "c": "d"
        }
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, debugger_info_response_status) {  // NOLINT
    auto res = hgdb::DebuggerInformationResponse("With great power comes great responsibility");
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "debugger-info",
    "status": "success",
    "payload": {
        "command": "status",
        "status": "With great power comes great responsibility"
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, evaluation_response) {  // NOLINT
    auto res = hgdb::EvaluationResponse("test.scope", "42");
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "evaluation",
    "status": "success",
    "payload": {
        "scope": "test.scope",
        "result": "42"
    }
})";
    EXPECT_EQ(s, expected_value);
}

TEST(proto, monitor_response) {  // NOLINT
    auto res = hgdb::MonitorResponse(42, "42");
    auto s = res.str(true);
    constexpr auto expected_value = R"({
    "request": false,
    "type": "monitor",
    "status": "success",
    "payload": {
        "track_id": 42,
        "value": "42"
    }
})";
    EXPECT_EQ(s, expected_value);
}
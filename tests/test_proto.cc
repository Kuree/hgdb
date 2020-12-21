#include "../src/proto.hh"
#include "gtest/gtest.h"

TEST(proto, breakpoint_request) { // NOLINT
    auto req = R"(
{
    "filename": "/tmp/abc",
    "line_num": 123,
    "action": "add",
    "column_num": 42,
    "condition": "a"
}
)";
    hgdb::BreakpointRequest r;
    r.parse_payload(req);
    EXPECT_EQ(r.status(), hgdb::status_code::success);
    auto const &bp = r.breakpoint();
    EXPECT_TRUE(bp);
    EXPECT_EQ(bp->filename, "/tmp/abc");
    EXPECT_EQ(bp->line_num, 123);
    EXPECT_EQ(bp->column_num, 42);
    EXPECT_EQ(bp->condition, "a");
    EXPECT_EQ(r.bp_action(), hgdb::BreakpointRequest::action::add);
}

TEST(proto, breakpoint_request_malformed) {  // NOLINT
    auto req1 = R"(
{
    "line_num": 123,
    "column_num": 42,
    "action": "remove",
    "condition": "a"
}
)";

    auto req2 = R"(
{
    "filename": "/tmp/abc",
    "action": "remove",
    "line_num": "123"
}
)";
    hgdb::BreakpointRequest r;
    r.parse_payload(req1);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    r = {};
    r.parse_payload(req2);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    EXPECT_FALSE(r.error_reason().empty());
}

TEST(proto, request_parse_breakpoint) { // NOLINT
    auto req = R"(
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
    auto br = dynamic_cast<hgdb::BreakpointRequest*>(r.get());
    EXPECT_NE(br, nullptr);
    EXPECT_EQ(br->breakpoint()->filename, "/tmp/abc");
}

TEST(proto, request_parse_connection) { // NOLINT
    auto req = R"(
{
    "request": true,
    "type": "connection",
    "payload": {
        "db_filename": "/tmp/abc.db",
        "path_mapping": {
            "a": "/tmp/a",
            "b": "/tmp/b"
        }
    }
}
)";
    auto r = hgdb::Request::parse_request(req);
    EXPECT_EQ(r->status(), hgdb::status_code::success);
    auto conn = dynamic_cast<hgdb::ConnectionRequest*>(r.get());
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(conn->db_filename(), "/tmp/abc.db");
    EXPECT_EQ(conn->path_mapping().size(), 2);
    EXPECT_EQ(conn->path_mapping().at("a"), "/tmp/a");
    EXPECT_EQ(conn->path_mapping().at("b"), "/tmp/b");
}

TEST(proto, generic_response) { // NOLINT
    auto res = hgdb::GenericResponse(hgdb::status_code::error, "TEST");
    auto s = res.str();
    EXPECT_EQ(s, R"({"status":"error","reason":"TEST"})");
    res = hgdb::GenericResponse(hgdb::status_code::success);
    s = res.str();
    EXPECT_EQ(s, R"({"status":"error","reason":"TEST"})");
}
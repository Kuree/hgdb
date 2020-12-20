#include "../src/proto.hh"
#include "gtest/gtest.h"

TEST(proto, breakpoint_request) { // NOLINT
    auto req = R"(
{
    "filename": "/tmp/abc",
    "line_num": 123,
    "column_num": 42,
    "condition": "a"
}
)";
    hgdb::BreakpointRequest r(req);
    EXPECT_EQ(r.status(), hgdb::status_code::success);
    auto const &bp = r.breakpoint();
    EXPECT_TRUE(bp);
    EXPECT_EQ(bp->filename, "/tmp/abc");
    EXPECT_EQ(bp->line_num, 123);
    EXPECT_EQ(bp->column_num, 42);
    EXPECT_EQ(bp->condition, "a");
}

TEST(proto, breakpoint_request_malformed) {  // NOLINT
    auto req1 = R"(
{
    "line_num": 123,
    "column_num": 42,
    "condition": "a"
}
)";

    auto req2 = R"(
{
    "filename": "/tmp/abc",
    "line_num": "123"
}
)";
    hgdb::BreakpointRequest r(req1);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    r = hgdb::BreakpointRequest(req2);
    EXPECT_EQ(r.status(), hgdb::status_code::error);
    EXPECT_FALSE(r.error_reason().empty());
}
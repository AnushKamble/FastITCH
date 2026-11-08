#include "fastitch/result.hpp"
#include "tests/support/test_framework.hpp"

using namespace fastitch;

TEST_CASE(ResultValueType) {
    Result<int> res(42);
    REQUIRE_TRUE(res.has_value());
    REQUIRE_TRUE(static_cast<bool>(res));
    REQUIRE_EQ(res.value(), 42);
}

TEST_CASE(ResultErrorType) {
    ParseError err{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, 100, 1, "test error"};
    Result<int> res(err);
    REQUIRE_FALSE(res.has_value());
    REQUIRE_FALSE(static_cast<bool>(res));
    REQUIRE_EQ(res.error().layer, SourceLayer::RawBytes);
    REQUIRE_EQ(res.error().code, ErrorCode::UnexpectedEndOfInput);
    REQUIRE_EQ(res.error().offset, 100ULL);
    REQUIRE_EQ(res.error().sequence, 1ULL);
    REQUIRE_EQ(res.error().message, "test error");
}

TEST_CASE(ResultVoidType) {
    Result<void> ok_res;
    REQUIRE_TRUE(ok_res.has_value());
    REQUIRE_TRUE(static_cast<bool>(ok_res));

    ParseError err{SourceLayer::OrderBook, ErrorCode::DuplicateOrderId, 50, 0, "duplicate order"};
    Result<void> err_res(err);
    REQUIRE_FALSE(err_res.has_value());
    REQUIRE_FALSE(static_cast<bool>(err_res));
    REQUIRE_EQ(err_res.error().code, ErrorCode::DuplicateOrderId);
}

TEST_CASE(ErrorStringRepresentations) {
    REQUIRE_EQ(to_string(SourceLayer::BinaryFile), "BinaryFile");
    REQUIRE_EQ(to_string(SourceLayer::MoldUDP64), "MoldUDP64");
    REQUIRE_EQ(to_string(ErrorCode::SequenceGap), "SequenceGap");
    REQUIRE_EQ(to_string(ErrorCode::Success), "Success");
}

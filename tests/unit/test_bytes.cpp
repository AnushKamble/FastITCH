#include "fastitch/bytes.hpp"
#include "fastitch/protocol.hpp"
#include "tests/support/test_framework.hpp"

#include <vector>

using namespace fastitch;

TEST_CASE(ByteReaderU8) {
    std::vector<std::byte> buf = {std::byte{0x41}, std::byte{0xFF}, std::byte{0x00}};
    std::span<const std::byte> span(buf);

    auto r1 = ByteReader::read_u8(span, 0);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), 0x41);

    auto r2 = ByteReader::read_u8(span, 1);
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(r2.value(), 0xFF);

    auto r3 = ByteReader::read_u8(span, 2);
    REQUIRE_TRUE(r3.has_value());
    REQUIRE_EQ(r3.value(), 0x00);

    auto r_out = ByteReader::read_u8(span, 3);
    REQUIRE_FALSE(r_out.has_value());
    REQUIRE_EQ(r_out.error().code, ErrorCode::UnexpectedEndOfInput);
}

TEST_CASE(ByteReaderBeU16) {
    // 0x1234 in big-endian: [0x12, 0x34]
    std::vector<std::byte> buf = {std::byte{0x12}, std::byte{0x34}, std::byte{0xFF}, std::byte{0xFE}};
    std::span<const std::byte> span(buf);

    auto r1 = ByteReader::read_be_u16(span, 0);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), 0x1234);

    auto r2 = ByteReader::read_be_u16(span, 2);
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(r2.value(), 0xFFFE);

    auto r_bound = ByteReader::read_be_u16(span, 3);
    REQUIRE_FALSE(r_bound.has_value());
    REQUIRE_EQ(r_bound.error().code, ErrorCode::UnexpectedEndOfInput);
}

TEST_CASE(ByteReaderBeU32) {
    // 0x12345678 in big-endian: [0x12, 0x34, 0x56, 0x78]
    std::vector<std::byte> buf = {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    std::span<const std::byte> span(buf);

    auto r1 = ByteReader::read_be_u32(span, 0);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), 0x12345678U);

    auto r2 = ByteReader::read_be_u32(span, 4);
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(r2.value(), 0xFFFFFFFFU);

    auto r_bound = ByteReader::read_be_u32(span, 5);
    REQUIRE_FALSE(r_bound.has_value());
}

TEST_CASE(ByteReaderBeU64) {
    // 0x0102030405060708ULL in big-endian
    std::vector<std::byte> buf = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };
    std::span<const std::byte> span(buf);

    auto r1 = ByteReader::read_be_u64(span, 0);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), 0x0102030405060708ULL);

    auto r_bound = ByteReader::read_be_u64(span, 1);
    REQUIRE_FALSE(r_bound.has_value());
}

TEST_CASE(ByteReaderBeU48Timestamp) {
    // ITCH 5.0 6-byte timestamp
    // E.g., 12:00:00.000000000 = 43,200,000,000,000 ns = 0x274657158000
    // In bytes: 0x27, 0x4A, 0x48, 0xA7, 0x80, 0x00
    std::vector<std::byte> buf = {
        std::byte{0x27}, std::byte{0x4A}, std::byte{0x48},
        std::byte{0xA7}, std::byte{0x80}, std::byte{0x00},
        std::byte{0xAA} // extra trailing byte
    };
    std::span<const std::byte> span(buf);

    auto r1 = ByteReader::read_be_u48(span, 0);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), 43200000000000ULL);

    // Max 48-bit value: 0xFFFFFFFFFFFF = 281474976710655
    std::vector<std::byte> buf_max = {
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}
    };
    auto r_max = ByteReader::read_be_u48(std::span<const std::byte>(buf_max), 0);
    REQUIRE_TRUE(r_max.has_value());
    REQUIRE_EQ(r_max.value(), 0xFFFFFFFFFFFFULL);

    // Bounds check
    auto r_short = ByteReader::read_be_u48(span, 2);
    REQUIRE_FALSE(r_short.has_value());
}

TEST_CASE(ByteReaderLittleEndian) {
    // 0x1234 in little endian: [0x34, 0x12]
    std::vector<std::byte> buf = {std::byte{0x34}, std::byte{0x12}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
    std::span<const std::byte> span(buf);

    auto r16 = ByteReader::read_le_u16(span, 0);
    REQUIRE_TRUE(r16.has_value());
    REQUIRE_EQ(r16.value(), 0x1234);

    auto r32 = ByteReader::read_le_u32(span, 2);
    REQUIRE_TRUE(r32.has_value());
    REQUIRE_EQ(r32.value(), 0x12345678U);
}

TEST_CASE(ByteReaderAsciiTrimmed) {
    std::string str = "AAPL    MSFT";
    std::span<const std::byte> span(reinterpret_cast<const std::byte*>(str.data()), str.size());

    auto r1 = ByteReader::read_ascii_trimmed(span, 0, 8);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value(), "AAPL");

    auto r2 = ByteReader::read_ascii_trimmed(span, 8, 4);
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(r2.value(), "MSFT");

    auto r3 = ByteReader::read_ascii_exact(span, 0, 8);
    REQUIRE_TRUE(r3.has_value());
    REQUIRE_EQ(r3.value(), "AAPL    ");
}

TEST_CASE(PriceRepresentation) {
    Price p(1234500); // $123.4500
    REQUIRE_EQ(p.raw, 1234500U);
    REQUIRE_EQ(p.to_double(), 123.45);
    REQUIRE_FALSE(p.is_zero());

    Price zero(0);
    REQUIRE_TRUE(zero.is_zero());
    REQUIRE_EQ(zero.to_double(), 0.0);

    Price p1(1000000); // 100.00
    Price p2(1005000); // 100.50
    REQUIRE_TRUE(p1 < p2);
    REQUIRE_TRUE(p2 > p1);
    REQUIRE_TRUE(p1 == Price(1000000));
}

TEST_CASE(SideParsing) {
    REQUIRE_EQ(parse_side('B'), Side::Buy);
    REQUIRE_EQ(parse_side('b'), Side::Buy);
    REQUIRE_EQ(parse_side('S'), Side::Sell);
    REQUIRE_EQ(parse_side('s'), Side::Sell);
    REQUIRE_EQ(parse_side('X'), Side::Unknown);
    REQUIRE_EQ(to_char(Side::Buy), 'B');
    REQUIRE_EQ(to_char(Side::Sell), 'S');
}

TEST_CASE(CatalogueEntries) {
    REQUIRE_EQ(kMessageCatalogue.size(), 23U);
    REQUIRE_TRUE(is_known_itch_type('A'));
    REQUIRE_TRUE(is_known_itch_type('F'));
    REQUIRE_TRUE(is_known_itch_type('E'));
    REQUIRE_TRUE(is_known_itch_type('C'));
    REQUIRE_TRUE(is_known_itch_type('X'));
    REQUIRE_TRUE(is_known_itch_type('D'));
    REQUIRE_TRUE(is_known_itch_type('U'));
    REQUIRE_TRUE(is_known_itch_type('P'));
    REQUIRE_TRUE(is_known_itch_type('Q'));
    REQUIRE_TRUE(is_known_itch_type('B'));
    REQUIRE_TRUE(is_known_itch_type('S'));
    REQUIRE_TRUE(is_known_itch_type('R'));
    REQUIRE_FALSE(is_known_itch_type('Z'));

    REQUIRE_EQ(get_expected_payload_length('A').value(), 36U);
    REQUIRE_EQ(get_expected_payload_length('F').value(), 40U);
    REQUIRE_EQ(get_expected_payload_length('E').value(), 31U);
    REQUIRE_EQ(get_expected_payload_length('C').value(), 36U);
    REQUIRE_EQ(get_expected_payload_length('X').value(), 23U);
    REQUIRE_EQ(get_expected_payload_length('D').value(), 19U);
    REQUIRE_EQ(get_expected_payload_length('U').value(), 35U);
    REQUIRE_EQ(get_expected_payload_length('S').value(), 12U);
    REQUIRE_EQ(get_expected_payload_length('R').value(), 39U);
}

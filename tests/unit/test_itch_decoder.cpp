#include "fastitch/itch_decoder.hpp"
#include "fastitch/protocol.hpp"
#include "tests/support/test_framework.hpp"

#include <vector>

using namespace fastitch;

// Helper to construct common 11-byte header:
// [type (1)] [locate (2)] [tracking (2)] [timestamp (6)]
static void append_common_header(std::vector<std::byte>& buf, char type, uint16_t locate, uint16_t tracking, uint64_t ts) {
    buf.push_back(static_cast<std::byte>(type));
    buf.push_back(static_cast<std::byte>((locate >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(locate & 0xFF));
    buf.push_back(static_cast<std::byte>((tracking >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(tracking & 0xFF));
    buf.push_back(static_cast<std::byte>((ts >> 40) & 0xFF));
    buf.push_back(static_cast<std::byte>((ts >> 32) & 0xFF));
    buf.push_back(static_cast<std::byte>((ts >> 24) & 0xFF));
    buf.push_back(static_cast<std::byte>((ts >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((ts >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(ts & 0xFF));
}

static void append_u32(std::vector<std::byte>& buf, uint32_t val) {
    buf.push_back(static_cast<std::byte>((val >> 24) & 0xFF));
    buf.push_back(static_cast<std::byte>((val >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((val >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(val & 0xFF));
}

static void append_u64(std::vector<std::byte>& buf, uint64_t val) {
    append_u32(buf, static_cast<uint32_t>(val >> 32));
    append_u32(buf, static_cast<uint32_t>(val & 0xFFFFFFFFU));
}

static void append_ascii(std::vector<std::byte>& buf, std::string_view str, size_t field_len) {
    for (size_t i = 0; i < field_len; ++i) {
        if (i < str.size()) {
            buf.push_back(static_cast<std::byte>(str[i]));
        } else {
            buf.push_back(static_cast<std::byte>(' '));
        }
    }
}

TEST_CASE(CatalogueCountAndMetadata) {
    REQUIRE_EQ(kMessageCatalogue.size(), 23U);
    size_t book_mutating_count = 0;
    for (const auto& entry : kMessageCatalogue) {
        if (entry.alters_resting_book) ++book_mutating_count;
        REQUIRE_TRUE(entry.wire_length > 0);
        REQUIRE_FALSE(entry.name.empty());
    }
    // Only A, F, E, C, X, D, U alter resting order book
    REQUIRE_EQ(book_mutating_count, 7U);
}

// 1. Type S: System Event (12 bytes)
TEST_CASE(GoldenMsgType_S) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'S', 1, 100, 3600000000000ULL);
    buf.push_back(static_cast<std::byte>('O')); // Start of Messages
    REQUIRE_EQ(buf.size(), 12U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    REQUIRE_TRUE(std::holds_alternative<MsgSystemEvent>(res.value()));
    const auto& msg = std::get<MsgSystemEvent>(res.value());
    REQUIRE_EQ(msg.header.stock_locate, 1);
    REQUIRE_EQ(msg.header.tracking_number, 100);
    REQUIRE_EQ(msg.header.timestamp_nanos, 3600000000000ULL);
    REQUIRE_EQ(msg.event_code, 'O');
}

// 2. Type R: Stock Directory (39 bytes)
TEST_CASE(GoldenMsgType_R) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'R', 42, 200, 3600100000000ULL);
    append_ascii(buf, "AAPL", 8);
    buf.push_back(static_cast<std::byte>('Q')); // Market Category
    buf.push_back(static_cast<std::byte>('N')); // Financial status
    append_u32(buf, 100);                       // Round lot size
    buf.push_back(static_cast<std::byte>('N')); // Round lots only
    buf.push_back(static_cast<std::byte>('C')); // Issue class
    append_ascii(buf, "SI", 2);                 // Issue subtype
    buf.push_back(static_cast<std::byte>('P')); // Authenticity
    buf.push_back(static_cast<std::byte>('N')); // Short sale
    buf.push_back(static_cast<std::byte>('N')); // IPO flag
    buf.push_back(static_cast<std::byte>('1')); // LULD tier
    buf.push_back(static_cast<std::byte>('N')); // ETP flag
    append_u32(buf, 1);                         // ETP leverage
    buf.push_back(static_cast<std::byte>('N')); // Inverse
    REQUIRE_EQ(buf.size(), 39U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    REQUIRE_TRUE(std::holds_alternative<MsgStockDirectory>(res.value()));
    const auto& msg = std::get<MsgStockDirectory>(res.value());
    REQUIRE_EQ(msg.stock, "AAPL");
    REQUIRE_EQ(msg.round_lot_size, 100U);
}

// 3. Type H: Stock Trading Action (25 bytes)
TEST_CASE(GoldenMsgType_H) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'H', 42, 201, 3600200000000ULL);
    append_ascii(buf, "AAPL", 8);
    buf.push_back(static_cast<std::byte>('T')); // Trading state = Trading
    buf.push_back(static_cast<std::byte>(' ')); // Reserved
    append_ascii(buf, "    ", 4);               // Reason
    REQUIRE_EQ(buf.size(), 25U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgStockTradingAction>(res.value());
    REQUIRE_EQ(msg.trading_state, 'T');
    REQUIRE_EQ(msg.stock, "AAPL");
}

// 4. Type Y: Reg SHO (20 bytes)
TEST_CASE(GoldenMsgType_Y) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'Y', 42, 202, 3600300000000ULL);
    append_ascii(buf, "AAPL", 8);
    buf.push_back(static_cast<std::byte>('0')); // Reg SHO Action = None
    REQUIRE_EQ(buf.size(), 20U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgRegShoRestriction>(res.value());
    REQUIRE_EQ(msg.reg_sho_action, '0');
}

// 5. Type L: Market Participant Position (26 bytes)
TEST_CASE(GoldenMsgType_L) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'L', 42, 203, 3600400000000ULL);
    append_ascii(buf, "GSCO", 4);
    append_ascii(buf, "AAPL", 8);
    buf.push_back(static_cast<std::byte>('Y'));
    buf.push_back(static_cast<std::byte>('N'));
    buf.push_back(static_cast<std::byte>('A'));
    REQUIRE_EQ(buf.size(), 26U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgMarketParticipantPosition>(res.value());
    REQUIRE_EQ(msg.mpid, "GSCO");
    REQUIRE_EQ(msg.primary_market_maker, 'Y');
}

// 6. Type V: MWCB Decline Level (35 bytes)
TEST_CASE(GoldenMsgType_V) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'V', 0, 1, 3600500000000ULL);
    append_u64(buf, 100000000ULL);
    append_u64(buf, 200000000ULL);
    append_u64(buf, 300000000ULL);
    REQUIRE_EQ(buf.size(), 35U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgMwcbDeclineLevel>(res.value());
    REQUIRE_EQ(msg.level1, 100000000ULL);
}

// 7. Type W: MWCB Status (12 bytes)
TEST_CASE(GoldenMsgType_W) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'W', 0, 1, 3600600000000ULL);
    buf.push_back(static_cast<std::byte>('1'));
    REQUIRE_EQ(buf.size(), 12U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgMwcbStatus>(res.value());
    REQUIRE_EQ(msg.breached_level, '1');
}

// 8. Type K: IPO Quoting Period (28 bytes)
TEST_CASE(GoldenMsgType_K) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'K', 10, 1, 3600700000000ULL);
    append_ascii(buf, "SNOW", 8);
    append_u32(buf, 36500); // release time seconds
    buf.push_back(static_cast<std::byte>('A'));
    append_u32(buf, 1200000); // $120.00
    REQUIRE_EQ(buf.size(), 28U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgIpoQuotingPeriodUpdate>(res.value());
    REQUIRE_EQ(msg.stock, "SNOW");
    REQUIRE_EQ(msg.ipo_price.raw, 1200000U);
}

// 9. Type J: LULD Auction Collar (35 bytes)
TEST_CASE(GoldenMsgType_J) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'J', 10, 2, 3600800000000ULL);
    append_ascii(buf, "SNOW", 8);
    append_u32(buf, 1200000);
    append_u32(buf, 1300000);
    append_u32(buf, 1100000);
    append_u32(buf, 1);
    REQUIRE_EQ(buf.size(), 35U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgLuldAuctionCollar>(res.value());
    REQUIRE_EQ(msg.auction_collar_reference_price.raw, 1200000U);
}

// 10. Type h: Operational Halt (21 bytes)
TEST_CASE(GoldenMsgType_h) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'h', 10, 3, 3600900000000ULL);
    append_ascii(buf, "SNOW", 8);
    buf.push_back(static_cast<std::byte>('Q'));
    buf.push_back(static_cast<std::byte>('T'));
    REQUIRE_EQ(buf.size(), 21U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOperationalHalt>(res.value());
    REQUIRE_EQ(msg.operational_halt_action, 'T');
}

// 11. Type A: Add Order No MPID (36 bytes)
TEST_CASE(GoldenMsgType_A) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'A', 42, 1, 3601000000000ULL);
    append_u64(buf, 100001ULL);              // Order reference number
    buf.push_back(static_cast<std::byte>('B'));// Buy
    append_u32(buf, 500);                     // 500 shares
    append_ascii(buf, "AAPL", 8);             // Stock
    append_u32(buf, 1500000);                 // Price $150.00
    REQUIRE_EQ(buf.size(), 36U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgAddOrder>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100001ULL);
    REQUIRE_EQ(msg.buy_sell_indicator, Side::Buy);
    REQUIRE_EQ(msg.shares, 500U);
    REQUIRE_EQ(msg.stock, "AAPL");
    REQUIRE_EQ(msg.price.raw, 1500000U);
    REQUIRE_EQ(msg.price.to_double(), 150.0);
}

// 12. Type F: Add Order With MPID (40 bytes)
TEST_CASE(GoldenMsgType_F) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'F', 42, 2, 3601100000000ULL);
    append_u64(buf, 100002ULL);
    buf.push_back(static_cast<std::byte>('S')); // Sell
    append_u32(buf, 200);
    append_ascii(buf, "AAPL", 8);
    append_u32(buf, 1502500); // $150.25
    append_ascii(buf, "MSCO", 4);
    REQUIRE_EQ(buf.size(), 40U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgAddOrderMpid>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100002ULL);
    REQUIRE_EQ(msg.buy_sell_indicator, Side::Sell);
    REQUIRE_EQ(msg.shares, 200U);
    REQUIRE_EQ(msg.price.raw, 1502500U);
    REQUIRE_EQ(msg.attribution, "MSCO");
}

// 13. Type E: Order Executed (31 bytes)
TEST_CASE(GoldenMsgType_E) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'E', 42, 3, 3601200000000ULL);
    append_u64(buf, 100001ULL); // Order ref
    append_u32(buf, 100);       // Executed 100 shares
    append_u64(buf, 999901ULL); // Match number
    REQUIRE_EQ(buf.size(), 31U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOrderExecuted>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100001ULL);
    REQUIRE_EQ(msg.executed_shares, 100U);
    REQUIRE_EQ(msg.match_number, 999901ULL);
}

// 14. Type C: Order Executed With Price (36 bytes)
TEST_CASE(GoldenMsgType_C) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'C', 42, 4, 3601300000000ULL);
    append_u64(buf, 100001ULL);
    append_u32(buf, 200);
    append_u64(buf, 999902ULL);
    buf.push_back(static_cast<std::byte>('Y'));
    append_u32(buf, 1501000); // execution price $150.10
    REQUIRE_EQ(buf.size(), 36U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOrderExecutedWithPrice>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100001ULL);
    REQUIRE_EQ(msg.executed_shares, 200U);
    REQUIRE_EQ(msg.printable, 'Y');
    REQUIRE_EQ(msg.execution_price.raw, 1501000U);
}

// 15. Type X: Order Cancel (23 bytes)
TEST_CASE(GoldenMsgType_X) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'X', 42, 5, 3601400000000ULL);
    append_u64(buf, 100001ULL);
    append_u32(buf, 50); // Cancel 50 shares
    REQUIRE_EQ(buf.size(), 23U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOrderCancel>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100001ULL);
    REQUIRE_EQ(msg.canceled_shares, 50U);
}

// 16. Type D: Order Delete (19 bytes)
TEST_CASE(GoldenMsgType_D) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'D', 42, 6, 3601500000000ULL);
    append_u64(buf, 100001ULL);
    REQUIRE_EQ(buf.size(), 19U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOrderDelete>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100001ULL);
}

// 17. Type U: Order Replace (35 bytes)
TEST_CASE(GoldenMsgType_U) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'U', 42, 7, 3601600000000ULL);
    append_u64(buf, 100001ULL); // original ID
    append_u64(buf, 100003ULL); // new ID
    append_u32(buf, 300);       // new shares
    append_u32(buf, 1505000);   // new price $150.50
    REQUIRE_EQ(buf.size(), 35U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgOrderReplace>(res.value());
    REQUIRE_EQ(msg.original_order_reference_number, 100001ULL);
    REQUIRE_EQ(msg.new_order_reference_number, 100003ULL);
    REQUIRE_EQ(msg.shares, 300U);
    REQUIRE_EQ(msg.price.raw, 1505000U);
}

// 18. Type P: Non-Cross Trade (44 bytes)
TEST_CASE(GoldenMsgType_P) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'P', 42, 8, 3601700000000ULL);
    append_u64(buf, 100004ULL);
    buf.push_back(static_cast<std::byte>('B'));
    append_u32(buf, 1000);
    append_ascii(buf, "AAPL", 8);
    append_u32(buf, 1500000);
    append_u64(buf, 888801ULL);
    REQUIRE_EQ(buf.size(), 44U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgTradeNonCross>(res.value());
    REQUIRE_EQ(msg.order_reference_number, 100004ULL);
    REQUIRE_EQ(msg.shares, 1000U);
    REQUIRE_EQ(msg.match_number, 888801ULL);
}

// 19. Type Q: Cross Trade (40 bytes)
TEST_CASE(GoldenMsgType_Q) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'Q', 42, 9, 3601800000000ULL);
    append_u64(buf, 50000ULL); // 50,000 shares
    append_ascii(buf, "AAPL", 8);
    append_u32(buf, 1500000);
    append_u64(buf, 777701ULL);
    buf.push_back(static_cast<std::byte>('O')); // Opening cross
    REQUIRE_EQ(buf.size(), 40U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgCrossTrade>(res.value());
    REQUIRE_EQ(msg.shares, 50000ULL);
    REQUIRE_EQ(msg.cross_type, 'O');
}

// 20. Type B: Broken Trade (19 bytes)
TEST_CASE(GoldenMsgType_B) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'B', 42, 10, 3601900000000ULL);
    append_u64(buf, 888801ULL);
    REQUIRE_EQ(buf.size(), 19U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgBrokenTrade>(res.value());
    REQUIRE_EQ(msg.match_number, 888801ULL);
}

// 21. Type I: NOII (50 bytes)
TEST_CASE(GoldenMsgType_I) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'I', 42, 11, 3602000000000ULL);
    append_u64(buf, 10000ULL); // paired
    append_u64(buf, 2000ULL);  // imbalance
    buf.push_back(static_cast<std::byte>('B')); // Buy imbalance
    append_ascii(buf, "AAPL", 8);
    append_u32(buf, 1505000); // Far price
    append_u32(buf, 1502500); // Near price
    append_u32(buf, 1501000); // Current ref price
    buf.push_back(static_cast<std::byte>('C')); // Closing cross
    buf.push_back(static_cast<std::byte>(' '));
    REQUIRE_EQ(buf.size(), 50U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgNoii>(res.value());
    REQUIRE_EQ(msg.paired_shares, 10000ULL);
    REQUIRE_EQ(msg.imbalance_direction, 'B');
}

// 22. Type N: Retail Price Improvement (20 bytes)
TEST_CASE(GoldenMsgType_N) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'N', 42, 12, 3602100000000ULL);
    append_ascii(buf, "AAPL", 8);
    buf.push_back(static_cast<std::byte>('B')); // Retail interest Buy
    REQUIRE_EQ(buf.size(), 20U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgRetailPriceImprovement>(res.value());
    REQUIRE_EQ(msg.interest_flag, 'B');
}

// 23. Type O: Direct Listing Capital Raise (48 bytes)
TEST_CASE(GoldenMsgType_O) {
    std::vector<std::byte> buf;
    append_common_header(buf, 'O', 42, 13, 3602200000000ULL);
    append_ascii(buf, "WARP", 8);
    // 29 remaining bytes to reach 48 bytes
    for (int i = 0; i < 29; ++i) buf.push_back(static_cast<std::byte>(i));
    REQUIRE_EQ(buf.size(), 48U);

    ItchDecoder decoder;
    auto res = decoder.decode(buf);
    REQUIRE_TRUE(res.has_value());
    const auto& msg = std::get<MsgDirectListingCapitalRaise>(res.value());
    REQUIRE_EQ(msg.stock, "WARP");
}

TEST_CASE(ExactLengthValidationKnownTypes) {
    // Take Add Order (A), expected length 36
    std::vector<std::byte> buf_short(35, std::byte{0});
    buf_short[0] = static_cast<std::byte>('A');

    std::vector<std::byte> buf_long(37, std::byte{0});
    buf_long[0] = static_cast<std::byte>('A');

    ItchDecoder decoder;
    auto res_short = decoder.decode(buf_short);
    REQUIRE_FALSE(res_short.has_value());
    REQUIRE_EQ(res_short.error().code, ErrorCode::InvalidMessageLength);

    auto res_long = decoder.decode(buf_long);
    REQUIRE_FALSE(res_long.has_value());
    REQUIRE_EQ(res_long.error().code, ErrorCode::InvalidMessageLength);
}

TEST_CASE(UnknownMessageTypePolicies) {
    std::vector<std::byte> buf(20, std::byte{0});
    buf[0] = static_cast<std::byte>('Z'); // Tag 'Z' is not in ITCH 5.0 catalogue

    // Strict policy
    ItchDecoder strict_decoder(UnknownTypePolicy::Strict);
    auto res_strict = strict_decoder.decode(buf);
    REQUIRE_FALSE(res_strict.has_value());
    REQUIRE_EQ(res_strict.error().code, ErrorCode::UnknownMessageType);
    REQUIRE_EQ(strict_decoder.unknown_type_count(), 0ULL);

    // Permissive policy
    ItchDecoder perm_decoder(UnknownTypePolicy::Permissive);
    auto res_perm = perm_decoder.decode(buf);
    REQUIRE_TRUE(res_perm.has_value());
    REQUIRE_TRUE(std::holds_alternative<MsgUnknown>(res_perm.value()));
    REQUIRE_EQ(perm_decoder.unknown_type_count(), 1ULL);
}

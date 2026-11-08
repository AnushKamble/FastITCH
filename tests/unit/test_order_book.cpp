#include "fastitch/order_book.hpp"
#include "fastitch/normalizer.hpp"
#include "tests/support/test_framework.hpp"

using namespace fastitch;

TEST_CASE(OrderBookAddSingleAndMultipleAtSamePrice) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // 1. Add first Buy order at $100.00 for 100 shares
    MsgAddOrder a1{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 101,
        .buy_sell_indicator = Side::Buy,
        .shares = 100,
        .stock = "AAPL",
        .price = Price(1000000)
    };
    auto r1 = engine.apply(DecodedItchMessage{a1});
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(engine.active_order_count(), 1U);
    REQUIRE_TRUE(engine.is_valid());

    auto bbo1 = engine.get_bbo(loc);
    REQUIRE_TRUE(bbo1.bid.valid);
    REQUIRE_EQ(bbo1.bid.price.raw, 1000000U);
    REQUIRE_EQ(bbo1.bid.total_shares, 100ULL);
    REQUIRE_EQ(bbo1.bid.order_count, 1U);
    REQUIRE_FALSE(bbo1.ask.valid); // No asks yet

    // 2. Add second Buy order at same price $100.00 for 250 shares
    MsgAddOrder a2{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .order_reference_number = 102,
        .buy_sell_indicator = Side::Buy,
        .shares = 250,
        .stock = "AAPL",
        .price = Price(1000000)
    };
    auto r2 = engine.apply(DecodedItchMessage{a2});
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(engine.active_order_count(), 2U);

    auto bbo2 = engine.get_bbo(loc);
    REQUIRE_EQ(bbo2.bid.total_shares, 350ULL);
    REQUIRE_EQ(bbo2.bid.order_count, 2U);
    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(OrderBookTwoSidedBBOAndSpread) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // Best Bid: $150.00 for 500 shares
    MsgAddOrder b1{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 201,
        .buy_sell_indicator = Side::Buy,
        .shares = 500,
        .stock = "MSFT",
        .price = Price(1500000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{b1}).has_value());

    // Best Ask: $150.10 for 300 shares
    MsgAddOrderMpid a1{
        .header = {.message_type = 'F', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .order_reference_number = 202,
        .buy_sell_indicator = Side::Sell,
        .shares = 300,
        .stock = "MSFT",
        .price = Price(1501000),
        .attribution = "TOWR"
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a1}).has_value());

    auto bbo = engine.get_bbo(loc);
    REQUIRE_TRUE(bbo.has_two_sided_quote());
    REQUIRE_FALSE(bbo.is_crossed());
    REQUIRE_EQ(bbo.bid.price.to_double(), 150.0);
    REQUIRE_EQ(bbo.bid.total_shares, 500ULL);
    REQUIRE_EQ(bbo.ask.price.to_double(), 150.10);
    REQUIRE_EQ(bbo.ask.total_shares, 300ULL);
    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(OrderBookExecutionsPartialAndFull) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // Add order for 1000 shares
    MsgAddOrder a{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 301,
        .buy_sell_indicator = Side::Buy,
        .shares = 1000,
        .stock = "GOOG",
        .price = Price(28000000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());

    // Partial execution of 400 shares
    MsgOrderExecuted e1{
        .header = {.message_type = 'E', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .order_reference_number = 301,
        .executed_shares = 400,
        .match_number = 1001
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{e1}).has_value());

    auto bbo1 = engine.get_bbo(loc);
    REQUIRE_EQ(bbo1.bid.total_shares, 600ULL);
    REQUIRE_EQ(bbo1.bid.order_count, 1U);
    REQUIRE_EQ(engine.active_order_count(), 1U);

    // Full execution of remaining 600 shares with price (Message C)
    MsgOrderExecutedWithPrice c1{
        .header = {.message_type = 'C', .stock_locate = loc, .tracking_number = 3, .timestamp_nanos = 3000},
        .order_reference_number = 301,
        .executed_shares = 600,
        .match_number = 1002,
        .printable = 'Y',
        .execution_price = Price(28000000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{c1}).has_value());

    // Order should be completely removed and price level erased
    auto bbo2 = engine.get_bbo(loc);
    REQUIRE_FALSE(bbo2.bid.valid);
    REQUIRE_EQ(engine.active_order_count(), 0U);
    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(OrderBookOverExecutionRejection) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    MsgAddOrder a{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 401,
        .buy_sell_indicator = Side::Sell,
        .shares = 100,
        .stock = "NVDA",
        .price = Price(5000000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());

    // Execute 150 shares (exceeds 100)
    MsgOrderExecuted e{
        .header = {.message_type = 'E', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .order_reference_number = 401,
        .executed_shares = 150,
        .match_number = 5001
    };
    auto res = engine.apply(DecodedItchMessage{e});
    REQUIRE_FALSE(res.has_value());
    REQUIRE_EQ(res.error().code, ErrorCode::OverExecute);
    REQUIRE_FALSE(engine.is_valid()); // Marked invalid after over-execution
}

TEST_CASE(OrderBookCancellationAndOverCancel) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    MsgAddOrder a{
        .header = {.message_type = 'A', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 501,
        .buy_sell_indicator = Side::Buy,
        .shares = 300,
        .stock = "AMZN",
        .price = Price(33000000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());

    // Cancel 100 shares
    MsgOrderCancel x1{
        .header = {.message_type = 'X', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .order_reference_number = 501,
        .canceled_shares = 100
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{x1}).has_value());
    REQUIRE_EQ(engine.get_bbo(loc).bid.total_shares, 200ULL);

    // Over-cancel 250 shares (> 200 remaining)
    MsgOrderCancel x2{
        .header = {.message_type = 'X', .stock_locate = loc, .tracking_number = 3, .timestamp_nanos = 3000},
        .order_reference_number = 501,
        .canceled_shares = 250
    };
    auto res = engine.apply(DecodedItchMessage{x2});
    REQUIRE_FALSE(res.has_value());
    REQUIRE_EQ(res.error().code, ErrorCode::OverCancel);
    REQUIRE_FALSE(engine.is_valid());
}

TEST_CASE(OrderBookBestLevelDeletionShiftsBBO) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // Three bids:
    // Level 1: $100.00 (order 601, 100 shares)
    // Level 2: $99.50  (order 602, 200 shares)
    // Level 3: $99.00  (order 603, 300 shares)
    MsgAddOrder o1{.header = {.message_type = 'A', .stock_locate = loc}, .order_reference_number = 601, .buy_sell_indicator = Side::Buy, .shares = 100, .stock = "XYZ", .price = Price(1000000)};
    MsgAddOrder o2{.header = {.message_type = 'A', .stock_locate = loc}, .order_reference_number = 602, .buy_sell_indicator = Side::Buy, .shares = 200, .stock = "XYZ", .price = Price(995000)};
    MsgAddOrder o3{.header = {.message_type = 'A', .stock_locate = loc}, .order_reference_number = 603, .buy_sell_indicator = Side::Buy, .shares = 300, .stock = "XYZ", .price = Price(990000)};

    REQUIRE_TRUE(engine.apply(DecodedItchMessage{o1}).has_value());
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{o2}).has_value());
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{o3}).has_value());

    REQUIRE_EQ(engine.get_bbo(loc).bid.price.raw, 1000000U);

    // Delete order 601 (entire $100.00 level erased)
    MsgOrderDelete d1{.header = {.message_type = 'D', .stock_locate = loc}, .order_reference_number = 601};
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{d1}).has_value());

    // BBO immediately shifts down to $99.50
    auto bbo1 = engine.get_bbo(loc);
    REQUIRE_TRUE(bbo1.bid.valid);
    REQUIRE_EQ(bbo1.bid.price.raw, 995000U);
    REQUIRE_EQ(bbo1.bid.total_shares, 200ULL);

    // Delete order 602
    MsgOrderDelete d2{.header = {.message_type = 'D', .stock_locate = loc}, .order_reference_number = 602};
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{d2}).has_value());

    // BBO shifts down to $99.00
    auto bbo2 = engine.get_bbo(loc);
    REQUIRE_TRUE(bbo2.bid.valid);
    REQUIRE_EQ(bbo2.bid.price.raw, 990000U);
    REQUIRE_EQ(bbo2.bid.total_shares, 300ULL);

    // Delete order 603
    MsgOrderDelete d3{.header = {.message_type = 'D', .stock_locate = loc}, .order_reference_number = 603};
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{d3}).has_value());

    // No bids remaining
    auto bbo3 = engine.get_bbo(loc);
    REQUIRE_FALSE(bbo3.bid.valid);
    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(OrderBookReplaceOrderAtomicSemantics) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // Add initial Sell order: 500 shares at $200.00, MPID TOWR
    MsgAddOrderMpid a{
        .header = {.message_type = 'F', .stock_locate = loc, .tracking_number = 1, .timestamp_nanos = 1000},
        .order_reference_number = 701,
        .buy_sell_indicator = Side::Sell,
        .shares = 500,
        .stock = "TSLA",
        .price = Price(2000000),
        .attribution = "TOWR"
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());

    // Replace order 701 with 702: 400 shares at $201.00
    MsgOrderReplace u{
        .header = {.message_type = 'U', .stock_locate = loc, .tracking_number = 2, .timestamp_nanos = 2000},
        .original_order_reference_number = 701,
        .new_order_reference_number = 702,
        .shares = 400,
        .price = Price(2010000)
    };
    auto res = engine.apply(DecodedItchMessage{u});
    REQUIRE_TRUE(res.has_value());

    // Old order 701 must not exist
    REQUIRE_FALSE(engine.active_orders().contains(701));
    // New order 702 must exist
    REQUIRE_TRUE(engine.active_orders().contains(702));

    const auto& new_ord = engine.active_orders().at(702);
    REQUIRE_EQ(new_ord.order_id, 702ULL);
    REQUIRE_EQ(new_ord.stock_locate, loc);
    REQUIRE_EQ(new_ord.side, Side::Sell); // Inherited side
    REQUIRE_EQ(new_ord.price.raw, 2010000U);
    REQUIRE_EQ(new_ord.remaining_shares, 400U);
    REQUIRE_EQ(new_ord.attribution, "TOWR"); // Inherited attribution

    // BBO reflected
    auto bbo = engine.get_bbo(loc);
    REQUIRE_TRUE(bbo.ask.valid);
    REQUIRE_EQ(bbo.ask.price.raw, 2010000U);
    REQUIRE_EQ(bbo.ask.total_shares, 400ULL);
    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(TradeAndAdminMessagesDoNotMutateBook) {
    OrderBookEngine engine;
    uint16_t loc = 1;

    // Resting order on book
    MsgAddOrder a{
        .header = {.message_type = 'A', .stock_locate = loc},
        .order_reference_number = 801,
        .buy_sell_indicator = Side::Buy,
        .shares = 100,
        .stock = "META",
        .price = Price(3000000)
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());
    REQUIRE_EQ(engine.active_order_count(), 1U);

    // Apply Non-Cross Trade (P)
    MsgTradeNonCross p{
        .header = {.message_type = 'P', .stock_locate = loc},
        .order_reference_number = 801,
        .buy_sell_indicator = Side::Buy,
        .shares = 5000,
        .stock = "META",
        .price = Price(3000000),
        .match_number = 9999
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{p}).has_value());
    // Resting order and BBO must be completely untouched!
    REQUIRE_EQ(engine.active_order_count(), 1U);
    REQUIRE_EQ(engine.get_bbo(loc).bid.total_shares, 100ULL);

    // Apply Cross Trade (Q)
    MsgCrossTrade q{
        .header = {.message_type = 'Q', .stock_locate = loc},
        .shares = 500000,
        .stock = "META",
        .cross_price = Price(3000000),
        .match_number = 8888,
        .cross_type = 'O'
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{q}).has_value());
    REQUIRE_EQ(engine.active_order_count(), 1U);
    REQUIRE_EQ(engine.get_bbo(loc).bid.total_shares, 100ULL);

    // Apply Broken Trade (B)
    MsgBrokenTrade b{
        .header = {.message_type = 'B', .stock_locate = loc},
        .match_number = 9999
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{b}).has_value());
    REQUIRE_EQ(engine.active_order_count(), 1U);
    REQUIRE_EQ(engine.get_bbo(loc).bid.total_shares, 100ULL);

    // Apply System Event (S)
    MsgSystemEvent s{
        .header = {.message_type = 'S'},
        .event_code = 'Q'
    };
    REQUIRE_TRUE(engine.apply(DecodedItchMessage{s}).has_value());
    REQUIRE_EQ(engine.active_order_count(), 1U);
    REQUIRE_EQ(engine.get_bbo(loc).bid.total_shares, 100ULL);

    REQUIRE_TRUE(engine.verify_invariants());
}

TEST_CASE(EventNormalizerAndCountingSink) {
    CountingEventSink sink;

    MsgAddOrder a{
        .header = {.message_type = 'A', .stock_locate = 10, .timestamp_nanos = 50000},
        .order_reference_number = 901,
        .buy_sell_indicator = Side::Buy,
        .shares = 100,
        .stock = "IBM",
        .price = Price(1400000)
    };
    auto ev_add = EventNormalizer::normalize(DecodedItchMessage{a}, 1);
    sink.on_event(ev_add);

    MsgOrderExecuted e{
        .header = {.message_type = 'E', .stock_locate = 10, .timestamp_nanos = 51000},
        .order_reference_number = 901,
        .executed_shares = 100,
        .match_number = 777
    };
    auto ev_exec = EventNormalizer::normalize(DecodedItchMessage{e}, 2);
    sink.on_event(ev_exec);

    MsgTradeNonCross p{
        .header = {.message_type = 'P', .stock_locate = 10, .timestamp_nanos = 52000},
        .order_reference_number = 902,
        .buy_sell_indicator = Side::Sell,
        .shares = 1000,
        .stock = "IBM",
        .price = Price(1400000),
        .match_number = 888
    };
    auto ev_trade = EventNormalizer::normalize(DecodedItchMessage{p}, 3);
    sink.on_event(ev_trade);

    REQUIRE_EQ(sink.total_events(), 3ULL);
    REQUIRE_EQ(sink.add_count(), 1ULL);
    REQUIRE_EQ(sink.exec_count(), 1ULL);
    REQUIRE_EQ(sink.trade_count(), 1ULL);
}

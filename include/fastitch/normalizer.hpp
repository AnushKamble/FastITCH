#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fastitch/itch_decoder.hpp"
#include "fastitch/protocol.hpp"

namespace fastitch {

enum class NormalizedEventKind : uint8_t {
    Add,
    Execute,
    Cancel,
    Delete,
    Replace,
    TradePrint,
    CrossSummary,
    BrokenTrade,
    SystemStatus,
    StockDirectory,
    OtherMetadata
};

[[nodiscard]] constexpr std::string_view to_string(NormalizedEventKind kind) noexcept {
    switch (kind) {
        case NormalizedEventKind::Add: return "ADD";
        case NormalizedEventKind::Execute: return "EXECUTE";
        case NormalizedEventKind::Cancel: return "CANCEL";
        case NormalizedEventKind::Delete: return "DELETE";
        case NormalizedEventKind::Replace: return "REPLACE";
        case NormalizedEventKind::TradePrint: return "TRADE";
        case NormalizedEventKind::CrossSummary: return "CROSS";
        case NormalizedEventKind::BrokenTrade: return "BROKEN_TRADE";
        case NormalizedEventKind::SystemStatus: return "SYSTEM";
        case NormalizedEventKind::StockDirectory: return "DIRECTORY";
        case NormalizedEventKind::OtherMetadata: return "METADATA";
    }
    return "UNKNOWN";
}

struct NormalizedEvent {
    NormalizedEventKind kind{NormalizedEventKind::OtherMetadata};
    uint16_t stock_locate{0};
    std::string stock{};
    uint64_t timestamp_nanos{0};
    uint64_t source_sequence{0};
    uint64_t order_reference{0};
    Side side{Side::Unknown};
    Price price{0};
    uint32_t shares{0};
    uint64_t match_number{0};
    std::string mpid{};
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_event(const NormalizedEvent& event) = 0;
};

class NullEventSink : public EventSink {
public:
    void on_event(const NormalizedEvent& /*event*/) override {}
};

class CountingEventSink : public EventSink {
public:
    void on_event(const NormalizedEvent& event) override {
        ++total_events_;
        switch (event.kind) {
            case NormalizedEventKind::Add: ++add_count_; break;
            case NormalizedEventKind::Execute: ++exec_count_; break;
            case NormalizedEventKind::Cancel: ++cancel_count_; break;
            case NormalizedEventKind::Delete: ++delete_count_; break;
            case NormalizedEventKind::Replace: ++replace_count_; break;
            case NormalizedEventKind::TradePrint: ++trade_count_; break;
            case NormalizedEventKind::CrossSummary: ++cross_count_; break;
            default: ++other_count_; break;
        }
    }

    [[nodiscard]] uint64_t total_events() const noexcept { return total_events_; }
    [[nodiscard]] uint64_t add_count() const noexcept { return add_count_; }
    [[nodiscard]] uint64_t exec_count() const noexcept { return exec_count_; }
    [[nodiscard]] uint64_t cancel_count() const noexcept { return cancel_count_; }
    [[nodiscard]] uint64_t delete_count() const noexcept { return delete_count_; }
    [[nodiscard]] uint64_t replace_count() const noexcept { return replace_count_; }
    [[nodiscard]] uint64_t trade_count() const noexcept { return trade_count_; }
    [[nodiscard]] uint64_t cross_count() const noexcept { return cross_count_; }
    [[nodiscard]] uint64_t other_count() const noexcept { return other_count_; }

private:
    uint64_t total_events_{0};
    uint64_t add_count_{0};
    uint64_t exec_count_{0};
    uint64_t cancel_count_{0};
    uint64_t delete_count_{0};
    uint64_t replace_count_{0};
    uint64_t trade_count_{0};
    uint64_t cross_count_{0};
    uint64_t other_count_{0};
};

class EventNormalizer {
public:
    static NormalizedEvent normalize(const DecodedItchMessage& msg, uint64_t seq = 0) {
        return std::visit([seq](const auto& m) -> NormalizedEvent {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, MsgAddOrder>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Add,
                    .stock_locate = m.header.stock_locate,
                    .stock = std::string(m.stock),
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .side = m.buy_sell_indicator,
                    .price = m.price,
                    .shares = m.shares
                };
            } else if constexpr (std::is_same_v<T, MsgAddOrderMpid>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Add,
                    .stock_locate = m.header.stock_locate,
                    .stock = std::string(m.stock),
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .side = m.buy_sell_indicator,
                    .price = m.price,
                    .shares = m.shares,
                    .mpid = std::string(m.attribution)
                };
            } else if constexpr (std::is_same_v<T, MsgOrderExecuted>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Execute,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .shares = m.executed_shares,
                    .match_number = m.match_number
                };
            } else if constexpr (std::is_same_v<T, MsgOrderExecutedWithPrice>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Execute,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .price = m.execution_price,
                    .shares = m.executed_shares,
                    .match_number = m.match_number
                };
            } else if constexpr (std::is_same_v<T, MsgOrderCancel>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Cancel,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .shares = m.canceled_shares
                };
            } else if constexpr (std::is_same_v<T, MsgOrderDelete>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Delete,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number
                };
            } else if constexpr (std::is_same_v<T, MsgOrderReplace>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::Replace,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.new_order_reference_number,
                    .price = m.price,
                    .shares = m.shares
                };
            } else if constexpr (std::is_same_v<T, MsgTradeNonCross>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::TradePrint,
                    .stock_locate = m.header.stock_locate,
                    .stock = std::string(m.stock),
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .order_reference = m.order_reference_number,
                    .side = m.buy_sell_indicator,
                    .price = m.price,
                    .shares = m.shares,
                    .match_number = m.match_number
                };
            } else if constexpr (std::is_same_v<T, MsgCrossTrade>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::CrossSummary,
                    .stock_locate = m.header.stock_locate,
                    .stock = std::string(m.stock),
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .price = m.cross_price,
                    .shares = static_cast<uint32_t>(m.shares),
                    .match_number = m.match_number
                };
            } else if constexpr (std::is_same_v<T, MsgBrokenTrade>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::BrokenTrade,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq,
                    .match_number = m.match_number
                };
            } else if constexpr (std::is_same_v<T, MsgSystemEvent>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::SystemStatus,
                    .stock_locate = m.header.stock_locate,
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq
                };
            } else if constexpr (std::is_same_v<T, MsgStockDirectory>) {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::StockDirectory,
                    .stock_locate = m.header.stock_locate,
                    .stock = std::string(m.stock),
                    .timestamp_nanos = m.header.timestamp_nanos,
                    .source_sequence = seq
                };
            } else {
                return NormalizedEvent{
                    .kind = NormalizedEventKind::OtherMetadata,
                    .source_sequence = seq
                };
            }
        }, msg);
    }
};

} // namespace fastitch

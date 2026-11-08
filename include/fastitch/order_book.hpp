#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

#include "fastitch/protocol.hpp"
#include "fastitch/itch_decoder.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

struct OrderRecord {
    uint64_t order_id{0};
    uint16_t stock_locate{0};
    Side side{Side::Unknown};
    Price price{0};
    uint32_t remaining_shares{0};
    std::string attribution{}; // 4-char MPID if F message
};

struct PriceLevel {
    uint64_t total_shares{0};
    uint32_t order_count{0};

    auto operator<=>(const PriceLevel&) const = default;
};

struct LevelQuote {
    Price price{0};
    uint64_t total_shares{0};
    uint32_t order_count{0};
    bool valid{false};

    auto operator<=>(const LevelQuote&) const = default;
};

struct BBO {
    LevelQuote bid{};
    LevelQuote ask{};

    [[nodiscard]] bool has_two_sided_quote() const noexcept {
        return bid.valid && ask.valid;
    }

    [[nodiscard]] bool is_crossed() const noexcept {
        return has_two_sided_quote() && bid.price.raw >= ask.price.raw;
    }

    auto operator<=>(const BBO&) const = default;
};

class SymbolOrderBook {
public:
    explicit SymbolOrderBook(uint16_t locate = 0, std::string symbol = "") noexcept
        : stock_locate_(locate), symbol_(std::move(symbol)) {}

    [[nodiscard]] uint16_t stock_locate() const noexcept { return stock_locate_; }
    [[nodiscard]] const std::string& symbol() const noexcept { return symbol_; }
    void set_symbol(std::string sym) { symbol_ = std::move(sym); }

    [[nodiscard]] const std::map<uint32_t, PriceLevel, std::greater<uint32_t>>& bids() const noexcept {
        return bids_;
    }

    [[nodiscard]] const std::map<uint32_t, PriceLevel, std::less<uint32_t>>& asks() const noexcept {
        return asks_;
    }

    [[nodiscard]] BBO bbo() const noexcept {
        BBO out;
        if (!bids_.empty()) {
            const auto& [p, lvl] = *bids_.begin();
            out.bid = LevelQuote{Price(p), lvl.total_shares, lvl.order_count, true};
        }
        if (!asks_.empty()) {
            const auto& [p, lvl] = *asks_.begin();
            out.ask = LevelQuote{Price(p), lvl.total_shares, lvl.order_count, true};
        }
        return out;
    }

    void add_order_to_level(Side side, Price price, uint32_t shares) {
        if (side == Side::Buy) {
            auto& lvl = bids_[price.raw];
            lvl.total_shares += shares;
            lvl.order_count += 1;
        } else if (side == Side::Sell) {
            auto& lvl = asks_[price.raw];
            lvl.total_shares += shares;
            lvl.order_count += 1;
        }
    }

    void reduce_level_shares(Side side, Price price, uint32_t shares) {
        if (side == Side::Buy) {
            auto it = bids_.find(price.raw);
            if (it != bids_.end()) {
                if (it->second.total_shares <= shares) {
                    it->second.total_shares = 0;
                } else {
                    it->second.total_shares -= shares;
                }
            }
        } else if (side == Side::Sell) {
            auto it = asks_.find(price.raw);
            if (it != asks_.end()) {
                if (it->second.total_shares <= shares) {
                    it->second.total_shares = 0;
                } else {
                    it->second.total_shares -= shares;
                }
            }
        }
    }

    void remove_order_from_level(Side side, Price price, uint32_t remaining_shares) {
        if (side == Side::Buy) {
            auto it = bids_.find(price.raw);
            if (it != bids_.end()) {
                if (it->second.total_shares <= remaining_shares) {
                    it->second.total_shares = 0;
                } else {
                    it->second.total_shares -= remaining_shares;
                }
                if (it->second.order_count > 0) {
                    it->second.order_count -= 1;
                }
                if (it->second.order_count == 0 || it->second.total_shares == 0) {
                    bids_.erase(it);
                }
            }
        } else if (side == Side::Sell) {
            auto it = asks_.find(price.raw);
            if (it != asks_.end()) {
                if (it->second.total_shares <= remaining_shares) {
                    it->second.total_shares = 0;
                } else {
                    it->second.total_shares -= remaining_shares;
                }
                if (it->second.order_count > 0) {
                    it->second.order_count -= 1;
                }
                if (it->second.order_count == 0 || it->second.total_shares == 0) {
                    asks_.erase(it);
                }
            }
        }
    }

    void clear() {
        bids_.clear();
        asks_.clear();
    }

private:
    uint16_t stock_locate_{0};
    std::string symbol_{};
    std::map<uint32_t, PriceLevel, std::greater<uint32_t>> bids_{};
    std::map<uint32_t, PriceLevel, std::less<uint32_t>> asks_{};
};

class OrderBookEngine {
public:
    static constexpr size_t kMaxStockLocate = 65536;

    OrderBookEngine() {
        symbol_books_.resize(kMaxStockLocate);
        for (uint32_t i = 0; i < kMaxStockLocate; ++i) {
            symbol_books_[i] = SymbolOrderBook(static_cast<uint16_t>(i));
        }
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return is_valid_;
    }

    void invalidate() noexcept {
        is_valid_ = false;
    }

    [[nodiscard]] size_t active_order_count() const noexcept {
        return active_orders_.size();
    }

    [[nodiscard]] const std::unordered_map<uint64_t, OrderRecord>& active_orders() const noexcept {
        return active_orders_;
    }

    [[nodiscard]] const SymbolOrderBook& get_book(uint16_t locate) const noexcept {
        return symbol_books_[locate];
    }

    [[nodiscard]] SymbolOrderBook& get_book(uint16_t locate) noexcept {
        return symbol_books_[locate];
    }

    [[nodiscard]] const SymbolOrderBook* find_book_by_symbol(std::string_view symbol) const noexcept {
        auto it = symbol_to_locate_.find(std::string(symbol));
        if (it != symbol_to_locate_.end()) {
            return &symbol_books_[it->second];
        }
        return nullptr;
    }

    [[nodiscard]] BBO get_bbo(uint16_t locate) const noexcept {
        return symbol_books_[locate].bbo();
    }

    // Core state mutation handlers
    Result<void> apply_add_order(const MsgAddOrder& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_add_order_internal(msg.header.stock_locate, msg.order_reference_number,
                                        msg.buy_sell_indicator, msg.shares, msg.stock, msg.price, "", offset, seq);
    }

    Result<void> apply_add_order_mpid(const MsgAddOrderMpid& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_add_order_internal(msg.header.stock_locate, msg.order_reference_number,
                                        msg.buy_sell_indicator, msg.shares, msg.stock, msg.price, msg.attribution, offset, seq);
    }

    Result<void> apply_order_executed(const MsgOrderExecuted& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_execute_internal(msg.header.stock_locate, msg.order_reference_number, msg.executed_shares, offset, seq);
    }

    Result<void> apply_order_executed_with_price(const MsgOrderExecutedWithPrice& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_execute_internal(msg.header.stock_locate, msg.order_reference_number, msg.executed_shares, offset, seq);
    }

    Result<void> apply_order_cancel(const MsgOrderCancel& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_cancel_internal(msg.header.stock_locate, msg.order_reference_number, msg.canceled_shares, offset, seq);
    }

    Result<void> apply_order_delete(const MsgOrderDelete& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return apply_delete_internal(msg.header.stock_locate, msg.order_reference_number, offset, seq);
    }

    Result<void> apply_order_replace(const MsgOrderReplace& msg, uint64_t offset = 0, uint64_t seq = 0) {
        uint64_t orig_id = msg.original_order_reference_number;
        uint64_t new_id = msg.new_order_reference_number;

        if (msg.shares == 0) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidShares, offset, seq, "Order replace with 0 shares"};
        }
        if (msg.price.is_zero()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidPrice, offset, seq, "Order replace with 0 price"};
        }

        auto it = active_orders_.find(orig_id);
        if (it == active_orders_.end()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OrderNotFound, offset, seq, "Replace target order not found: " + std::to_string(orig_id)};
        }

        if (active_orders_.contains(new_id)) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::DuplicateOrderId, offset, seq, "Replace new order ID already active: " + std::to_string(new_id)};
        }

        OrderRecord old_record = it->second;

        // Atomic update:
        // 1. Remove old order from price levels
        symbol_books_[old_record.stock_locate].remove_order_from_level(old_record.side, old_record.price, old_record.remaining_shares);
        active_orders_.erase(it);

        // 2. Insert new order with inherited side, locate, and attribution
        OrderRecord new_record{
            .order_id = new_id,
            .stock_locate = old_record.stock_locate,
            .side = old_record.side,
            .price = msg.price,
            .remaining_shares = msg.shares,
            .attribution = old_record.attribution
        };

        symbol_books_[old_record.stock_locate].add_order_to_level(new_record.side, new_record.price, new_record.remaining_shares);
        active_orders_.emplace(new_id, std::move(new_record));

        return Result<void>();
    }

    void record_stock_directory(const MsgStockDirectory& msg) {
        uint16_t loc = msg.header.stock_locate;
        std::string sym(msg.stock);
        symbol_books_[loc].set_symbol(sym);
        symbol_to_locate_[sym] = loc;
    }

    // Apply any decoded message according to its specified book impact
    Result<void> apply(const DecodedItchMessage& msg, uint64_t offset = 0, uint64_t seq = 0) {
        return std::visit([this, offset, seq](const auto& m) -> Result<void> {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, MsgAddOrder>) {
                return apply_add_order(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgAddOrderMpid>) {
                return apply_add_order_mpid(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgOrderExecuted>) {
                return apply_order_executed(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgOrderExecutedWithPrice>) {
                return apply_order_executed_with_price(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgOrderCancel>) {
                return apply_order_cancel(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgOrderDelete>) {
                return apply_order_delete(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgOrderReplace>) {
                return apply_order_replace(m, offset, seq);
            } else if constexpr (std::is_same_v<T, MsgStockDirectory>) {
                record_stock_directory(m);
                return Result<void>();
            } else {
                // Non-mutating administrative or trade messages (P, Q, B, S, H, etc.)
                return Result<void>();
            }
        }, msg);
    }

    // Comprehensive Invariants Audit
    [[nodiscard]] bool verify_invariants() const {
        // 1. Check all active orders have shares > 0 and valid side
        for (const auto& [order_id, order] : active_orders_) {
            if (order.remaining_shares == 0) return false;
            if (order.side != Side::Buy && order.side != Side::Sell) return false;
            if (order.price.is_zero()) return false;
        }

        // 2. Cross-verify price-level aggregated sums against active orders
        std::unordered_map<uint16_t, std::map<uint32_t, PriceLevel, std::greater<uint32_t>>> expected_bids;
        std::unordered_map<uint16_t, std::map<uint32_t, PriceLevel, std::less<uint32_t>>> expected_asks;

        for (const auto& [order_id, order] : active_orders_) {
            if (order.side == Side::Buy) {
                auto& lvl = expected_bids[order.stock_locate][order.price.raw];
                lvl.total_shares += order.remaining_shares;
                lvl.order_count += 1;
            } else {
                auto& lvl = expected_asks[order.stock_locate][order.price.raw];
                lvl.total_shares += order.remaining_shares;
                lvl.order_count += 1;
            }
        }

        for (uint32_t loc = 0; loc < kMaxStockLocate; ++loc) {
            const auto& book = symbol_books_[loc];
            if (expected_bids.contains(static_cast<uint16_t>(loc))) {
                if (book.bids() != expected_bids.at(static_cast<uint16_t>(loc))) return false;
            } else {
                if (!book.bids().empty()) return false;
            }

            if (expected_asks.contains(static_cast<uint16_t>(loc))) {
                if (book.asks() != expected_asks.at(static_cast<uint16_t>(loc))) return false;
            } else {
                if (!book.asks().empty()) return false;
            }
        }

        return true;
    }

private:
    Result<void> apply_add_order_internal(uint16_t locate, uint64_t order_id, Side side,
                                         uint32_t shares, std::string_view stock, Price price,
                                         std::string_view mpid, uint64_t offset, uint64_t seq) {
        if (side != Side::Buy && side != Side::Sell) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidSide, offset, seq, "Invalid buy/sell side"};
        }
        if (shares == 0) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidShares, offset, seq, "Order shares must be > 0"};
        }
        if (price.is_zero()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidPrice, offset, seq, "Order price must be > 0"};
        }
        if (active_orders_.contains(order_id)) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::DuplicateOrderId, offset, seq, "Duplicate order ID: " + std::to_string(order_id)};
        }

        if (!stock.empty() && symbol_books_[locate].symbol().empty()) {
            std::string sym(stock);
            symbol_books_[locate].set_symbol(sym);
            symbol_to_locate_[sym] = locate;
        }

        OrderRecord record{
            .order_id = order_id,
            .stock_locate = locate,
            .side = side,
            .price = price,
            .remaining_shares = shares,
            .attribution = std::string(mpid)
        };

        symbol_books_[locate].add_order_to_level(side, price, shares);
        active_orders_.emplace(order_id, std::move(record));

        return Result<void>();
    }

    Result<void> apply_execute_internal(uint16_t locate, uint64_t order_id, uint32_t executed_shares, uint64_t offset, uint64_t seq) {
        if (executed_shares == 0) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidShares, offset, seq, "Execution shares must be > 0"};
        }

        auto it = active_orders_.find(order_id);
        if (it == active_orders_.end()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OrderNotFound, offset, seq, "Execute target order not found: " + std::to_string(order_id)};
        }

        if (executed_shares > it->second.remaining_shares) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OverExecute, offset, seq,
                              "Over-execution: executed " + std::to_string(executed_shares) + " > remaining " + std::to_string(it->second.remaining_shares)};
        }

        OrderRecord& record = it->second;
        if (locate != 0 && locate != record.stock_locate) {
            // Locate mismatch if specified
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::CorruptHeader, offset, seq, "Stock locate mismatch on execution"};
        }

        symbol_books_[record.stock_locate].reduce_level_shares(record.side, record.price, executed_shares);
        record.remaining_shares -= executed_shares;

        if (record.remaining_shares == 0) {
            symbol_books_[record.stock_locate].remove_order_from_level(record.side, record.price, 0);
            active_orders_.erase(it);
        }

        return Result<void>();
    }

    Result<void> apply_cancel_internal(uint16_t locate, uint64_t order_id, uint32_t canceled_shares, uint64_t offset, uint64_t seq) {
        if (canceled_shares == 0) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::InvalidShares, offset, seq, "Cancel shares must be > 0"};
        }

        auto it = active_orders_.find(order_id);
        if (it == active_orders_.end()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OrderNotFound, offset, seq, "Cancel target order not found: " + std::to_string(order_id)};
        }

        if (canceled_shares > it->second.remaining_shares) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OverCancel, offset, seq,
                              "Over-cancel: canceled " + std::to_string(canceled_shares) + " > remaining " + std::to_string(it->second.remaining_shares)};
        }

        OrderRecord& record = it->second;
        if (locate != 0 && locate != record.stock_locate) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::CorruptHeader, offset, seq, "Stock locate mismatch on cancel"};
        }

        symbol_books_[record.stock_locate].reduce_level_shares(record.side, record.price, canceled_shares);
        record.remaining_shares -= canceled_shares;

        if (record.remaining_shares == 0) {
            symbol_books_[record.stock_locate].remove_order_from_level(record.side, record.price, 0);
            active_orders_.erase(it);
        }

        return Result<void>();
    }

    Result<void> apply_delete_internal(uint16_t locate, uint64_t order_id, uint64_t offset, uint64_t seq) {
        auto it = active_orders_.find(order_id);
        if (it == active_orders_.end()) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::OrderNotFound, offset, seq, "Delete target order not found: " + std::to_string(order_id)};
        }

        OrderRecord& record = it->second;
        if (locate != 0 && locate != record.stock_locate) {
            invalidate();
            return ParseError{SourceLayer::OrderBook, ErrorCode::CorruptHeader, offset, seq, "Stock locate mismatch on delete"};
        }

        symbol_books_[record.stock_locate].remove_order_from_level(record.side, record.price, record.remaining_shares);
        active_orders_.erase(it);

        return Result<void>();
    }

    std::unordered_map<uint64_t, OrderRecord> active_orders_{};
    std::vector<SymbolOrderBook> symbol_books_{};
    std::unordered_map<std::string, uint16_t> symbol_to_locate_{};
    bool is_valid_{true};
};

} // namespace fastitch

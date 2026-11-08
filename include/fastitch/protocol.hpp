#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <optional>

namespace fastitch {

enum class Side : uint8_t {
    Buy = 'B',
    Sell = 'S',
    Unknown = '?'
};

[[nodiscard]] constexpr Side parse_side(uint8_t val) noexcept {
    if (val == 'B' || val == 'b') return Side::Buy;
    if (val == 'S' || val == 's') return Side::Sell;
    return Side::Unknown;
}

[[nodiscard]] constexpr char to_char(Side s) noexcept {
    switch (s) {
        case Side::Buy: return 'B';
        case Side::Sell: return 'S';
        case Side::Unknown: return '?';
    }
    return '?';
}

struct Price {
    uint32_t raw{0}; // 4 implied decimal places: 1234500 = $123.4500

    constexpr Price() noexcept = default;
    constexpr explicit Price(uint32_t r) noexcept : raw(r) {}

    [[nodiscard]] constexpr double to_double() const noexcept {
        return static_cast<double>(raw) / 10000.0;
    }

    [[nodiscard]] constexpr bool is_zero() const noexcept {
        return raw == 0;
    }

    constexpr auto operator<=>(const Price&) const noexcept = default;
};

enum class MessageType : uint8_t {
    SystemEvent               = 'S',
    StockDirectory            = 'R',
    StockTradingAction        = 'H',
    RegShoRestriction         = 'Y',
    MarketParticipantPosition = 'L',
    MwcbDeclineLevel          = 'V',
    MwcbStatus                = 'W',
    IpoQuotingPeriodUpdate    = 'K',
    LuldAuctionCollar         = 'J',
    OperationalHalt           = 'h',
    AddOrderNoMpid            = 'A',
    AddOrderMpid              = 'F',
    OrderExecuted             = 'E',
    OrderExecutedWithPrice    = 'C',
    OrderCancel               = 'X',
    OrderDelete               = 'D',
    OrderReplace              = 'U',
    TradeNonCross             = 'P',
    CrossTrade                = 'Q',
    BrokenTrade               = 'B',
    Noii                      = 'I',
    RetailPriceImprovement    = 'N',
    DirectListingWithCapitalRaise = 'O',
    Unknown                   = 0
};

struct MessageCatalogueEntry {
    char tag;
    MessageType type;
    size_t wire_length;
    std::string_view name;
    bool alters_resting_book;
};

inline constexpr std::array<MessageCatalogueEntry, 23> kMessageCatalogue = {{
    { 'S', MessageType::SystemEvent,               12, "System Event",                    false },
    { 'R', MessageType::StockDirectory,            39, "Stock Directory",                 false },
    { 'H', MessageType::StockTradingAction,        25, "Stock Trading Action",            false },
    { 'Y', MessageType::RegShoRestriction,         20, "Reg SHO Restriction",             false },
    { 'L', MessageType::MarketParticipantPosition, 26, "Market Participant Position",     false },
    { 'V', MessageType::MwcbDeclineLevel,          35, "MWCB Decline Level",              false },
    { 'W', MessageType::MwcbStatus,                12, "MWCB Status",                     false },
    { 'K', MessageType::IpoQuotingPeriodUpdate,    28, "IPO Quoting Period Update",       false },
    { 'J', MessageType::LuldAuctionCollar,         35, "LULD Auction Collar",             false },
    { 'h', MessageType::OperationalHalt,           21, "Operational Halt",                false },
    { 'A', MessageType::AddOrderNoMpid,            36, "Add Order (No MPID)",             true  },
    { 'F', MessageType::AddOrderMpid,              40, "Add Order (With MPID)",           true  },
    { 'E', MessageType::OrderExecuted,             31, "Order Executed",                  true  },
    { 'C', MessageType::OrderExecutedWithPrice,    36, "Order Executed With Price",       true  },
    { 'X', MessageType::OrderCancel,               23, "Order Cancel",                    true  },
    { 'D', MessageType::OrderDelete,               19, "Order Delete",                    true  },
    { 'U', MessageType::OrderReplace,              35, "Order Replace",                   true  },
    { 'P', MessageType::TradeNonCross,             44, "Trade (Non-Cross)",               false },
    { 'Q', MessageType::CrossTrade,                40, "Cross Trade",                     false },
    { 'B', MessageType::BrokenTrade,               19, "Broken Trade",                    false },
    { 'I', MessageType::Noii,                      50, "NOII",                            false },
    { 'N', MessageType::RetailPriceImprovement,    20, "Retail Price Improvement",        false },
    { 'O', MessageType::DirectListingWithCapitalRaise, 48, "Direct Listing Capital Raise", false }
}};

[[nodiscard]] constexpr std::optional<MessageCatalogueEntry> get_catalogue_entry(uint8_t type_byte) noexcept {
    for (const auto& entry : kMessageCatalogue) {
        if (static_cast<uint8_t>(entry.tag) == type_byte) {
            return entry;
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool is_known_itch_type(uint8_t type_byte) noexcept {
    return get_catalogue_entry(type_byte).has_value();
}

[[nodiscard]] constexpr std::optional<size_t> get_expected_payload_length(uint8_t type_byte) noexcept {
    if (auto entry = get_catalogue_entry(type_byte)) {
        return entry->wire_length;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view get_message_name(uint8_t type_byte) noexcept {
    if (auto entry = get_catalogue_entry(type_byte)) {
        return entry->name;
    }
    return "Unknown Type";
}

} // namespace fastitch

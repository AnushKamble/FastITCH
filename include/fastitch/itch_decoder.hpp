#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <span>

#include "fastitch/bytes.hpp"
#include "fastitch/protocol.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

// Common header for all ITCH 5.0 messages
struct CommonHeader {
    uint8_t  message_type{0};
    uint16_t stock_locate{0};
    uint16_t tracking_number{0};
    uint64_t timestamp_nanos{0};
};

// Typed representations of ITCH 5.0 messages
struct MsgSystemEvent {
    CommonHeader header;
    char event_code{' '};
};

struct MsgStockDirectory {
    CommonHeader header;
    std::string_view stock{};
    char market_category{' '};
    char financial_status_indicator{' '};
    uint32_t round_lot_size{0};
    char round_lots_only{' '};
    char issue_classification{' '};
    std::string_view issue_sub_type{};
    char authenticity{' '};
    char short_sale_threshold_indicator{' '};
    char ipo_flag{' '};
    char luld_reference_price_tier{' '};
    char etp_flag{' '};
    uint32_t etp_leverage_factor{0};
    char inverse_indicator{' '};
};

struct MsgStockTradingAction {
    CommonHeader header;
    std::string_view stock{};
    char trading_state{' '};
    char reserved{' '};
    std::string_view reason{};
};

struct MsgRegShoRestriction {
    CommonHeader header;
    std::string_view stock{};
    char reg_sho_action{' '};
};

struct MsgMarketParticipantPosition {
    CommonHeader header;
    std::string_view mpid{};
    std::string_view stock{};
    char primary_market_maker{' '};
    char market_maker_mode{' '};
    char market_participant_state{' '};
};

struct MsgMwcbDeclineLevel {
    CommonHeader header;
    uint64_t level1{0};
    uint64_t level2{0};
    uint64_t level3{0};
};

struct MsgMwcbStatus {
    CommonHeader header;
    char breached_level{' '};
};

struct MsgIpoQuotingPeriodUpdate {
    CommonHeader header;
    std::string_view stock{};
    uint32_t ipo_quotation_release_time{0};
    char ipo_quotation_release_qualifier{' '};
    Price ipo_price{0};
};

struct MsgLuldAuctionCollar {
    CommonHeader header;
    std::string_view stock{};
    Price auction_collar_reference_price{0};
    Price upper_auction_collar_price{0};
    Price lower_auction_collar_price{0};
    uint32_t auction_collar_extension{0};
};

struct MsgOperationalHalt {
    CommonHeader header;
    std::string_view stock{};
    char market_code{' '};
    char operational_halt_action{' '};
};

// Core Order Lifecycle Messages
struct MsgAddOrder {
    CommonHeader header;
    uint64_t order_reference_number{0};
    Side buy_sell_indicator{Side::Unknown};
    uint32_t shares{0};
    std::string_view stock{};
    Price price{0};
};

struct MsgAddOrderMpid {
    CommonHeader header;
    uint64_t order_reference_number{0};
    Side buy_sell_indicator{Side::Unknown};
    uint32_t shares{0};
    std::string_view stock{};
    Price price{0};
    std::string_view attribution{}; // 4-char MPID
};

struct MsgOrderExecuted {
    CommonHeader header;
    uint64_t order_reference_number{0};
    uint32_t executed_shares{0};
    uint64_t match_number{0};
};

struct MsgOrderExecutedWithPrice {
    CommonHeader header;
    uint64_t order_reference_number{0};
    uint32_t executed_shares{0};
    uint64_t match_number{0};
    char printable{' '};
    Price execution_price{0};
};

struct MsgOrderCancel {
    CommonHeader header;
    uint64_t order_reference_number{0};
    uint32_t canceled_shares{0};
};

struct MsgOrderDelete {
    CommonHeader header;
    uint64_t order_reference_number{0};
};

struct MsgOrderReplace {
    CommonHeader header;
    uint64_t original_order_reference_number{0};
    uint64_t new_order_reference_number{0};
    uint32_t shares{0};
    Price price{0};
};

// Trade and Auction Messages
struct MsgTradeNonCross {
    CommonHeader header;
    uint64_t order_reference_number{0};
    Side buy_sell_indicator{Side::Unknown};
    uint32_t shares{0};
    std::string_view stock{};
    Price price{0};
    uint64_t match_number{0};
};

struct MsgCrossTrade {
    CommonHeader header;
    uint64_t shares{0};
    std::string_view stock{};
    Price cross_price{0};
    uint64_t match_number{0};
    char cross_type{' '};
};

struct MsgBrokenTrade {
    CommonHeader header;
    uint64_t match_number{0};
};

struct MsgNoii {
    CommonHeader header;
    uint64_t paired_shares{0};
    uint64_t imbalance_shares{0};
    char imbalance_direction{' '};
    std::string_view stock{};
    Price far_price{0};
    Price near_price{0};
    Price current_reference_price{0};
    char cross_type{' '};
    char price_variation_indicator{' '};
};

struct MsgRetailPriceImprovement {
    CommonHeader header;
    std::string_view stock{};
    char interest_flag{' '};
};

struct MsgDirectListingCapitalRaise {
    CommonHeader header;
    std::string_view stock{};
    std::span<const std::byte> raw_extension{};
};

// Raw / Unknown Message view for permissive skipping
struct MsgUnknown {
    uint8_t type_tag{0};
    std::span<const std::byte> payload{};
};

using DecodedItchMessage = std::variant<
    MsgSystemEvent,
    MsgStockDirectory,
    MsgStockTradingAction,
    MsgRegShoRestriction,
    MsgMarketParticipantPosition,
    MsgMwcbDeclineLevel,
    MsgMwcbStatus,
    MsgIpoQuotingPeriodUpdate,
    MsgLuldAuctionCollar,
    MsgOperationalHalt,
    MsgAddOrder,
    MsgAddOrderMpid,
    MsgOrderExecuted,
    MsgOrderExecutedWithPrice,
    MsgOrderCancel,
    MsgOrderDelete,
    MsgOrderReplace,
    MsgTradeNonCross,
    MsgCrossTrade,
    MsgBrokenTrade,
    MsgNoii,
    MsgRetailPriceImprovement,
    MsgDirectListingCapitalRaise,
    MsgUnknown
>;

enum class UnknownTypePolicy : uint8_t {
    Strict,     // Return error on unknown type
    Permissive  // Return MsgUnknown and increment unknown counter
};

class ItchDecoder {
public:
    explicit ItchDecoder(UnknownTypePolicy policy = UnknownTypePolicy::Strict) noexcept
        : policy_(policy) {}

    [[nodiscard]] UnknownTypePolicy policy() const noexcept {
        return policy_;
    }

    void set_policy(UnknownTypePolicy p) noexcept {
        policy_ = p;
    }

    [[nodiscard]] uint64_t unknown_type_count() const noexcept {
        return unknown_type_count_;
    }

    // Decodes a bounded ITCH payload.
    // Verifies exact wire length for known types.
    Result<DecodedItchMessage> decode(std::span<const std::byte> payload, uint64_t source_offset = 0, uint64_t sequence = 0) noexcept {
        if (payload.empty()) {
            return ParseError{
                SourceLayer::ITCHDecoder,
                ErrorCode::UnexpectedEndOfInput,
                source_offset,
                sequence,
                "Empty payload in ITCH decoder"
            };
        }

        uint8_t type_tag = static_cast<uint8_t>(payload[0]);
        auto expected_len = get_expected_payload_length(type_tag);

        if (!expected_len.has_value()) {
            if (policy_ == UnknownTypePolicy::Strict) {
                return ParseError{
                    SourceLayer::ITCHDecoder,
                    ErrorCode::UnknownMessageType,
                    source_offset,
                    sequence,
                    "Unknown ITCH message type tag: '" + std::string(1, static_cast<char>(type_tag)) + "'"
                };
            }
            ++unknown_type_count_;
            return DecodedItchMessage{MsgUnknown{type_tag, payload}};
        }

        if (payload.size() != expected_len.value()) {
            return ParseError{
                SourceLayer::ITCHDecoder,
                ErrorCode::InvalidMessageLength,
                source_offset,
                sequence,
                "Message length mismatch for type '" + std::string(1, static_cast<char>(type_tag)) +
                "': expected " + std::to_string(expected_len.value()) +
                ", got " + std::to_string(payload.size())
            };
        }

        // Parse common header (all 23 official types have common 11-byte header)
        CommonHeader hdr;
        hdr.message_type = type_tag;
        auto loc_res = ByteReader::read_be_u16(payload, 1);
        if (!loc_res) return loc_res.error();
        hdr.stock_locate = loc_res.value();

        auto trk_res = ByteReader::read_be_u16(payload, 3);
        if (!trk_res) return trk_res.error();
        hdr.tracking_number = trk_res.value();

        auto ts_res = ByteReader::read_be_u48(payload, 5);
        if (!ts_res) return ts_res.error();
        hdr.timestamp_nanos = ts_res.value();

        switch (type_tag) {
            case 'S': {
                auto code_res = ByteReader::read_u8(payload, 11);
                if (!code_res) return code_res.error();
                return DecodedItchMessage{MsgSystemEvent{hdr, static_cast<char>(code_res.value())}};
            }
            case 'R': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto cat_res = ByteReader::read_u8(payload, 19);
                if (!cat_res) return cat_res.error();
                auto fin_res = ByteReader::read_u8(payload, 20);
                if (!fin_res) return fin_res.error();
                auto lot_res = ByteReader::read_be_u32(payload, 21);
                if (!lot_res) return lot_res.error();
                auto lot_only_res = ByteReader::read_u8(payload, 25);
                if (!lot_only_res) return lot_only_res.error();
                auto cls_res = ByteReader::read_u8(payload, 26);
                if (!cls_res) return cls_res.error();
                auto sub_res = ByteReader::read_ascii_trimmed(payload, 27, 2);
                if (!sub_res) return sub_res.error();
                auto auth_res = ByteReader::read_u8(payload, 29);
                if (!auth_res) return auth_res.error();
                auto ss_res = ByteReader::read_u8(payload, 30);
                if (!ss_res) return ss_res.error();
                auto ipo_res = ByteReader::read_u8(payload, 31);
                if (!ipo_res) return ipo_res.error();
                auto tier_res = ByteReader::read_u8(payload, 32);
                if (!tier_res) return tier_res.error();
                auto etp_res = ByteReader::read_u8(payload, 33);
                if (!etp_res) return etp_res.error();
                auto fac_res = ByteReader::read_be_u32(payload, 34);
                if (!fac_res) return fac_res.error();
                auto inv_res = ByteReader::read_u8(payload, 38);
                if (!inv_res) return inv_res.error();

                return DecodedItchMessage{MsgStockDirectory{
                    .header = hdr,
                    .stock = stk_res.value(),
                    .market_category = static_cast<char>(cat_res.value()),
                    .financial_status_indicator = static_cast<char>(fin_res.value()),
                    .round_lot_size = lot_res.value(),
                    .round_lots_only = static_cast<char>(lot_only_res.value()),
                    .issue_classification = static_cast<char>(cls_res.value()),
                    .issue_sub_type = sub_res.value(),
                    .authenticity = static_cast<char>(auth_res.value()),
                    .short_sale_threshold_indicator = static_cast<char>(ss_res.value()),
                    .ipo_flag = static_cast<char>(ipo_res.value()),
                    .luld_reference_price_tier = static_cast<char>(tier_res.value()),
                    .etp_flag = static_cast<char>(etp_res.value()),
                    .etp_leverage_factor = fac_res.value(),
                    .inverse_indicator = static_cast<char>(inv_res.value())
                }};
            }
            case 'H': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto state_res = ByteReader::read_u8(payload, 19);
                if (!state_res) return state_res.error();
                auto res_res = ByteReader::read_u8(payload, 20);
                if (!res_res) return res_res.error();
                auto rsn_res = ByteReader::read_ascii_trimmed(payload, 21, 4);
                if (!rsn_res) return rsn_res.error();
                return DecodedItchMessage{MsgStockTradingAction{
                    hdr, stk_res.value(), static_cast<char>(state_res.value()),
                    static_cast<char>(res_res.value()), rsn_res.value()
                }};
            }
            case 'Y': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto act_res = ByteReader::read_u8(payload, 19);
                if (!act_res) return act_res.error();
                return DecodedItchMessage{MsgRegShoRestriction{
                    hdr, stk_res.value(), static_cast<char>(act_res.value())
                }};
            }
            case 'L': {
                auto mpid_res = ByteReader::read_ascii_trimmed(payload, 11, 4);
                if (!mpid_res) return mpid_res.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 15, 8);
                if (!stk_res) return stk_res.error();
                auto mm_res = ByteReader::read_u8(payload, 23);
                if (!mm_res) return mm_res.error();
                auto mode_res = ByteReader::read_u8(payload, 24);
                if (!mode_res) return mode_res.error();
                auto ps_res = ByteReader::read_u8(payload, 25);
                if (!ps_res) return ps_res.error();
                return DecodedItchMessage{MsgMarketParticipantPosition{
                    hdr, mpid_res.value(), stk_res.value(),
                    static_cast<char>(mm_res.value()), static_cast<char>(mode_res.value()), static_cast<char>(ps_res.value())
                }};
            }
            case 'V': {
                auto l1 = ByteReader::read_be_u64(payload, 11);
                if (!l1) return l1.error();
                auto l2 = ByteReader::read_be_u64(payload, 19);
                if (!l2) return l2.error();
                auto l3 = ByteReader::read_be_u64(payload, 27);
                if (!l3) return l3.error();
                return DecodedItchMessage{MsgMwcbDeclineLevel{hdr, l1.value(), l2.value(), l3.value()}};
            }
            case 'W': {
                auto b_res = ByteReader::read_u8(payload, 11);
                if (!b_res) return b_res.error();
                return DecodedItchMessage{MsgMwcbStatus{hdr, static_cast<char>(b_res.value())}};
            }
            case 'K': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto time_res = ByteReader::read_be_u32(payload, 19);
                if (!time_res) return time_res.error();
                auto qual_res = ByteReader::read_u8(payload, 23);
                if (!qual_res) return qual_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 24);
                if (!pr_res) return pr_res.error();
                return DecodedItchMessage{MsgIpoQuotingPeriodUpdate{
                    hdr, stk_res.value(), time_res.value(), static_cast<char>(qual_res.value()), Price(pr_res.value())
                }};
            }
            case 'J': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto ref_p = ByteReader::read_be_u32(payload, 19);
                if (!ref_p) return ref_p.error();
                auto up_p = ByteReader::read_be_u32(payload, 23);
                if (!up_p) return up_p.error();
                auto lo_p = ByteReader::read_be_u32(payload, 27);
                if (!lo_p) return lo_p.error();
                auto ext = ByteReader::read_be_u32(payload, 31);
                if (!ext) return ext.error();
                return DecodedItchMessage{MsgLuldAuctionCollar{
                    hdr, stk_res.value(), Price(ref_p.value()), Price(up_p.value()), Price(lo_p.value()), ext.value()
                }};
            }
            case 'h': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto mc_res = ByteReader::read_u8(payload, 19);
                if (!mc_res) return mc_res.error();
                auto act_res = ByteReader::read_u8(payload, 20);
                if (!act_res) return act_res.error();
                return DecodedItchMessage{MsgOperationalHalt{
                    hdr, stk_res.value(), static_cast<char>(mc_res.value()), static_cast<char>(act_res.value())
                }};
            }
            case 'A': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto side_res = ByteReader::read_u8(payload, 19);
                if (!side_res) return side_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 20);
                if (!sh_res) return sh_res.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 24, 8);
                if (!stk_res) return stk_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 32);
                if (!pr_res) return pr_res.error();

                return DecodedItchMessage{MsgAddOrder{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .buy_sell_indicator = parse_side(side_res.value()),
                    .shares = sh_res.value(),
                    .stock = stk_res.value(),
                    .price = Price(pr_res.value())
                }};
            }
            case 'F': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto side_res = ByteReader::read_u8(payload, 19);
                if (!side_res) return side_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 20);
                if (!sh_res) return sh_res.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 24, 8);
                if (!stk_res) return stk_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 32);
                if (!pr_res) return pr_res.error();
                auto mpid_res = ByteReader::read_ascii_trimmed(payload, 36, 4);
                if (!mpid_res) return mpid_res.error();

                return DecodedItchMessage{MsgAddOrderMpid{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .buy_sell_indicator = parse_side(side_res.value()),
                    .shares = sh_res.value(),
                    .stock = stk_res.value(),
                    .price = Price(pr_res.value()),
                    .attribution = mpid_res.value()
                }};
            }
            case 'E': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 19);
                if (!sh_res) return sh_res.error();
                auto match_res = ByteReader::read_be_u64(payload, 23);
                if (!match_res) return match_res.error();

                return DecodedItchMessage{MsgOrderExecuted{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .executed_shares = sh_res.value(),
                    .match_number = match_res.value()
                }};
            }
            case 'C': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 19);
                if (!sh_res) return sh_res.error();
                auto match_res = ByteReader::read_be_u64(payload, 23);
                if (!match_res) return match_res.error();
                auto print_res = ByteReader::read_u8(payload, 31);
                if (!print_res) return print_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 32);
                if (!pr_res) return pr_res.error();

                return DecodedItchMessage{MsgOrderExecutedWithPrice{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .executed_shares = sh_res.value(),
                    .match_number = match_res.value(),
                    .printable = static_cast<char>(print_res.value()),
                    .execution_price = Price(pr_res.value())
                }};
            }
            case 'X': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 19);
                if (!sh_res) return sh_res.error();

                return DecodedItchMessage{MsgOrderCancel{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .canceled_shares = sh_res.value()
                }};
            }
            case 'D': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();

                return DecodedItchMessage{MsgOrderDelete{
                    .header = hdr,
                    .order_reference_number = ref_res.value()
                }};
            }
            case 'U': {
                auto orig_ref = ByteReader::read_be_u64(payload, 11);
                if (!orig_ref) return orig_ref.error();
                auto new_ref = ByteReader::read_be_u64(payload, 19);
                if (!new_ref) return new_ref.error();
                auto sh_res = ByteReader::read_be_u32(payload, 27);
                if (!sh_res) return sh_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 31);
                if (!pr_res) return pr_res.error();

                return DecodedItchMessage{MsgOrderReplace{
                    .header = hdr,
                    .original_order_reference_number = orig_ref.value(),
                    .new_order_reference_number = new_ref.value(),
                    .shares = sh_res.value(),
                    .price = Price(pr_res.value())
                }};
            }
            case 'P': {
                auto ref_res = ByteReader::read_be_u64(payload, 11);
                if (!ref_res) return ref_res.error();
                auto side_res = ByteReader::read_u8(payload, 19);
                if (!side_res) return side_res.error();
                auto sh_res = ByteReader::read_be_u32(payload, 20);
                if (!sh_res) return sh_res.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 24, 8);
                if (!stk_res) return stk_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 32);
                if (!pr_res) return pr_res.error();
                auto match_res = ByteReader::read_be_u64(payload, 36);
                if (!match_res) return match_res.error();

                return DecodedItchMessage{MsgTradeNonCross{
                    .header = hdr,
                    .order_reference_number = ref_res.value(),
                    .buy_sell_indicator = parse_side(side_res.value()),
                    .shares = sh_res.value(),
                    .stock = stk_res.value(),
                    .price = Price(pr_res.value()),
                    .match_number = match_res.value()
                }};
            }
            case 'Q': {
                auto sh_res = ByteReader::read_be_u64(payload, 11);
                if (!sh_res) return sh_res.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 19, 8);
                if (!stk_res) return stk_res.error();
                auto pr_res = ByteReader::read_be_u32(payload, 27);
                if (!pr_res) return pr_res.error();
                auto match_res = ByteReader::read_be_u64(payload, 31);
                if (!match_res) return match_res.error();
                auto ct_res = ByteReader::read_u8(payload, 39);
                if (!ct_res) return ct_res.error();

                return DecodedItchMessage{MsgCrossTrade{
                    .header = hdr,
                    .shares = sh_res.value(),
                    .stock = stk_res.value(),
                    .cross_price = Price(pr_res.value()),
                    .match_number = match_res.value(),
                    .cross_type = static_cast<char>(ct_res.value())
                }};
            }
            case 'B': {
                auto match_res = ByteReader::read_be_u64(payload, 11);
                if (!match_res) return match_res.error();
                return DecodedItchMessage{MsgBrokenTrade{hdr, match_res.value()}};
            }
            case 'I': {
                auto paired = ByteReader::read_be_u64(payload, 11);
                if (!paired) return paired.error();
                auto imb = ByteReader::read_be_u64(payload, 19);
                if (!imb) return imb.error();
                auto dir = ByteReader::read_u8(payload, 27);
                if (!dir) return dir.error();
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 28, 8);
                if (!stk_res) return stk_res.error();
                auto far_p = ByteReader::read_be_u32(payload, 36);
                if (!far_p) return far_p.error();
                auto near_p = ByteReader::read_be_u32(payload, 40);
                if (!near_p) return near_p.error();
                auto ref_p = ByteReader::read_be_u32(payload, 44);
                if (!ref_p) return ref_p.error();
                auto ct = ByteReader::read_u8(payload, 48);
                if (!ct) return ct.error();
                auto var = ByteReader::read_u8(payload, 49);
                if (!var) return var.error();

                return DecodedItchMessage{MsgNoii{
                    .header = hdr,
                    .paired_shares = paired.value(),
                    .imbalance_shares = imb.value(),
                    .imbalance_direction = static_cast<char>(dir.value()),
                    .stock = stk_res.value(),
                    .far_price = Price(far_p.value()),
                    .near_price = Price(near_p.value()),
                    .current_reference_price = Price(ref_p.value()),
                    .cross_type = static_cast<char>(ct.value()),
                    .price_variation_indicator = static_cast<char>(var.value())
                }};
            }
            case 'N': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto int_res = ByteReader::read_u8(payload, 19);
                if (!int_res) return int_res.error();
                return DecodedItchMessage{MsgRetailPriceImprovement{
                    hdr, stk_res.value(), static_cast<char>(int_res.value())
                }};
            }
            case 'O': {
                auto stk_res = ByteReader::read_ascii_trimmed(payload, 11, 8);
                if (!stk_res) return stk_res.error();
                auto ext_span = payload.subspan(19);
                return DecodedItchMessage{MsgDirectListingCapitalRaise{
                    hdr, stk_res.value(), ext_span
                }};
            }
            default:
                break;
        }

        return ParseError{
            SourceLayer::ITCHDecoder,
            ErrorCode::UnknownMessageType,
            source_offset,
            sequence,
            "Unhandled catalogue type: " + std::to_string(type_tag)
        };
    }

private:
    UnknownTypePolicy policy_{UnknownTypePolicy::Strict};
    uint64_t unknown_type_count_{0};
};

} // namespace fastitch

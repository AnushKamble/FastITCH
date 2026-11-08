#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "fastitch/bytes.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

struct EthernetFrameView {
    std::span<const std::byte> payload;
    uint16_t ether_type{0};
    uint8_t vlan_count{0};
    uint16_t vlan_ids[2]{0, 0};
};

struct Ipv4PacketView {
    std::span<const std::byte> payload;
    uint8_t protocol{0};
    uint8_t ihl_bytes{0};
    uint16_t total_length{0};
    bool is_fragmented{false};
};

struct UdpDatagramView {
    std::span<const std::byte> payload;
    uint16_t src_port{0};
    uint16_t dst_port{0};
    uint16_t length{0};
};

class NetworkLayerParser {
public:
    static constexpr uint16_t kEtherTypeIpv4  = 0x0800;
    static constexpr uint16_t kEtherTypeVlan1 = 0x8100; // 802.1Q
    static constexpr uint16_t kEtherTypeVlan2 = 0x88A8; // 802.1ad
    static constexpr uint8_t  kIpProtocolUdp  = 17;

    static Result<EthernetFrameView> parse_ethernet(std::span<const std::byte> frame, uint64_t offset = 0) noexcept {
        // Standard Ethernet header is 14 bytes: dest(6) + src(6) + ethertype(2)
        if (frame.size() < 14) {
            return ParseError{SourceLayer::Ethernet, ErrorCode::CorruptHeader, offset, 0, "Ethernet frame too short (< 14 bytes)"};
        }

        size_t cursor = 12;
        auto etype_res = ByteReader::read_be_u16(frame, cursor);
        if (!etype_res) return etype_res.error();
        uint16_t ether_type = etype_res.value();
        cursor += 2;

        EthernetFrameView view;
        // Parse up to 2 VLAN tags
        while ((ether_type == kEtherTypeVlan1 || ether_type == kEtherTypeVlan2) && view.vlan_count < 2) {
            if (frame.size() - cursor < 4) {
                return ParseError{SourceLayer::Ethernet, ErrorCode::CorruptHeader, offset + cursor, 0, "Truncated VLAN tag"};
            }
            auto tci_res = ByteReader::read_be_u16(frame, cursor);
            if (!tci_res) return tci_res.error();
            view.vlan_ids[view.vlan_count++] = tci_res.value() & 0x0FFFu;
            cursor += 2;

            auto inner_etype = ByteReader::read_be_u16(frame, cursor);
            if (!inner_etype) return inner_etype.error();
            ether_type = inner_etype.value();
            cursor += 2;
        }

        view.ether_type = ether_type;
        view.payload = frame.subspan(cursor);
        return view;
    }

    static Result<Ipv4PacketView> parse_ipv4(std::span<const std::byte> ip_packet, uint64_t offset = 0) noexcept {
        if (ip_packet.size() < 20) {
            return ParseError{SourceLayer::IPv4, ErrorCode::CorruptHeader, offset, 0, "IPv4 packet shorter than minimum 20 bytes"};
        }

        auto ver_ihl_res = ByteReader::read_u8(ip_packet, 0);
        if (!ver_ihl_res) return ver_ihl_res.error();
        uint8_t ver_ihl = ver_ihl_res.value();
        uint8_t version = ver_ihl >> 4;
        uint8_t ihl_words = ver_ihl & 0x0Fu;

        if (version != 4) {
            return ParseError{SourceLayer::IPv4, ErrorCode::UnsupportedVersion, offset, 0, "Non-IPv4 packet: version=" + std::to_string(version)};
        }

        if (ihl_words < 5) {
            return ParseError{SourceLayer::IPv4, ErrorCode::CorruptHeader, offset, 0, "Invalid IPv4 IHL < 5: " + std::to_string(ihl_words)};
        }

        uint8_t ihl_bytes = static_cast<uint8_t>(ihl_words * 4);
        if (ip_packet.size() < ihl_bytes) {
            return ParseError{SourceLayer::IPv4, ErrorCode::CorruptHeader, offset, 0, "IPv4 packet truncated before IHL end"};
        }

        auto len_res = ByteReader::read_be_u16(ip_packet, 2);
        if (!len_res) return len_res.error();
        uint16_t total_length = len_res.value();

        if (total_length < ihl_bytes) {
            return ParseError{SourceLayer::IPv4, ErrorCode::InvalidLength, offset, 0, "IPv4 total length smaller than header length"};
        }

        // Check fragmentation flags
        auto frag_res = ByteReader::read_be_u16(ip_packet, 6);
        if (!frag_res) return frag_res.error();
        uint16_t frag_field = frag_res.value();
        bool more_fragments = (frag_field & 0x2000u) != 0;
        uint16_t frag_offset = frag_field & 0x1FFFu;
        bool is_fragmented = more_fragments || (frag_offset > 0);

        auto proto_res = ByteReader::read_u8(ip_packet, 9);
        if (!proto_res) return proto_res.error();
        uint8_t protocol = proto_res.value();

        size_t available_payload = std::min<size_t>(ip_packet.size() - ihl_bytes, total_length - ihl_bytes);

        return Ipv4PacketView{
            .payload = ip_packet.subspan(ihl_bytes, available_payload),
            .protocol = protocol,
            .ihl_bytes = ihl_bytes,
            .total_length = total_length,
            .is_fragmented = is_fragmented
        };
    }

    static Result<UdpDatagramView> parse_udp(std::span<const std::byte> udp_bytes, uint64_t offset = 0) noexcept {
        if (udp_bytes.size() < 8) {
            return ParseError{SourceLayer::UDP, ErrorCode::CorruptHeader, offset, 0, "UDP datagram shorter than 8-byte header"};
        }

        auto src_p = ByteReader::read_be_u16(udp_bytes, 0);
        if (!src_p) return src_p.error();
        auto dst_p = ByteReader::read_be_u16(udp_bytes, 2);
        if (!dst_p) return dst_p.error();
        auto len_res = ByteReader::read_be_u16(udp_bytes, 4);
        if (!len_res) return len_res.error();

        uint16_t udp_len = len_res.value();
        if (udp_len < 8) {
            return ParseError{SourceLayer::UDP, ErrorCode::InvalidLength, offset, 0, "UDP declared length < 8 bytes: " + std::to_string(udp_len)};
        }

        if (udp_bytes.size() < udp_len) {
            return ParseError{SourceLayer::UDP, ErrorCode::InvalidLength, offset, 0, "UDP datagram truncated: declared " + std::to_string(udp_len) + ", available " + std::to_string(udp_bytes.size())};
        }

        return UdpDatagramView{
            .payload = udp_bytes.subspan(8, udp_len - 8),
            .src_port = src_p.value(),
            .dst_port = dst_p.value(),
            .length = udp_len
        };
    }
};

} // namespace fastitch

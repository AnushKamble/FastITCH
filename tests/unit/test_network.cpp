#include "fastitch/ethernet_ipv4_udp.hpp"
#include "tests/support/test_framework.hpp"

#include <vector>

using namespace fastitch;

TEST_CASE(NetworkEthernetNormalAndVlanTags) {
    // 1. Normal Ethernet frame (14 bytes)
    std::vector<std::byte> frame = {
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0x55}, // dst
        std::byte{0x66}, std::byte{0x77}, std::byte{0x88}, std::byte{0x99}, std::byte{0xAA}, std::byte{0xBB}, // src
        std::byte{0x08}, std::byte{0x00}, // IPv4 EtherType
        std::byte{0x45}, std::byte{0x00}  // Payload bytes
    };
    auto r1 = NetworkLayerParser::parse_ethernet(frame);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value().ether_type, 0x0800);
    REQUIRE_EQ(r1.value().vlan_count, 0);
    REQUIRE_EQ(r1.value().payload.size(), 2U);

    // 2. Single 802.1Q VLAN tagged frame (18 bytes)
    std::vector<std::byte> frame_vlan = {
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0x55},
        std::byte{0x66}, std::byte{0x77}, std::byte{0x88}, std::byte{0x99}, std::byte{0xAA}, std::byte{0xBB},
        std::byte{0x81}, std::byte{0x00}, // 802.1Q EtherType
        std::byte{0x00}, std::byte{0x64}, // VLAN ID = 100
        std::byte{0x08}, std::byte{0x00}, // Encapsulated IPv4
        std::byte{0xDE}, std::byte{0xAD}
    };
    auto r2 = NetworkLayerParser::parse_ethernet(frame_vlan);
    REQUIRE_TRUE(r2.has_value());
    REQUIRE_EQ(r2.value().ether_type, 0x0800);
    REQUIRE_EQ(r2.value().vlan_count, 1);
    REQUIRE_EQ(r2.value().vlan_ids[0], 100);
    REQUIRE_EQ(r2.value().payload.size(), 2U);

    // 3. Truncated frame (< 14 bytes)
    auto r_short = NetworkLayerParser::parse_ethernet(std::span<const std::byte>(frame.data(), 10));
    REQUIRE_FALSE(r_short.has_value());
    REQUIRE_EQ(r_short.error().code, ErrorCode::CorruptHeader);
}

TEST_CASE(NetworkIpv4HeaderAndFragmentation) {
    // 1. Standard 20-byte IPv4 header (IHL=5), UDP proto 17, total length 28
    std::vector<std::byte> ip = {
        std::byte{0x45}, std::byte{0x00}, // ver 4, IHL 5
        std::byte{0x00}, std::byte{0x1C}, // Total length 28 (20 IP + 8 UDP)
        std::byte{0x00}, std::byte{0x01}, // Identification
        std::byte{0x40}, std::byte{0x00}, // Flags: DF set, MF not set, frag offset 0
        std::byte{0x40}, std::byte{0x11}, // TTL 64, Protocol 17 (UDP)
        std::byte{0x00}, std::byte{0x00}, // Checksum
        std::byte{192}, std::byte{168}, std::byte{1}, std::byte{1}, // Src
        std::byte{192}, std::byte{168}, std::byte{1}, std::byte{2}, // Dst
        // 8 bytes payload
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}
    };
    auto r1 = NetworkLayerParser::parse_ipv4(ip);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value().protocol, 17);
    REQUIRE_EQ(r1.value().ihl_bytes, 20);
    REQUIRE_FALSE(r1.value().is_fragmented);
    REQUIRE_EQ(r1.value().payload.size(), 8U);

    // 2. Fragmented IPv4 packet (MF bit set: 0x2000)
    std::vector<std::byte> ip_frag = ip;
    ip_frag[6] = std::byte{0x20}; // Set More Fragments bit
    auto r_frag = NetworkLayerParser::parse_ipv4(ip_frag);
    REQUIRE_TRUE(r_frag.has_value());
    REQUIRE_TRUE(r_frag.value().is_fragmented);

    // 3. Invalid IHL (< 5)
    std::vector<std::byte> ip_bad_ihl = ip;
    ip_bad_ihl[0] = std::byte{0x44}; // IHL=4
    auto r_bad_ihl = NetworkLayerParser::parse_ipv4(ip_bad_ihl);
    REQUIRE_FALSE(r_bad_ihl.has_value());
    REQUIRE_EQ(r_bad_ihl.error().code, ErrorCode::CorruptHeader);

    // 4. Non-IPv4 version (e.g. IPv6 = 6)
    std::vector<std::byte> ip_v6 = ip;
    ip_v6[0] = std::byte{0x65};
    auto r_v6 = NetworkLayerParser::parse_ipv4(ip_v6);
    REQUIRE_FALSE(r_v6.has_value());
    REQUIRE_EQ(r_v6.error().code, ErrorCode::UnsupportedVersion);
}

TEST_CASE(NetworkUdpDatagram) {
    // 8-byte header: src port 5000, dst port 5001, length 12 (8 hdr + 4 data)
    std::vector<std::byte> udp = {
        std::byte{0x13}, std::byte{0x88}, // src 5000
        std::byte{0x13}, std::byte{0x89}, // dst 5001
        std::byte{0x00}, std::byte{0x0C}, // len 12
        std::byte{0x00}, std::byte{0x00}, // chksum
        std::byte{'T'}, std::byte{'E'}, std::byte{'S'}, std::byte{'T'}
    };
    auto r1 = NetworkLayerParser::parse_udp(udp);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value().src_port, 5000);
    REQUIRE_EQ(r1.value().dst_port, 5001);
    REQUIRE_EQ(r1.value().length, 12);
    REQUIRE_EQ(r1.value().payload.size(), 4U);

    // Truncated UDP datagram: declares len 20, but only 12 bytes present
    std::vector<std::byte> udp_trunc = udp;
    udp_trunc[5] = std::byte{0x14}; // 20
    auto r_trunc = NetworkLayerParser::parse_udp(udp_trunc);
    REQUIRE_FALSE(r_trunc.has_value());
    REQUIRE_EQ(r_trunc.error().code, ErrorCode::InvalidLength);
}

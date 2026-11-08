#include "fastitch/pcap_source.hpp"
#include "tests/support/test_framework.hpp"

#include <vector>

using namespace fastitch;

TEST_CASE(PcapGlobalHeaderEndiannessAndMagic) {
    // 24-byte global header in Big-Endian:
    // magic=0xa1b2c3d4, v2.4, thiszone=0, sigfigs=0, snaplen=65535, network=1 (Ethernet)
    std::vector<std::byte> be_hdr = {
        std::byte{0xA1}, std::byte{0xB2}, std::byte{0xC3}, std::byte{0xD4}, // magic
        std::byte{0x00}, std::byte{0x02}, // v_major 2
        std::byte{0x00}, std::byte{0x04}, // v_minor 4
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // thiszone
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // sigfigs
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}, // snaplen 65535
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}  // network 1
    };

    PcapParser pcap_be(be_hdr);
    auto r_be = pcap_be.parse_global_header();
    REQUIRE_TRUE(r_be.has_value());
    REQUIRE_EQ(r_be.value().version_major, 2);
    REQUIRE_EQ(r_be.value().version_minor, 4);
    REQUIRE_EQ(r_be.value().snaplen, 65535U);
    REQUIRE_EQ(r_be.value().network, 1U);
    REQUIRE_FALSE(r_be.value().is_little_endian);
    REQUIRE_EQ(r_be.value().timestamp_precision, PcapTimestampPrecision::Microseconds);

    // 24-byte global header in Little-Endian:
    // magic=0xd4c3b2a1 on wire: [0xd4, 0xc3, 0xb2, 0xa1]
    std::vector<std::byte> le_hdr = {
        std::byte{0xD4}, std::byte{0xC3}, std::byte{0xB2}, std::byte{0xA1},
        std::byte{0x02}, std::byte{0x00},
        std::byte{0x04}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
    };

    PcapParser pcap_le(le_hdr);
    auto r_le = pcap_le.parse_global_header();
    REQUIRE_TRUE(r_le.has_value());
    REQUIRE_EQ(r_le.value().version_major, 2);
    REQUIRE_EQ(r_le.value().version_minor, 4);
    REQUIRE_EQ(r_le.value().snaplen, 65535U);
    REQUIRE_EQ(r_le.value().network, 1U);
    REQUIRE_TRUE(r_le.value().is_little_endian);

    // Unsupported Link Type (e.g. network = 101 raw IP)
    std::vector<std::byte> bad_link = be_hdr;
    bad_link[23] = std::byte{0x65}; // 101
    PcapParser pcap_bad(bad_link);
    auto r_bad = pcap_bad.parse_global_header();
    REQUIRE_FALSE(r_bad.has_value());
    REQUIRE_EQ(r_bad.error().code, ErrorCode::UnsupportedLinkType);
}

TEST_CASE(PcapPacketRecordsParsing) {
    // 24-byte BE global header + one 16-byte record header + 4 bytes data
    std::vector<std::byte> pcap_file = {
        std::byte{0xA1}, std::byte{0xB2}, std::byte{0xC3}, std::byte{0xD4},
        std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        // Packet 1 Record Header:
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0A}, // sec 10
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x14}, // usec 20
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04}, // incl_len 4
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04}, // orig_len 4
        // Packet 1 data:
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}
    };

    PcapParser parser(pcap_file);
    REQUIRE_TRUE(parser.parse_global_header().has_value());

    auto pkt1 = parser.next_packet();
    REQUIRE_TRUE(pkt1.has_value());
    REQUIRE_TRUE(pkt1.value().has_value());
    REQUIRE_EQ(pkt1.value()->ts_sec, 10U);
    REQUIRE_EQ(pkt1.value()->ts_subsec, 20U);
    REQUIRE_EQ(pkt1.value()->incl_len, 4U);
    REQUIRE_EQ(pkt1.value()->packet_data.size(), 4U);

    // EOF reached cleanly
    auto pkt2 = parser.next_packet();
    REQUIRE_TRUE(pkt2.has_value());
    REQUIRE_FALSE(pkt2.value().has_value());
}

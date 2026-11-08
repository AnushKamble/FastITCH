#include "fastitch/moldudp64.hpp"
#include "tests/support/test_framework.hpp"

#include <vector>

using namespace fastitch;

TEST_CASE(MoldUdp64HeaderAndSpecialCounts) {
    // 20-byte header: session "SESSION123", seq 100, count 2
    std::vector<std::byte> hdr_bytes = {
        std::byte{'S'}, std::byte{'E'}, std::byte{'S'}, std::byte{'S'}, std::byte{'I'},
        std::byte{'O'}, std::byte{'N'}, std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x64}, // seq 100
        std::byte{0x00}, std::byte{0x02}                                     // count 2
    };

    auto r1 = MoldUdp64Parser::parse_header(hdr_bytes);
    REQUIRE_TRUE(r1.has_value());
    REQUIRE_EQ(r1.value().sequence_number, 100ULL);
    REQUIRE_EQ(r1.value().message_count, 2U);
    REQUIRE_FALSE(r1.value().is_heartbeat());
    REQUIRE_FALSE(r1.value().is_end_of_session());

    // Heartbeat: count 0
    std::vector<std::byte> hb_bytes = hdr_bytes;
    hb_bytes[18] = std::byte{0x00};
    hb_bytes[19] = std::byte{0x00};
    auto r_hb = MoldUdp64Parser::parse_header(hb_bytes);
    REQUIRE_TRUE(r_hb.has_value());
    REQUIRE_TRUE(r_hb.value().is_heartbeat());

    // End-of-Session: count 0xFFFF
    std::vector<std::byte> eos_bytes = hdr_bytes;
    eos_bytes[18] = std::byte{0xFF};
    eos_bytes[19] = std::byte{0xFF};
    auto r_eos = MoldUdp64Parser::parse_header(eos_bytes);
    REQUIRE_TRUE(r_eos.has_value());
    REQUIRE_TRUE(r_eos.value().is_end_of_session());

    // Truncated header (< 20 bytes)
    auto r_short = MoldUdp64Parser::parse_header(std::span<const std::byte>(hdr_bytes.data(), 15));
    REQUIRE_FALSE(r_short.has_value());
    REQUIRE_EQ(r_short.error().code, ErrorCode::CorruptHeader);
}

TEST_CASE(MoldUdp64MessageBlocksParsing) {
    // 20-byte header + 2 blocks:
    // Block 1: len 3, data [1, 2, 3]
    // Block 2: len 2, data [4, 5]
    std::vector<std::byte> pkt = {
        std::byte{'S'}, std::byte{'E'}, std::byte{'S'}, std::byte{'S'}, std::byte{'I'},
        std::byte{'O'}, std::byte{'N'}, std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, // seq 1
        std::byte{0x00}, std::byte{0x02},                                     // count 2
        // Block 1:
        std::byte{0x00}, std::byte{0x03}, std::byte{1}, std::byte{2}, std::byte{3},
        // Block 2:
        std::byte{0x00}, std::byte{0x02}, std::byte{4}, std::byte{5}
    };

    auto hdr_res = MoldUdp64Parser::parse_header(pkt);
    REQUIRE_TRUE(hdr_res.has_value());

    auto blocks_res = MoldUdp64Parser::parse_message_blocks(pkt, hdr_res.value());
    REQUIRE_TRUE(blocks_res.has_value());
    const auto& blocks = blocks_res.value();
    REQUIRE_EQ(blocks.size(), 2U);
    REQUIRE_EQ(blocks[0].sequence_number, 1ULL);
    REQUIRE_EQ(blocks[0].payload.size(), 3U);
    REQUIRE_EQ(blocks[1].sequence_number, 2ULL);
    REQUIRE_EQ(blocks[1].payload.size(), 2U);

    // Trailing unexplained data after declared blocks
    std::vector<std::byte> pkt_trailing = pkt;
    pkt_trailing.push_back(std::byte{0xFF});
    auto blocks_trail = MoldUdp64Parser::parse_message_blocks(pkt_trailing, hdr_res.value());
    REQUIRE_FALSE(blocks_trail.has_value());
    REQUIRE_EQ(blocks_trail.error().code, ErrorCode::TrailingData);
}

TEST_CASE(MoldSequenceTrackerContinuityAndGaps) {
    MoldSequenceTracker tracker;

    MoldUdp64Header p1{
        .session = {'T', 'E', 'S', 'T'},
        .sequence_number = 1,
        .message_count = 3
    };
    // Expected seq progresses 1 -> 4
    REQUIRE_TRUE(tracker.track(p1).has_value());
    REQUIRE_EQ(tracker.expected_sequence(), 4ULL);
    REQUIRE_EQ(tracker.gap_count(), 0ULL);

    // Heartbeat does not alter expected sequence
    MoldUdp64Header hb{
        .session = {'T', 'E', 'S', 'T'},
        .sequence_number = 4,
        .message_count = 0
    };
    REQUIRE_TRUE(tracker.track(hb).has_value());
    REQUIRE_EQ(tracker.expected_sequence(), 4ULL);

    // Sequence Gap: incoming sequence is 8 instead of 4 (gap of 4 messages)
    MoldUdp64Header p_gap{
        .session = {'T', 'E', 'S', 'T'},
        .sequence_number = 8,
        .message_count = 2
    };
    auto gap_res = tracker.track(p_gap);
    REQUIRE_FALSE(gap_res.has_value());
    REQUIRE_EQ(gap_res.error().code, ErrorCode::SequenceGap);
    REQUIRE_EQ(tracker.gap_count(), 4ULL);
    REQUIRE_EQ(tracker.expected_sequence(), 10ULL);

    // Duplicate / old sequence (seq 8 received when expecting 10)
    auto dup_res = tracker.track(p_gap);
    REQUIRE_FALSE(dup_res.has_value());
    REQUIRE_EQ(dup_res.error().code, ErrorCode::DuplicateSequence);
    REQUIRE_EQ(tracker.duplicate_count(), 1ULL);
}

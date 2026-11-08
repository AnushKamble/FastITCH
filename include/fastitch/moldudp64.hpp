#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "fastitch/bytes.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

struct MoldUdp64Header {
    std::array<uint8_t, 10> session{};
    uint64_t sequence_number{0};
    uint16_t message_count{0};

    [[nodiscard]] bool is_heartbeat() const noexcept {
        return message_count == 0;
    }

    [[nodiscard]] bool is_end_of_session() const noexcept {
        return message_count == 0xFFFFu;
    }
};

struct MoldUdp64MessageBlock {
    std::span<const std::byte> payload;
    uint64_t sequence_number{0};
};

class MoldUdp64Parser {
public:
    static constexpr size_t kHeaderSize = 20;

    static Result<MoldUdp64Header> parse_header(std::span<const std::byte> payload, uint64_t offset = 0) noexcept {
        if (payload.size() < kHeaderSize) {
            return ParseError{SourceLayer::MoldUDP64, ErrorCode::CorruptHeader, offset, 0, "MoldUDP64 packet shorter than 20-byte header"};
        }

        MoldUdp64Header hdr;
        for (size_t i = 0; i < 10; ++i) {
            hdr.session[i] = static_cast<uint8_t>(payload[i]);
        }

        auto seq_res = ByteReader::read_be_u64(payload, 10);
        if (!seq_res) return seq_res.error();
        hdr.sequence_number = seq_res.value();

        auto count_res = ByteReader::read_be_u16(payload, 18);
        if (!count_res) return count_res.error();
        hdr.message_count = count_res.value();

        return hdr;
    }

    // Parses all message blocks in a MoldUDP64 packet payload
    static Result<std::vector<MoldUdp64MessageBlock>> parse_message_blocks(
        std::span<const std::byte> packet_bytes, const MoldUdp64Header& hdr, uint64_t offset = 0) {
        std::vector<MoldUdp64MessageBlock> blocks;

        if (hdr.is_heartbeat() || hdr.is_end_of_session()) {
            if (packet_bytes.size() != kHeaderSize) {
                return ParseError{SourceLayer::MoldUDP64, ErrorCode::TrailingData, offset + kHeaderSize, hdr.sequence_number,
                                  "Heartbeat or EOS packet has unexplained trailing data"};
            }
            return blocks;
        }

        blocks.reserve(hdr.message_count);
        size_t cursor = kHeaderSize;
        uint64_t current_seq = hdr.sequence_number;

        for (uint16_t i = 0; i < hdr.message_count; ++i) {
            if (packet_bytes.size() - cursor < 2) {
                return ParseError{SourceLayer::MoldUDP64, ErrorCode::UnexpectedEndOfInput, offset + cursor, current_seq,
                                  "Truncated MoldUDP64 message block length prefix"};
            }

            auto len_res = ByteReader::read_be_u16(packet_bytes, cursor);
            if (!len_res) return len_res.error();
            uint16_t block_len = len_res.value();
            cursor += 2;

            if (packet_bytes.size() - cursor < block_len) {
                return ParseError{SourceLayer::MoldUDP64, ErrorCode::InvalidLength, offset + cursor, current_seq,
                                  "Truncated MoldUDP64 message block payload"};
            }

            blocks.push_back(MoldUdp64MessageBlock{
                .payload = packet_bytes.subspan(cursor, block_len),
                .sequence_number = current_seq++
            });

            cursor += block_len;
        }

        if (cursor < packet_bytes.size()) {
            return ParseError{SourceLayer::MoldUDP64, ErrorCode::TrailingData, offset + cursor, current_seq,
                              "Unexplained trailing bytes after declared MoldUDP64 message blocks"};
        }

        return blocks;
    }
};

class MoldSequenceTracker {
public:
    void reset() noexcept {
        initialized_ = false;
        expected_sequence_ = 1;
        gap_count_ = 0;
        duplicate_count_ = 0;
        active_session_ = {};
    }

    [[nodiscard]] uint64_t expected_sequence() const noexcept {
        return expected_sequence_;
    }

    [[nodiscard]] uint64_t gap_count() const noexcept {
        return gap_count_;
    }

    [[nodiscard]] uint64_t duplicate_count() const noexcept {
        return duplicate_count_;
    }

    // Process incoming packet sequence
    Result<void> track(const MoldUdp64Header& hdr, uint64_t offset = 0) noexcept {
        if (!initialized_) {
            initialized_ = true;
            active_session_ = hdr.session;
            expected_sequence_ = hdr.sequence_number;
        } else if (hdr.session != active_session_) {
            // New session detected
            active_session_ = hdr.session;
            expected_sequence_ = hdr.sequence_number;
        }

        if (hdr.is_heartbeat() || hdr.is_end_of_session()) {
            return Result<void>();
        }

        if (hdr.sequence_number > expected_sequence_) {
            uint64_t gap = hdr.sequence_number - expected_sequence_;
            gap_count_ += gap;
            expected_sequence_ = hdr.sequence_number + hdr.message_count;
            return ParseError{SourceLayer::MoldUDP64, ErrorCode::SequenceGap, offset, hdr.sequence_number,
                              "Sequence gap detected: expected " + std::to_string(expected_sequence_ - gap - hdr.message_count) +
                              ", received " + std::to_string(hdr.sequence_number) + " (gap of " + std::to_string(gap) + " msgs)"};
        } else if (hdr.sequence_number < expected_sequence_) {
            ++duplicate_count_;
            return ParseError{SourceLayer::MoldUDP64, ErrorCode::DuplicateSequence, offset, hdr.sequence_number,
                              "Duplicate / old sequence received: expected " + std::to_string(expected_sequence_) +
                              ", received " + std::to_string(hdr.sequence_number)};
        }

        expected_sequence_ += hdr.message_count;
        return Result<void>();
    }

private:
    bool initialized_{false};
    uint64_t expected_sequence_{1};
    uint64_t gap_count_{0};
    uint64_t duplicate_count_{0};
    std::array<uint8_t, 10> active_session_{};
};

} // namespace fastitch

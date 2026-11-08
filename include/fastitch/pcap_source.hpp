#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "fastitch/bytes.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

enum class PcapTimestampPrecision : uint8_t {
    Microseconds,
    Nanoseconds
};

struct PcapGlobalHeader {
    uint32_t magic{0};
    uint16_t version_major{0};
    uint16_t version_minor{0};
    int32_t thiszone{0};
    uint32_t sigfigs{0};
    uint32_t snaplen{0};
    uint32_t network{0}; // Link-layer header type (1 = Ethernet)
    bool is_little_endian{false};
    PcapTimestampPrecision timestamp_precision{PcapTimestampPrecision::Microseconds};
};

struct PcapPacketRecord {
    uint32_t ts_sec{0};
    uint32_t ts_subsec{0};
    uint32_t incl_len{0};
    uint32_t orig_len{0};
    std::span<const std::byte> packet_data{};
    uint64_t source_offset{0};
};

class PcapParser {
public:
    static constexpr size_t kGlobalHeaderSize = 24;
    static constexpr size_t kRecordHeaderSize = 16;
    static constexpr uint32_t kLinkTypeEthernet = 1;

    explicit PcapParser(std::span<const std::byte> pcap_bytes) noexcept
        : data_(pcap_bytes), cursor_(0), packet_index_(0) {}

    Result<PcapGlobalHeader> parse_global_header() noexcept {
        if (data_.size() < kGlobalHeaderSize) {
            return ParseError{SourceLayer::PCAP, ErrorCode::CorruptHeader, 0, 0, "PCAP file shorter than 24-byte global header"};
        }

        uint32_t raw_magic = 0;
        std::memcpy(&raw_magic, data_.data(), 4);

        bool le = false;
        PcapTimestampPrecision prec = PcapTimestampPrecision::Microseconds;

        if (raw_magic == 0xa1b2c3d4U) {
            le = true;  // File written in host order (LE on x86)
            prec = PcapTimestampPrecision::Microseconds;
        } else if (raw_magic == 0xd4c3b2a1U) {
            le = false; // File written in opposite byte order (BE on x86)
            prec = PcapTimestampPrecision::Microseconds;
        } else if (raw_magic == 0xa1b23c4dU) {
            le = true;  // Nanosecond LE
            prec = PcapTimestampPrecision::Nanoseconds;
        } else if (raw_magic == 0x4d3cb2a1U) {
            le = false; // Nanosecond BE
            prec = PcapTimestampPrecision::Nanoseconds;
        } else {
            return ParseError{SourceLayer::PCAP, ErrorCode::CorruptHeader, 0, 0, "Unrecognized PCAP magic number"};
        }

        auto read_u16 = [this, le](size_t off) -> uint16_t {
            uint16_t v;
            std::memcpy(&v, data_.data() + off, 2);
            return le ? le16_to_host(v) : be16_to_host(v);
        };

        auto read_u32 = [this, le](size_t off) -> uint32_t {
            uint32_t v;
            std::memcpy(&v, data_.data() + off, 4);
            return le ? le32_to_host(v) : be32_to_host(v);
        };

        header_.magic = raw_magic;
        header_.is_little_endian = le;
        header_.timestamp_precision = prec;
        header_.version_major = read_u16(4);
        header_.version_minor = read_u16(6);
        header_.thiszone = static_cast<int32_t>(read_u32(8));
        header_.sigfigs = read_u32(12);
        header_.snaplen = read_u32(16);
        header_.network = read_u32(20);

        if (header_.network != kLinkTypeEthernet) {
            return ParseError{SourceLayer::PCAP, ErrorCode::UnsupportedLinkType, 20, 0,
                              "Unsupported PCAP link layer: expected 1 (Ethernet), got " + std::to_string(header_.network)};
        }

        cursor_ = kGlobalHeaderSize;
        return header_;
    }

    // Step to the next packet record
    Result<std::optional<PcapPacketRecord>> next_packet() noexcept {
        if (cursor_ == data_.size()) {
            return std::nullopt; // Clean EOF
        }

        if (data_.size() - cursor_ < kRecordHeaderSize) {
            return ParseError{SourceLayer::PCAP, ErrorCode::UnexpectedEndOfInput, cursor_, packet_index_,
                              "Truncated PCAP packet record header"};
        }

        bool le = header_.is_little_endian;
        auto read_u32 = [this, le](size_t off) -> uint32_t {
            uint32_t v;
            std::memcpy(&v, data_.data() + off, 4);
            return le ? le32_to_host(v) : be32_to_host(v);
        };

        uint32_t ts_sec = read_u32(cursor_);
        uint32_t ts_subsec = read_u32(cursor_ + 4);
        uint32_t incl_len = read_u32(cursor_ + 8);
        uint32_t orig_len = read_u32(cursor_ + 12);
        uint64_t record_offset = cursor_;

        cursor_ += kRecordHeaderSize;

        if (data_.size() - cursor_ < incl_len) {
            return ParseError{SourceLayer::PCAP, ErrorCode::InvalidLength, record_offset, packet_index_,
                              "Truncated PCAP packet body: declared " + std::to_string(incl_len) +
                              ", remaining " + std::to_string(data_.size() - cursor_)};
        }

        PcapPacketRecord record{
            .ts_sec = ts_sec,
            .ts_subsec = ts_subsec,
            .incl_len = incl_len,
            .orig_len = orig_len,
            .packet_data = data_.subspan(cursor_, incl_len),
            .source_offset = record_offset
        };

        cursor_ += incl_len;
        ++packet_index_;
        return record;
    }

    [[nodiscard]] uint64_t packet_index() const noexcept {
        return packet_index_;
    }

private:
    std::span<const std::byte> data_;
    size_t cursor_{0};
    uint64_t packet_index_{0};
    PcapGlobalHeader header_{};
};

struct PcapRunStats {
    uint64_t total_packets{0};
    uint64_t total_datagrams{0};
    uint64_t total_mold_messages{0};
    uint64_t sequence_gaps{0};
    bool session_complete{false};
    bool book_valid{true};
};

class OrderBookEngine;
Result<PcapRunStats> run_pcap_file(const std::string& path, OrderBookEngine& engine,
                                   std::string_view target_symbol = "", uint64_t print_events = 0,
                                   uint64_t max_messages = 0, bool strict_mode = false);

} // namespace fastitch

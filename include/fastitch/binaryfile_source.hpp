#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "fastitch/bytes.hpp"
#include "fastitch/mmap_file.hpp"
#include "fastitch/result.hpp"

namespace fastitch {

struct ItchMessageView {
    std::span<const std::byte> payload;
    uint64_t source_offset{0};
    uint64_t message_index{0};

    [[nodiscard]] uint8_t type_tag() const noexcept {
        return payload.empty() ? 0 : static_cast<uint8_t>(payload[0]);
    }
};

class BinaryFileParser {
public:
    explicit BinaryFileParser(std::span<const std::byte> data) noexcept
        : data_(data), offset_(0), message_index_(0), session_complete_(false) {}

    [[nodiscard]] bool is_session_complete() const noexcept {
        return session_complete_;
    }

    [[nodiscard]] uint64_t offset() const noexcept {
        return offset_;
    }

    [[nodiscard]] uint64_t message_index() const noexcept {
        return message_index_;
    }

    [[nodiscard]] size_t remaining_bytes() const noexcept {
        return offset_ <= data_.size() ? data_.size() - offset_ : 0;
    }

    // Step to the next framed ITCH message.
    // Returns:
    // - value with ItchMessageView on valid message
    // - value with std::nullopt on clean end-of-session (0x0000 marker reached)
    // - error on truncated prefix, truncated payload, missing terminator, or trailing unexplained data.
    Result<std::optional<ItchMessageView>> next_message() noexcept {
        if (session_complete_) {
            // Already finished cleanly; check for trailing data
            if (offset_ < data_.size()) {
                return ParseError{
                    SourceLayer::BinaryFile,
                    ErrorCode::TrailingData,
                    offset_,
                    message_index_,
                    "Unexpected data found after clean end-of-session marker"
                };
            }
            return std::nullopt;
        }

        if (offset_ == data_.size()) {
            // Reached EOF without encountering the 0x0000 end-of-session marker
            return ParseError{
                SourceLayer::BinaryFile,
                ErrorCode::MissingSessionTerminator,
                offset_,
                message_index_,
                "BinaryFILE stream ended without 0x0000 session terminator"
            };
        }

        // Must have at least 2 bytes for the length prefix
        if (data_.size() - offset_ < 2) {
            return ParseError{
                SourceLayer::BinaryFile,
                ErrorCode::UnexpectedEndOfInput,
                offset_,
                message_index_,
                "Truncated length prefix at offset " + std::to_string(offset_)
            };
        }

        auto len_res = ByteReader::read_be_u16(data_, offset_);
        if (!len_res) {
            return len_res.error();
        }

        uint16_t length = len_res.value();
        uint64_t current_msg_offset = offset_;

        if (length == 0) {
            // Clean End-Of-Session marker (0x0000)
            offset_ += 2;
            session_complete_ = true;

            if (offset_ < data_.size()) {
                return ParseError{
                    SourceLayer::BinaryFile,
                    ErrorCode::TrailingData,
                    offset_,
                    message_index_,
                    "Trailing bytes after end-of-session marker at offset " + std::to_string(offset_)
                };
            }
            return std::nullopt;
        }

        // Verify full payload exists
        size_t available_payload = data_.size() - (offset_ + 2);
        if (available_payload < length) {
            return ParseError{
                SourceLayer::BinaryFile,
                ErrorCode::InvalidLength,
                current_msg_offset,
                message_index_,
                "Payload truncated: expected " + std::to_string(length) +
                " bytes, only " + std::to_string(available_payload) + " available"
            };
        }

        std::span<const std::byte> payload = data_.subspan(offset_ + 2, length);
        offset_ += 2 + length;

        ItchMessageView view{
            .payload = payload,
            .source_offset = current_msg_offset,
            .message_index = message_index_++
        };

        return view;
    }

    void reset() noexcept {
        offset_ = 0;
        message_index_ = 0;
        session_complete_ = false;
    }

private:
    std::span<const std::byte> data_;
    uint64_t offset_{0};
    uint64_t message_index_{0};
    bool session_complete_{false};
};

// Buffered streaming reader for environments where mmap is unavailable or file exceeds memory
class BufferedBinaryFileReader {
public:
    static constexpr size_t kDefaultBufferSize = 64 * 1024; // 64 KB chunk

    explicit BufferedBinaryFileReader(const std::string& path, size_t chunk_size = kDefaultBufferSize)
        : path_(path), chunk_size_(chunk_size), stream_(path, std::ios::binary) {
        buffer_.reserve(chunk_size_ * 2);
    }

    [[nodiscard]] bool is_open() const noexcept {
        return stream_.is_open() && stream_.good();
    }

    [[nodiscard]] bool is_session_complete() const noexcept {
        return session_complete_;
    }

    Result<std::optional<std::vector<std::byte>>> next_message_payload() {
        if (session_complete_) {
            if (buffer_pos_ < buffer_.size() || stream_.peek() != EOF) {
                return ParseError{
                    SourceLayer::BinaryFile,
                    ErrorCode::TrailingData,
                    file_offset_,
                    message_index_,
                    "Trailing data after session terminator"
                };
            }
            return std::nullopt;
        }

        // Ensure at least 2 bytes in buffer for length prefix
        if (!refill_buffer_up_to(2)) {
            if (buffer_pos_ == buffer_.size()) {
                return ParseError{
                    SourceLayer::BinaryFile,
                    ErrorCode::MissingSessionTerminator,
                    file_offset_,
                    message_index_,
                    "Unexpected EOF without session terminator"
                };
            }
            return ParseError{
                SourceLayer::BinaryFile,
                ErrorCode::UnexpectedEndOfInput,
                file_offset_,
                message_index_,
                "Truncated length prefix"
            };
        }

        std::span<const std::byte> span(reinterpret_cast<const std::byte*>(buffer_.data()), buffer_.size());
        auto len_res = ByteReader::read_be_u16(span, buffer_pos_);
        if (!len_res) return len_res.error();

        uint16_t length = len_res.value();
        buffer_pos_ += 2;
        file_offset_ += 2;

        if (length == 0) {
            session_complete_ = true;
            return std::nullopt;
        }

        // Ensure length bytes in buffer
        if (!refill_buffer_up_to(length)) {
            return ParseError{
                SourceLayer::BinaryFile,
                ErrorCode::InvalidLength,
                file_offset_ - 2,
                message_index_,
                "Payload truncated in streaming reader"
            };
        }

        std::vector<std::byte> payload(length);
        std::memcpy(payload.data(), buffer_.data() + buffer_pos_, length);
        buffer_pos_ += length;
        file_offset_ += length;
        ++message_index_;

        return payload;
    }

private:
    bool refill_buffer_up_to(size_t required_bytes) {
        // Compact consumed bytes
        if (buffer_pos_ > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_pos_));
            buffer_pos_ = 0;
        }

        while (buffer_.size() < required_bytes && stream_.good()) {
            size_t before = buffer_.size();
            buffer_.resize(before + chunk_size_);
            stream_.read(reinterpret_cast<char*>(buffer_.data() + before), static_cast<std::streamsize>(chunk_size_));
            std::streamsize bytes_read = stream_.gcount();
            buffer_.resize(before + static_cast<size_t>(bytes_read));
            if (bytes_read == 0) break;
        }

        return buffer_.size() >= required_bytes;
    }

    std::string path_;
    size_t chunk_size_;
    std::ifstream stream_;
    std::vector<uint8_t> buffer_;
    size_t buffer_pos_{0};
    uint64_t file_offset_{0};
    uint64_t message_index_{0};
    bool session_complete_{false};
};

} // namespace fastitch

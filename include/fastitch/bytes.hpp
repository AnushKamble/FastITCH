#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "fastitch/result.hpp"

namespace fastitch {

// Portable byte swappers using compiler intrinsics where available
[[nodiscard]] constexpr uint16_t bswap16(uint16_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(val);
#else
    return static_cast<uint16_t>((val << 8) | (val >> 8));
#endif
}

[[nodiscard]] constexpr uint32_t bswap32(uint32_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return ((val & 0x000000FFu) << 24) |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0xFF000000u) >> 24);
#endif
}

[[nodiscard]] constexpr uint64_t bswap64(uint64_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#else
    return ((val & 0x00000000000000FFull) << 56) |
           ((val & 0x000000000000FF00ull) << 40) |
           ((val & 0x0000000000FF0000ull) << 24) |
           ((val & 0x00000000FF000000ull) << 8)  |
           ((val & 0x000000FF00000000ull) >> 8)  |
           ((val & 0x0000FF0000000000ull) >> 24) |
           ((val & 0x00FF000000000000ull) >> 40) |
           ((val & 0xFF00000000000000ull) >> 56);
#endif
}

// Convert Big-Endian to Host
[[nodiscard]] constexpr uint16_t be16_to_host(uint16_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return bswap16(val);
    } else {
        return val;
    }
}

[[nodiscard]] constexpr uint32_t be32_to_host(uint32_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return bswap32(val);
    } else {
        return val;
    }
}

[[nodiscard]] constexpr uint64_t be64_to_host(uint64_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return bswap64(val);
    } else {
        return val;
    }
}

// Convert Little-Endian to Host
[[nodiscard]] constexpr uint16_t le16_to_host(uint16_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return val;
    } else {
        return bswap16(val);
    }
}

[[nodiscard]] constexpr uint32_t le32_to_host(uint32_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return val;
    } else {
        return bswap32(val);
    }
}

[[nodiscard]] constexpr uint64_t le64_to_host(uint64_t val) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return val;
    } else {
        return bswap64(val);
    }
}

// Safe bounds-checked reading primitives
class ByteReader {
public:
    [[nodiscard]] static bool has_remaining(std::span<const std::byte> bytes, size_t offset, size_t width) noexcept {
        if (offset > bytes.size()) {
            return false;
        }
        return width <= (bytes.size() - offset);
    }

    [[nodiscard]] static Result<uint8_t> read_u8(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 1)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for u8"};
        }
        return static_cast<uint8_t>(bytes[offset]);
    }

    [[nodiscard]] static Result<uint16_t> read_be_u16(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 2)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for be_u16"};
        }
        uint16_t val;
        std::memcpy(&val, bytes.data() + offset, 2);
        return be16_to_host(val);
    }

    [[nodiscard]] static Result<uint32_t> read_be_u32(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 4)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for be_u32"};
        }
        uint32_t val;
        std::memcpy(&val, bytes.data() + offset, 4);
        return be32_to_host(val);
    }

    [[nodiscard]] static Result<uint64_t> read_be_u64(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 8)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for be_u64"};
        }
        uint64_t val;
        std::memcpy(&val, bytes.data() + offset, 8);
        return be64_to_host(val);
    }

    // NASDAQ ITCH 48-bit (6-byte) nanosecond timestamp
    [[nodiscard]] static Result<uint64_t> read_be_u48(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 6)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for be_u48"};
        }
        const auto* ptr = reinterpret_cast<const uint8_t*>(bytes.data() + offset);
        uint64_t val = (static_cast<uint64_t>(ptr[0]) << 40) |
                       (static_cast<uint64_t>(ptr[1]) << 32) |
                       (static_cast<uint64_t>(ptr[2]) << 24) |
                       (static_cast<uint64_t>(ptr[3]) << 16) |
                       (static_cast<uint64_t>(ptr[4]) << 8)  |
                       (static_cast<uint64_t>(ptr[5]));
        return val;
    }

    [[nodiscard]] static Result<uint16_t> read_le_u16(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 2)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for le_u16"};
        }
        uint16_t val;
        std::memcpy(&val, bytes.data() + offset, 2);
        return le16_to_host(val);
    }

    [[nodiscard]] static Result<uint32_t> read_le_u32(std::span<const std::byte> bytes, size_t offset) noexcept {
        if (!has_remaining(bytes, offset, 4)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for le_u32"};
        }
        uint32_t val;
        std::memcpy(&val, bytes.data() + offset, 4);
        return le32_to_host(val);
    }

    [[nodiscard]] static Result<std::span<const std::byte>> read_span(std::span<const std::byte> bytes, size_t offset, size_t len) noexcept {
        if (!has_remaining(bytes, offset, len)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for requested span"};
        }
        return bytes.subspan(offset, len);
    }

    // Read ASCII string, right-trimmed of spaces
    [[nodiscard]] static Result<std::string_view> read_ascii_trimmed(std::span<const std::byte> bytes, size_t offset, size_t len) noexcept {
        if (!has_remaining(bytes, offset, len)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for ascii string"};
        }
        const char* ptr = reinterpret_cast<const char*>(bytes.data() + offset);
        size_t actual_len = len;
        while (actual_len > 0 && ptr[actual_len - 1] == ' ') {
            --actual_len;
        }
        return std::string_view(ptr, actual_len);
    }

    // Read raw fixed-width ASCII string without trimming
    [[nodiscard]] static Result<std::string_view> read_ascii_exact(std::span<const std::byte> bytes, size_t offset, size_t len) noexcept {
        if (!has_remaining(bytes, offset, len)) {
            return ParseError{SourceLayer::RawBytes, ErrorCode::UnexpectedEndOfInput, offset, 0, "Buffer too short for ascii exact"};
        }
        return std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), len);
    }
};

} // namespace fastitch

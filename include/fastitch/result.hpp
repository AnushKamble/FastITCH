#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <utility>
#include <optional>

namespace fastitch {

enum class SourceLayer : uint8_t {
    None,
    RawBytes,
    BinaryFile,
    PCAP,
    Ethernet,
    IPv4,
    UDP,
    MoldUDP64,
    ITCHDecoder,
    OrderBook,
    IO
};

[[nodiscard]] constexpr std::string_view to_string(SourceLayer layer) noexcept {
    switch (layer) {
        case SourceLayer::None: return "None";
        case SourceLayer::RawBytes: return "RawBytes";
        case SourceLayer::BinaryFile: return "BinaryFile";
        case SourceLayer::PCAP: return "PCAP";
        case SourceLayer::Ethernet: return "Ethernet";
        case SourceLayer::IPv4: return "IPv4";
        case SourceLayer::UDP: return "UDP";
        case SourceLayer::MoldUDP64: return "MoldUDP64";
        case SourceLayer::ITCHDecoder: return "ITCHDecoder";
        case SourceLayer::OrderBook: return "OrderBook";
        case SourceLayer::IO: return "IO";
    }
    return "UnknownLayer";
}

enum class ErrorCode : uint16_t {
    Success = 0,
    UnexpectedEndOfInput,
    InvalidLength,
    UnsupportedVersion,
    UnsupportedLinkType,
    UnsupportedProtocol,
    IpFragmented,
    CorruptHeader,
    InvalidMessageLength,
    UnknownMessageType,
    InvalidSide,
    OrderNotFound,
    DuplicateOrderId,
    OverCancel,
    OverExecute,
    InvalidPrice,
    InvalidShares,
    SequenceGap,
    DuplicateSequence,
    OutOfOrderSequence,
    SessionMismatch,
    MissingSessionTerminator,
    UnexpectedSessionTerminator,
    TrailingData,
    IoError,
    SystemError
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::UnexpectedEndOfInput: return "UnexpectedEndOfInput";
        case ErrorCode::InvalidLength: return "InvalidLength";
        case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
        case ErrorCode::UnsupportedLinkType: return "UnsupportedLinkType";
        case ErrorCode::UnsupportedProtocol: return "UnsupportedProtocol";
        case ErrorCode::IpFragmented: return "IpFragmented";
        case ErrorCode::CorruptHeader: return "CorruptHeader";
        case ErrorCode::InvalidMessageLength: return "InvalidMessageLength";
        case ErrorCode::UnknownMessageType: return "UnknownMessageType";
        case ErrorCode::InvalidSide: return "InvalidSide";
        case ErrorCode::OrderNotFound: return "OrderNotFound";
        case ErrorCode::DuplicateOrderId: return "DuplicateOrderId";
        case ErrorCode::OverCancel: return "OverCancel";
        case ErrorCode::OverExecute: return "OverExecute";
        case ErrorCode::InvalidPrice: return "InvalidPrice";
        case ErrorCode::InvalidShares: return "InvalidShares";
        case ErrorCode::SequenceGap: return "SequenceGap";
        case ErrorCode::DuplicateSequence: return "DuplicateSequence";
        case ErrorCode::OutOfOrderSequence: return "OutOfOrderSequence";
        case ErrorCode::SessionMismatch: return "SessionMismatch";
        case ErrorCode::MissingSessionTerminator: return "MissingSessionTerminator";
        case ErrorCode::UnexpectedSessionTerminator: return "UnexpectedSessionTerminator";
        case ErrorCode::TrailingData: return "TrailingData";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::SystemError: return "SystemError";
    }
    return "UnknownError";
}

struct ParseError {
    SourceLayer layer{SourceLayer::None};
    ErrorCode code{ErrorCode::Success};
    uint64_t offset{0};
    uint64_t sequence{0};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return code == ErrorCode::Success;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return !ok();
    }
};

template <typename T>
class Result {
public:
    constexpr Result(const T& val) : data_(val) {}
    constexpr Result(T&& val) : data_(std::move(val)) {}

    template <typename U, typename = std::enable_if_t<std::is_constructible_v<T, U&&> && !std::is_same_v<std::decay_t<U>, Result<T>> && !std::is_same_v<std::decay_t<U>, ParseError>>>
    constexpr Result(U&& val) : data_(std::in_place_type<T>, std::forward<U>(val)) {}

    constexpr Result(const ParseError& err) : data_(err) {}
    constexpr Result(ParseError&& err) : data_(std::move(err)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] constexpr const T& value() const & {
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr T& value() & {
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr T&& value() && {
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] constexpr const ParseError& error() const & {
        return std::get<ParseError>(data_);
    }

    [[nodiscard]] constexpr ParseError& error() & {
        return std::get<ParseError>(data_);
    }

    [[nodiscard]] constexpr ParseError&& error() && {
        return std::get<ParseError>(std::move(data_));
    }

private:
    std::variant<T, ParseError> data_;
};

// Specialization for void
template <>
class Result<void> {
public:
    constexpr Result() : error_(std::nullopt) {}
    constexpr Result(const ParseError& err) : error_(err) {}
    constexpr Result(ParseError&& err) : error_(std::move(err)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return !error_.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] const ParseError& error() const & {
        return *error_;
    }

    [[nodiscard]] ParseError& error() & {
        return *error_;
    }

    [[nodiscard]] ParseError&& error() && {
        return *std::move(error_);
    }

private:
    std::optional<ParseError> error_;
};

} // namespace fastitch

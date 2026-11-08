#include "fastitch/binaryfile_source.hpp"
#include "fastitch/mmap_file.hpp"
#include "tests/support/test_framework.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

using namespace fastitch;

static std::vector<std::byte> make_binaryfile_stream(const std::vector<std::vector<std::byte>>& payloads, bool include_terminator = true, const std::vector<std::byte>& trailing = {}) {
    std::vector<std::byte> out;
    for (const auto& p : payloads) {
        uint16_t len = static_cast<uint16_t>(p.size());
        out.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
        out.push_back(static_cast<std::byte>(len & 0xFF));
        out.insert(out.end(), p.begin(), p.end());
    }
    if (include_terminator) {
        out.push_back(std::byte{0x00});
        out.push_back(std::byte{0x00});
    }
    out.insert(out.end(), trailing.begin(), trailing.end());
    return out;
}

TEST_CASE(BinaryFileEmptySession) {
    auto stream = make_binaryfile_stream({}); // only 0x00, 0x00
    BinaryFileParser parser(stream);

    auto msg_res = parser.next_message();
    REQUIRE_TRUE(msg_res.has_value());
    REQUIRE_FALSE(msg_res.value().has_value()); // nullopt indicates clean EOS
    REQUIRE_TRUE(parser.is_session_complete());
    REQUIRE_EQ(parser.offset(), 2ULL);
}

TEST_CASE(BinaryFileMultiMessageSession) {
    std::vector<std::byte> p1 = {std::byte{'S'}, std::byte{1}, std::byte{2}, std::byte{3}};
    std::vector<std::byte> p2 = {std::byte{'A'}, std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}};

    auto stream = make_binaryfile_stream({p1, p2});
    BinaryFileParser parser(stream);

    auto m1 = parser.next_message();
    REQUIRE_TRUE(m1.has_value());
    REQUIRE_TRUE(m1.value().has_value());
    REQUIRE_EQ(m1.value()->payload.size(), p1.size());
    REQUIRE_EQ(m1.value()->source_offset, 0ULL);
    REQUIRE_EQ(m1.value()->message_index, 0ULL);
    REQUIRE_EQ(m1.value()->type_tag(), static_cast<uint8_t>('S'));

    auto m2 = parser.next_message();
    REQUIRE_TRUE(m2.has_value());
    REQUIRE_TRUE(m2.value().has_value());
    REQUIRE_EQ(m2.value()->payload.size(), p2.size());
    REQUIRE_EQ(m2.value()->source_offset, 2ULL + p1.size());
    REQUIRE_EQ(m2.value()->message_index, 1ULL);
    REQUIRE_EQ(m2.value()->type_tag(), static_cast<uint8_t>('A'));

    auto m3 = parser.next_message();
    REQUIRE_TRUE(m3.has_value());
    REQUIRE_FALSE(m3.value().has_value()); // Clean EOS
    REQUIRE_TRUE(parser.is_session_complete());
}

TEST_CASE(BinaryFileMissingTerminator) {
    std::vector<std::byte> p1 = {std::byte{'S'}, std::byte{1}, std::byte{2}};
    // Stream ends abruptly without 0x0000
    auto stream = make_binaryfile_stream({p1}, false);
    BinaryFileParser parser(stream);

    auto m1 = parser.next_message();
    REQUIRE_TRUE(m1.has_value());

    auto m2 = parser.next_message();
    REQUIRE_FALSE(m2.has_value());
    REQUIRE_EQ(m2.error().code, ErrorCode::MissingSessionTerminator);
    REQUIRE_FALSE(parser.is_session_complete());
}

TEST_CASE(BinaryFilePrefixTruncation) {
    std::vector<std::byte> p1 = {std::byte{'S'}, std::byte{1}};
    auto stream = make_binaryfile_stream({p1}, false);
    // Add 1 solitary byte (truncated length prefix)
    stream.push_back(std::byte{0x01});

    BinaryFileParser parser(stream);
    auto m1 = parser.next_message();
    REQUIRE_TRUE(m1.has_value());

    auto m2 = parser.next_message();
    REQUIRE_FALSE(m2.has_value());
    REQUIRE_EQ(m2.error().code, ErrorCode::UnexpectedEndOfInput);
}

TEST_CASE(BinaryFilePayloadTruncation) {
    // Length prefix says 10 bytes, but only 4 bytes follow
    std::vector<std::byte> stream = {
        std::byte{0x00}, std::byte{0x0A}, // length 10
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}
    };

    BinaryFileParser parser(stream);
    auto m1 = parser.next_message();
    REQUIRE_FALSE(m1.has_value());
    REQUIRE_EQ(m1.error().code, ErrorCode::InvalidLength);
}

TEST_CASE(BinaryFileTrailingDataAfterTerminator) {
    std::vector<std::byte> p1 = {std::byte{'S'}, std::byte{1}};
    std::vector<std::byte> trailing = {std::byte{0xDE}, std::byte{0xAD}};
    auto stream = make_binaryfile_stream({p1}, true, trailing);

    BinaryFileParser parser(stream);
    auto m1 = parser.next_message();
    REQUIRE_TRUE(m1.has_value());

    auto m2 = parser.next_message();
    REQUIRE_FALSE(m2.has_value());
    REQUIRE_EQ(m2.error().code, ErrorCode::TrailingData);
}

TEST_CASE(MmapAndBufferedEquivalence) {
    std::string test_path = "/tmp/fastitch_test_session.bin";
    std::vector<std::byte> p1 = {std::byte{'A'}, std::byte{1}, std::byte{2}, std::byte{3}};
    std::vector<std::byte> p2 = {std::byte{'D'}, std::byte{4}, std::byte{5}};
    auto stream = make_binaryfile_stream({p1, p2}, true);

    // Write file to disk
    {
        std::ofstream out(test_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(stream.data()), static_cast<std::streamsize>(stream.size()));
    }

    // 1. Read via MmapFile
    auto mmap_res = MmapFile::open(test_path);
    REQUIRE_TRUE(mmap_res.has_value());
    REQUIRE_EQ(mmap_res.value().size(), stream.size());

    BinaryFileParser mmap_parser(mmap_res.value().bytes());
    std::vector<std::vector<std::byte>> mmap_payloads;
    while (true) {
        auto msg = mmap_parser.next_message();
        REQUIRE_TRUE(msg.has_value());
        if (!msg.value().has_value()) break;
        const auto& span = msg.value()->payload;
        mmap_payloads.emplace_back(span.begin(), span.end());
    }
    REQUIRE_TRUE(mmap_parser.is_session_complete());
    REQUIRE_EQ(mmap_payloads.size(), 2U);

    // 2. Read via BufferedBinaryFileReader
    BufferedBinaryFileReader buffered_reader(test_path, 4); // small chunk size (4 bytes) to test boundary crossings
    REQUIRE_TRUE(buffered_reader.is_open());
    std::vector<std::vector<std::byte>> buffered_payloads;
    while (true) {
        auto msg = buffered_reader.next_message_payload();
        REQUIRE_TRUE(msg.has_value());
        if (!msg.value().has_value()) break;
        buffered_payloads.push_back(msg.value().value());
    }
    REQUIRE_TRUE(buffered_reader.is_session_complete());
    REQUIRE_EQ(buffered_payloads.size(), 2U);

    // Verify equivalence
    REQUIRE_EQ(mmap_payloads[0], buffered_payloads[0]);
    REQUIRE_EQ(mmap_payloads[1], buffered_payloads[1]);

    std::remove(test_path.c_str());
}

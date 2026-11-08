#include "fastitch/binaryfile_source.hpp"
#include "fastitch/bytes.hpp"
#include "fastitch/ethernet_ipv4_udp.hpp"
#include "fastitch/itch_decoder.hpp"
#include "fastitch/moldudp64.hpp"
#include "fastitch/order_book.hpp"
#include "fastitch/pcap_source.hpp"
#include "tests/support/test_framework.hpp"

#include <random>
#include <vector>

using namespace fastitch;

// Deterministic LCG random generator for reproducible fuzz tests
class SimpleRng {
public:
    explicit SimpleRng(uint64_t seed = 1337) : state_(seed) {}

    uint32_t next_u32() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state_ >> 32);
    }

    uint8_t next_u8() {
        return static_cast<uint8_t>(next_u32() & 0xFF);
    }

    std::vector<std::byte> random_bytes(size_t len) {
        std::vector<std::byte> buf(len);
        for (size_t i = 0; i < len; ++i) {
            buf[i] = static_cast<std::byte>(next_u8());
        }
        return buf;
    }

private:
    uint64_t state_;
};

TEST_CASE(FuzzBinaryFileParserMalformedInput) {
    SimpleRng rng(0xDEADBEEF);
    // 500 randomized mutated buffers of lengths 0 to 2048 bytes
    for (size_t iter = 0; iter < 500; ++iter) {
        size_t len = rng.next_u32() % 2048;
        auto bytes = rng.random_bytes(len);

        BinaryFileParser parser(bytes);
        while (true) {
            auto res = parser.next_message();
            if (!res || !res.value().has_value()) {
                break;
            }
        }
    }
}

TEST_CASE(FuzzItchDecoderRandomPayloads) {
    SimpleRng rng(0xCAFEBABE);
    ItchDecoder strict_decoder(UnknownTypePolicy::Strict);
    ItchDecoder perm_decoder(UnknownTypePolicy::Permissive);

    // 1000 randomized payloads
    for (size_t iter = 0; iter < 1000; ++iter) {
        size_t len = rng.next_u32() % 128;
        auto bytes = rng.random_bytes(len);

        // Neither strict nor permissive decoder must crash or exhibit UB
        auto r1 = strict_decoder.decode(bytes);
        auto r2 = perm_decoder.decode(bytes);
        (void)r1;
        (void)r2;
    }
}

TEST_CASE(FuzzNetworkLayersMalformedPackets) {
    SimpleRng rng(0x12345678);

    for (size_t iter = 0; iter < 500; ++iter) {
        size_t len = rng.next_u32() % 1500;
        auto bytes = rng.random_bytes(len);

        auto eth = NetworkLayerParser::parse_ethernet(bytes);
        if (eth) {
            auto ip = NetworkLayerParser::parse_ipv4(eth.value().payload);
            if (ip) {
                auto udp = NetworkLayerParser::parse_udp(ip.value().payload);
                if (udp) {
                    auto mold_hdr = MoldUdp64Parser::parse_header(udp.value().payload);
                    if (mold_hdr) {
                        auto blocks = MoldUdp64Parser::parse_message_blocks(udp.value().payload, mold_hdr.value());
                        (void)blocks;
                    }
                }
            }
        }
    }
}

TEST_CASE(FuzzPcapParserRandomStreams) {
    SimpleRng rng(0x98765432);

    for (size_t iter = 0; iter < 200; ++iter) {
        size_t len = rng.next_u32() % 4096;
        auto bytes = rng.random_bytes(len);

        PcapParser pcap(bytes);
        auto hdr = pcap.parse_global_header();
        if (hdr) {
            while (true) {
                auto pkt = pcap.next_packet();
                if (!pkt || !pkt.value().has_value()) break;
            }
        }
    }
}

TEST_CASE(FuzzOrderBookEngineStateIntegrity) {
    // Generate valid random sequence of adds and removes, verifying invariants
    OrderBookEngine engine;
    SimpleRng rng(0x55AA55AA);
    uint16_t loc = 1;
    std::vector<uint64_t> order_ids;

    for (size_t iter = 0; iter < 1000; ++iter) {
        uint32_t action = rng.next_u32() % 4;

        if (action == 0 || order_ids.empty()) {
            // Add order
            uint64_t id = 10000 + iter;
            Side side = (rng.next_u32() % 2 == 0) ? Side::Buy : Side::Sell;
            uint32_t shares = 10 + (rng.next_u32() % 500);
            uint32_t price_raw = 1000000 + (rng.next_u32() % 100) * 10000;

            MsgAddOrder a{
                .header = {.message_type = 'A', .stock_locate = loc},
                .order_reference_number = id,
                .buy_sell_indicator = side,
                .shares = shares,
                .stock = "AAPL",
                .price = Price(price_raw)
            };
            REQUIRE_TRUE(engine.apply(DecodedItchMessage{a}).has_value());
            order_ids.push_back(id);
        } else if (action == 1) {
            // Cancel partial shares
            size_t idx = rng.next_u32() % order_ids.size();
            uint64_t id = order_ids[idx];
            if (engine.active_orders().contains(id)) {
                uint32_t rem = engine.active_orders().at(id).remaining_shares;
                uint32_t cancel_shares = (rem > 1) ? (1 + (rng.next_u32() % (rem - 1))) : 1;
                MsgOrderCancel x{
                    .header = {.message_type = 'X', .stock_locate = loc},
                    .order_reference_number = id,
                    .canceled_shares = cancel_shares
                };
                REQUIRE_TRUE(engine.apply(DecodedItchMessage{x}).has_value());
            }
        } else {
            // Delete order
            size_t idx = rng.next_u32() % order_ids.size();
            uint64_t id = order_ids[idx];
            if (engine.active_orders().contains(id)) {
                MsgOrderDelete d{
                    .header = {.message_type = 'D', .stock_locate = loc},
                    .order_reference_number = id
                };
                REQUIRE_TRUE(engine.apply(DecodedItchMessage{d}).has_value());
            }
            order_ids.erase(order_ids.begin() + static_cast<std::ptrdiff_t>(idx));
        }

        // Periodically verify invariants
        if (iter % 100 == 0) {
            REQUIRE_TRUE(engine.verify_invariants());
        }
    }

    REQUIRE_TRUE(engine.verify_invariants());
}

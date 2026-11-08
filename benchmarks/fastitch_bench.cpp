#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "fastitch/binaryfile_source.hpp"
#include "fastitch/bytes.hpp"
#include "fastitch/ethernet_ipv4_udp.hpp"
#include "fastitch/itch_decoder.hpp"
#include "fastitch/moldudp64.hpp"
#include "fastitch/normalizer.hpp"
#include "fastitch/order_book.hpp"
#include "fastitch/pcap_source.hpp"
#include "fastitch/protocol.hpp"

using namespace fastitch;

template <typename T>
inline void do_not_optimize(const T& val) {
    asm volatile("" : : "g"(val) : "memory");
}

struct BenchTrialStats {
    std::string name;
    uint64_t operations{0};
    uint64_t total_bytes{0};
    std::vector<double> elapsed_nanos;

    [[nodiscard]] double median_ns() const {
        auto sorted = elapsed_nanos;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    [[nodiscard]] double mean_ns() const {
        double sum = std::accumulate(elapsed_nanos.begin(), elapsed_nanos.end(), 0.0);
        return sum / static_cast<double>(elapsed_nanos.size());
    }

    [[nodiscard]] double stddev_ns() const {
        double m = mean_ns();
        double accum = 0.0;
        for (double d : elapsed_nanos) {
            accum += (d - m) * (d - m);
        }
        return std::sqrt(accum / static_cast<double>(elapsed_nanos.size()));
    }

    [[nodiscard]] double ops_per_sec() const {
        double med_s = median_ns() / 1e9;
        return static_cast<double>(operations) / med_s;
    }

    [[nodiscard]] double ns_per_op() const {
        return median_ns() / static_cast<double>(operations);
    }

    [[nodiscard]] double mb_per_sec() const {
        double med_s = median_ns() / 1e9;
        return (static_cast<double>(total_bytes) / (1024.0 * 1024.0)) / med_s;
    }
};

inline std::byte to_b(uint8_t val) {
    return static_cast<std::byte>(val);
}

// Generate realistic message mix in memory:
// 100,000 messages (60% Adds, 20% Executions, 10% Cancels, 5% Deletes, 5% Replaces)
struct SyntheticWorkload {
    std::vector<std::vector<std::byte>> raw_payloads;
    std::vector<std::byte> binaryfile_stream;
    std::vector<DecodedItchMessage> predecoded_messages;
    size_t total_payload_bytes{0};
};

SyntheticWorkload generate_workload(size_t num_messages) {
    SyntheticWorkload wl;
    wl.raw_payloads.reserve(num_messages);
    wl.predecoded_messages.reserve(num_messages);

    uint64_t order_id = 1000;
    std::vector<uint64_t> active_ids;
    active_ids.reserve(num_messages);

    ItchDecoder decoder;

    for (size_t i = 0; i < num_messages; ++i) {
        std::vector<std::byte> payload;
        size_t r = i % 100;
        uint64_t ts = 34200000000000ULL + i * 1000ULL;
        uint16_t loc = 1 + static_cast<uint16_t>(i % 10);

        if (r < 60 || active_ids.empty()) {
            // Add Order (36 bytes)
            payload.resize(36);
            payload[0] = to_b('A');
            // locate
            payload[1] = to_b(static_cast<uint8_t>((loc >> 8) & 0xFF));
            payload[2] = to_b(static_cast<uint8_t>(loc & 0xFF));
            // tracking
            payload[3] = to_b(0); payload[4] = to_b(1);
            // timestamp (6 bytes)
            for (size_t b = 0; b < 6; ++b) payload[5 + b] = to_b(static_cast<uint8_t>((ts >> ((5 - b) * 8)) & 0xFF));
            // order_id
            for (size_t b = 0; b < 8; ++b) payload[11 + b] = to_b(static_cast<uint8_t>((order_id >> ((7 - b) * 8)) & 0xFF));
            // buy/sell
            payload[19] = (i % 2 == 0) ? to_b('B') : to_b('S');
            // shares: 100
            payload[20] = to_b(0); payload[21] = to_b(0); payload[22] = to_b(0); payload[23] = to_b(100);
            // stock "AAPL    "
            std::memcpy(payload.data() + 24, "AAPL    ", 8);
            // price: $150.00 = 1500000
            uint32_t px = 1500000 + static_cast<uint32_t>((i % 20) * 1000);
            for (size_t b = 0; b < 4; ++b) payload[32 + b] = to_b(static_cast<uint8_t>((px >> ((3 - b) * 8)) & 0xFF));

            active_ids.push_back(order_id++);
        } else if (r < 80) {
            // Execution (31 bytes)
            uint64_t target_id = active_ids.back();
            payload.resize(31);
            payload[0] = to_b('E');
            payload[1] = to_b(static_cast<uint8_t>((loc >> 8) & 0xFF));
            payload[2] = to_b(static_cast<uint8_t>(loc & 0xFF));
            payload[3] = to_b(0); payload[4] = to_b(2);
            for (size_t b = 0; b < 6; ++b) payload[5 + b] = to_b(static_cast<uint8_t>((ts >> ((5 - b) * 8)) & 0xFF));
            for (size_t b = 0; b < 8; ++b) payload[11 + b] = to_b(static_cast<uint8_t>((target_id >> ((7 - b) * 8)) & 0xFF));
            // shares 50
            payload[19] = to_b(0); payload[20] = to_b(0); payload[21] = to_b(0); payload[22] = to_b(50);
            // match num
            uint64_t match = 90000 + i;
            for (size_t b = 0; b < 8; ++b) payload[23 + b] = to_b(static_cast<uint8_t>((match >> ((7 - b) * 8)) & 0xFF));
        } else if (r < 90) {
            // Cancel (23 bytes)
            uint64_t target_id = active_ids.back();
            payload.resize(23);
            payload[0] = to_b('X');
            payload[1] = to_b(static_cast<uint8_t>((loc >> 8) & 0xFF));
            payload[2] = to_b(static_cast<uint8_t>(loc & 0xFF));
            payload[3] = to_b(0); payload[4] = to_b(3);
            for (size_t b = 0; b < 6; ++b) payload[5 + b] = to_b(static_cast<uint8_t>((ts >> ((5 - b) * 8)) & 0xFF));
            for (size_t b = 0; b < 8; ++b) payload[11 + b] = to_b(static_cast<uint8_t>((target_id >> ((7 - b) * 8)) & 0xFF));
            payload[19] = to_b(0); payload[20] = to_b(0); payload[21] = to_b(0); payload[22] = to_b(25);
        } else {
            // Delete (19 bytes)
            uint64_t target_id = active_ids.back();
            active_ids.pop_back();
            payload.resize(19);
            payload[0] = to_b('D');
            payload[1] = to_b(static_cast<uint8_t>((loc >> 8) & 0xFF));
            payload[2] = to_b(static_cast<uint8_t>(loc & 0xFF));
            payload[3] = to_b(0); payload[4] = to_b(4);
            for (size_t b = 0; b < 6; ++b) payload[5 + b] = to_b(static_cast<uint8_t>((ts >> ((5 - b) * 8)) & 0xFF));
            for (size_t b = 0; b < 8; ++b) payload[11 + b] = to_b(static_cast<uint8_t>((target_id >> ((7 - b) * 8)) & 0xFF));
        }

        wl.total_payload_bytes += payload.size();

        // Pack into BinaryFILE stream
        uint16_t plen = static_cast<uint16_t>(payload.size());
        wl.binaryfile_stream.push_back(to_b(static_cast<uint8_t>((plen >> 8) & 0xFF)));
        wl.binaryfile_stream.push_back(to_b(static_cast<uint8_t>(plen & 0xFF)));
        wl.binaryfile_stream.insert(wl.binaryfile_stream.end(), payload.begin(), payload.end());

        // Predecode message for book apply benchmark
        auto dec = decoder.decode(payload, 0, i);
        if (dec) {
            wl.predecoded_messages.push_back(dec.value());
        }

        wl.raw_payloads.push_back(std::move(payload));
    }

    // Terminal marker for binaryfile
    wl.binaryfile_stream.push_back(to_b(0));
    wl.binaryfile_stream.push_back(to_b(0));

    return wl;
}

int main(int argc, char** argv) {
    size_t num_messages = 500000;
    size_t trials = 5;

    if (argc > 1) {
        num_messages = std::strtoull(argv[1], nullptr, 10);
    }

    std::cout << "FastITCH Benchmark Suite (C++20)\n";
    std::cout << "Messages per trial: " << num_messages << "\n";
    std::cout << "Trials per stage:   " << trials << " (with warm-up)\n\n";

    std::cout << "Generating synthetic workload..." << std::flush;
    auto workload = generate_workload(num_messages);
    std::cout << " Done (" << workload.binaryfile_stream.size() << " bytes).\n\n";

    // 1. Primitive Decode-Only Benchmark
    BenchTrialStats stats_decode{"1. ITCH Decode-Only", num_messages, workload.total_payload_bytes, {}};
    ItchDecoder decoder;

    // Warm-up
    {
        uint64_t checksum = 0;
        for (const auto& p : workload.raw_payloads) {
            auto res = decoder.decode(p);
            checksum += res ? 1ULL : 0ULL;
        }
        do_not_optimize(checksum);
    }

    for (size_t t = 0; t < trials; ++t) {
        uint64_t checksum = 0;
        auto start = std::chrono::steady_clock::now();
        for (const auto& p : workload.raw_payloads) {
            auto res = decoder.decode(p);
            checksum += res ? 1ULL : 0ULL;
        }
        auto end = std::chrono::steady_clock::now();
        do_not_optimize(checksum);
        double nanos = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        stats_decode.elapsed_nanos.push_back(nanos);
    }

    // 2. BinaryFILE Framing + Decode Benchmark
    BenchTrialStats stats_framing{"2. BinaryFILE Framing + Decode", num_messages, workload.binaryfile_stream.size(), {}};
    for (size_t t = 0; t < trials; ++t) {
        BinaryFileParser parser(workload.binaryfile_stream);
        uint64_t checksum = 0;
        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto msg = parser.next_message();
            if (!msg || !msg.value().has_value()) break;
            auto dec = decoder.decode(msg.value()->payload);
            checksum += dec ? 1ULL : 0ULL;
        }
        auto end = std::chrono::steady_clock::now();
        do_not_optimize(checksum);
        double nanos = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        stats_framing.elapsed_nanos.push_back(nanos);
    }

    // 3. Order Book Apply Benchmark (predecoded events)
    BenchTrialStats stats_book{"3. Order Book Apply (L3->L1)", workload.predecoded_messages.size(), workload.total_payload_bytes, {}};
    for (size_t t = 0; t < trials; ++t) {
        OrderBookEngine engine;
        uint64_t checksum = 0;
        auto start = std::chrono::steady_clock::now();
        for (const auto& msg : workload.predecoded_messages) {
            auto r = engine.apply(msg);
            checksum += r ? 1ULL : 0ULL;
        }
        auto end = std::chrono::steady_clock::now();
        checksum += engine.active_order_count();
        do_not_optimize(checksum);
        double nanos = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        stats_book.elapsed_nanos.push_back(nanos);
    }

    // 4. End-to-End Replay Benchmark (Framing + Decode + Book + Null Sink)
    BenchTrialStats stats_e2e{"4. End-to-End Replay (BinaryFILE)", num_messages, workload.binaryfile_stream.size(), {}};
    for (size_t t = 0; t < trials; ++t) {
        BinaryFileParser parser(workload.binaryfile_stream);
        OrderBookEngine engine;
        NullEventSink sink;
        uint64_t checksum = 0;

        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto msg = parser.next_message();
            if (!msg || !msg.value().has_value()) break;
            auto dec = decoder.decode(msg.value()->payload);
            if (dec) {
                engine.apply(dec.value());
                auto ev = EventNormalizer::normalize(dec.value(), msg.value()->message_index);
                sink.on_event(ev);
                checksum += 1ULL;
            }
        }
        auto end = std::chrono::steady_clock::now();
        checksum += engine.active_order_count();
        do_not_optimize(checksum);
        double nanos = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        stats_e2e.elapsed_nanos.push_back(nanos);
    }

    // Output Benchmark Results Table
    std::vector<BenchTrialStats> all_stats = {stats_decode, stats_framing, stats_book, stats_e2e};

    std::cout << std::left << std::setw(34) << "Benchmark Category"
              << std::right << std::setw(16) << "Throughput"
              << std::setw(16) << "Latency (med)"
              << std::setw(14) << "MB/sec"
              << std::setw(14) << "Std Dev\n";
    std::cout << std::string(94, '=') << "\n";

    for (const auto& s : all_stats) {
        std::cout << std::left << std::setw(34) << s.name
                  << std::right << std::setw(12) << std::fixed << std::setprecision(2) << (s.ops_per_sec() / 1e6) << " M/s"
                  << std::setw(13) << std::fixed << std::setprecision(1) << s.ns_per_op() << " ns"
                  << std::setw(11) << std::fixed << std::setprecision(1) << s.mb_per_sec() << " MB/s"
                  << std::setw(11) << std::fixed << std::setprecision(1) << (s.stddev_ns() / 1e6) << " ms\n";
    }
    std::cout << std::string(94, '=') << "\n\n";

    // Write machine-readable JSON results
    std::ofstream jout("results/benchmark_results.json");
    if (jout.is_open()) {
        jout << "{\n";
        jout << "  \"messages_per_trial\": " << num_messages << ",\n";
        jout << "  \"trials_count\": " << trials << ",\n";
        jout << "  \"benchmarks\": [\n";
        for (size_t i = 0; i < all_stats.size(); ++i) {
            const auto& s = all_stats[i];
            jout << "    {\n";
            jout << "      \"name\": \"" << s.name << "\",\n";
            jout << "      \"operations\": " << s.operations << ",\n";
            jout << "      \"median_ns_per_op\": " << s.ns_per_op() << ",\n";
            jout << "      \"throughput_msgs_per_sec\": " << s.ops_per_sec() << ",\n";
            jout << "      \"throughput_mb_per_sec\": " << s.mb_per_sec() << "\n";
            jout << "    }" << (i + 1 < all_stats.size() ? "," : "") << "\n";
        }
        jout << "  ]\n";
        jout << "}\n";
    }

    return 0;
}

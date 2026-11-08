#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fastitch/binaryfile_source.hpp"
#include "fastitch/bytes.hpp"
#include "fastitch/itch_decoder.hpp"
#include "fastitch/mmap_file.hpp"
#include "fastitch/normalizer.hpp"
#include "fastitch/order_book.hpp"
#include "fastitch/protocol.hpp"
#include "fastitch/pcap_source.hpp"

using namespace fastitch;

void print_help(const char* prog) {
    std::cout << "FastITCH (QD-02) — Zero-Copy NASDAQ TotalView-ITCH 5.0 Feed Handler\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --input <path>            Path to input BinaryFILE or PCAP capture\n"
              << "  --format <fmt>            Source format: auto | binaryfile | pcap (default: auto)\n"
              << "  --symbol <sym>            Filter or display BBO for specific ticker (e.g. AAPL)\n"
              << "  --print-events <N>        Print normalized details for first N events (default: 0)\n"
              << "  --max-messages <N>        Stop processing after N messages (default: 0 = unlimited)\n"
              << "  --summary                 Print summary statistics and final Top-of-Book (BBO)\n"
              << "  --stats-json <path>       Output machine-readable execution statistics to JSON file\n"
              << "  --strict                  Halt immediately on malformed message or sequence gap\n"
              << "  --allow-unknown           Permissively skip unknown message types instead of erroring\n"
              << "  --list-message-types      Display all 23 official ITCH 5.0 message types in registry\n"
              << "  --help                    Show this help message and exit\n";
}

void print_message_types() {
    std::cout << "NASDAQ TotalView-ITCH 5.0 Versioned Message Catalogue ("
              << kMessageCatalogue.size() << " official types):\n\n";
    std::cout << std::left << std::setw(6) << "Tag"
              << std::setw(32) << "Message Name"
              << std::setw(14) << "Wire Length"
              << "Alters Resting Book\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& entry : kMessageCatalogue) {
        std::cout << " '" << entry.tag << "'  "
                  << std::setw(32) << entry.name
                  << std::setw(14) << (std::to_string(entry.wire_length) + " bytes")
                  << (entry.alters_resting_book ? "YES (L3)" : "NO") << "\n";
    }
}

bool detect_is_pcap(std::span<const std::byte> bytes) {
    if (bytes.size() < 4) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), 4);
    // Standard PCAP magic numbers:
    // 0xa1b2c3d4 (microsecond, native / BE)
    // 0xd4c3b2a1 (microsecond, swapped / LE)
    // 0xa1b23c4d (nanosecond, native / BE)
    // 0x4d3cb2a1 (nanosecond, swapped / LE)
    return (magic == 0xa1b2c3d4U || magic == 0xd4c3b2a1U ||
            magic == 0xa1b23c4dU || magic == 0x4d3cb2a1U);
}

int main(int argc, char** argv) {
    std::string input_path;
    std::string format_str = "auto";
    std::string target_symbol;
    std::string stats_json_path;
    uint64_t print_events = 0;
    uint64_t max_messages = 0;
    bool show_summary = false;
    bool strict_mode = false;
    bool allow_unknown = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--list-message-types") {
            print_message_types();
            return 0;
        } else if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format_str = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            target_symbol = argv[++i];
        } else if (arg == "--print-events" && i + 1 < argc) {
            print_events = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--max-messages" && i + 1 < argc) {
            max_messages = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--summary") {
            show_summary = true;
        } else if (arg == "--stats-json" && i + 1 < argc) {
            stats_json_path = argv[++i];
        } else if (arg == "--strict") {
            strict_mode = true;
        } else if (arg == "--allow-unknown") {
            allow_unknown = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\nUse --help for usage.\n";
            return 1;
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: --input <path> is required.\nUse --help for usage.\n";
        return 1;
    }

    auto mmap_res = MmapFile::open(input_path);
    if (!mmap_res) {
        std::cerr << "Error opening file: " << mmap_res.error().message << "\n";
        return 1;
    }

    const auto& file = mmap_res.value();
    std::span<const std::byte> file_bytes = file.bytes();

    std::string resolved_format = format_str;
    if (resolved_format == "auto") {
        if (detect_is_pcap(file_bytes)) {
            resolved_format = "pcap";
        } else {
            resolved_format = "binaryfile";
        }
    }

    OrderBookEngine book_engine;
    CountingEventSink event_sink;

    uint64_t message_count = 0;
    std::unordered_map<char, uint64_t> type_counts;
    bool session_complete = false;
    std::string error_message;

    auto start_time = std::chrono::steady_clock::now();

    if (resolved_format == "binaryfile") {
        BinaryFileParser parser(file_bytes);
        ItchDecoder decoder(allow_unknown ? UnknownTypePolicy::Permissive : UnknownTypePolicy::Strict);

        while (true) {
            if (max_messages > 0 && message_count >= max_messages) {
                break;
            }

            auto msg_view_res = parser.next_message();
            if (!msg_view_res) {
                error_message = msg_view_res.error().message;
                if (strict_mode) {
                    book_engine.invalidate();
                    std::cerr << "Strict error at offset " << msg_view_res.error().offset
                              << ": " << error_message << "\n";
                    break;
                }
                // Under permissive mode on error, stop further parsing but record issue
                break;
            }

            if (!msg_view_res.value().has_value()) {
                // Clean end of session marker 0x0000 reached
                session_complete = true;
                break;
            }

            const auto& view = msg_view_res.value().value();
            auto decoded_res = decoder.decode(view.payload, view.source_offset, view.message_index);
            if (!decoded_res) {
                error_message = decoded_res.error().message;
                if (strict_mode) {
                    book_engine.invalidate();
                    std::cerr << "Strict decode error: " << error_message << "\n";
                    break;
                }
                continue;
            }

            const auto& decoded_msg = decoded_res.value();
            uint8_t tag = view.type_tag();
            type_counts[static_cast<char>(tag)]++;
            ++message_count;

            // Apply to order book engine
            auto apply_res = book_engine.apply(decoded_msg, view.source_offset, view.message_index);
            if (!apply_res && strict_mode) {
                std::cerr << "Strict book error: " << apply_res.error().message << "\n";
                break;
            }

            // Normalize and dispatch to sink
            auto norm_event = EventNormalizer::normalize(decoded_msg, view.message_index);
            event_sink.on_event(norm_event);

            // Optional rate-limited event printing
            if (print_events > 0 && message_count <= print_events) {
                if (target_symbol.empty() || norm_event.stock == target_symbol) {
                    std::cout << "[EVENT #" << std::setw(6) << message_count << "] "
                              << std::setw(12) << to_string(norm_event.kind)
                              << " Sym: " << std::setw(6) << (norm_event.stock.empty() ? "-" : norm_event.stock)
                              << " Shares: " << std::setw(6) << norm_event.shares
                              << " Px: " << std::fixed << std::setprecision(4) << norm_event.price.to_double()
                              << " OrderID: " << norm_event.order_reference << "\n";
                }
            }
        }
    } else if (resolved_format == "pcap") {
        auto pcap_res = run_pcap_file(input_path, book_engine, target_symbol, print_events, max_messages, strict_mode);
        if (!pcap_res) {
            std::cerr << "PCAP replay error: " << pcap_res.error().message << "\n";
            book_engine.invalidate();
            error_message = pcap_res.error().message;
        } else {
            const auto& stats = pcap_res.value();
            message_count = stats.total_mold_messages;
            session_complete = stats.session_complete;
            if (!stats.book_valid) {
                book_engine.invalidate();
            }
        }
    } else {
        std::cerr << "Unsupported format: " << resolved_format << "\n";
        return 1;
    }

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    bool is_book_valid = book_engine.is_valid();

    // Summary presentation
    if (show_summary) {
        std::cout << "\n================= FastITCH Replay Summary =================\n";
        std::cout << "Input File:         " << input_path << "\n";
        std::cout << "Format:             " << resolved_format << "\n";
        std::cout << "Total Messages:     " << message_count << "\n";
        std::cout << "Active Resting:     " << book_engine.active_order_count() << "\n";
        std::cout << "Elapsed Time:       " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
        if (elapsed_ms > 0) {
            double msgs_per_sec = (static_cast<double>(message_count) / (elapsed_ms / 1000.0));
            std::cout << "Throughput:         " << std::fixed << std::setprecision(2)
                      << (msgs_per_sec / 1e6) << " M msgs/sec\n";
        }
        std::cout << "Session Status:     " << (session_complete ? "COMPLETE (Clean 0x0000 EOS)" : "INCOMPLETE / UNTERMINATED") << "\n";
        std::cout << "Book Integrity:     " << (is_book_valid ? "VALID (Verified Invariants)" : "INVALID (Corruption or Gaps Detected)") << "\n";

        if (!target_symbol.empty()) {
            const auto* book = book_engine.find_book_by_symbol(target_symbol);
            std::cout << "\n--- Top-of-Book (BBO) for " << target_symbol << " ---\n";
            if (book) {
                auto bbo = book->bbo();
                std::cout << "Bid: ";
                if (bbo.bid.valid) {
                    std::cout << "$" << std::fixed << std::setprecision(4) << bbo.bid.price.to_double()
                              << " x " << bbo.bid.total_shares << " shares (" << bbo.bid.order_count << " orders)\n";
                } else {
                    std::cout << "[NO BID]\n";
                }

                std::cout << "Ask: ";
                if (bbo.ask.valid) {
                    std::cout << "$" << std::fixed << std::setprecision(4) << bbo.ask.price.to_double()
                              << " x " << bbo.ask.total_shares << " shares (" << bbo.ask.order_count << " orders)\n";
                } else {
                    std::cout << "[NO ASK]\n";
                }

                if (bbo.has_two_sided_quote()) {
                    double spread = bbo.ask.price.to_double() - bbo.bid.price.to_double();
                    std::cout << "Spread:             $" << std::fixed << std::setprecision(4) << spread << "\n";
                }
            } else {
                std::cout << "Symbol " << target_symbol << " not found in feed.\n";
            }
        }
        std::cout << "===========================================================\n";
    }

    // Machine-readable JSON output
    if (!stats_json_path.empty()) {
        std::ofstream jout(stats_json_path);
        if (jout.is_open()) {
            jout << "{\n";
            jout << "  \"file_path\": \"" << input_path << "\",\n";
            jout << "  \"format\": \"" << resolved_format << "\",\n";
            jout << "  \"session_complete\": " << (session_complete ? "true" : "false") << ",\n";
            jout << "  \"book_valid\": " << (is_book_valid ? "true" : "false") << ",\n";
            jout << "  \"total_messages\": " << message_count << ",\n";
            jout << "  \"active_orders\": " << book_engine.active_order_count() << ",\n";
            jout << "  \"elapsed_ms\": " << elapsed_ms << ",\n";

            if (!target_symbol.empty()) {
                const auto* book = book_engine.find_book_by_symbol(target_symbol);
                jout << "  \"target_symbol\": \"" << target_symbol << "\",\n";
                if (book) {
                    auto bbo = book->bbo();
                    jout << "  \"bbo\": {\n";
                    jout << "    \"bid_valid\": " << (bbo.bid.valid ? "true" : "false") << ",\n";
                    jout << "    \"bid_price\": " << bbo.bid.price.to_double() << ",\n";
                    jout << "    \"bid_shares\": " << bbo.bid.total_shares << ",\n";
                    jout << "    \"ask_valid\": " << (bbo.ask.valid ? "true" : "false") << ",\n";
                    jout << "    \"ask_price\": " << bbo.ask.price.to_double() << ",\n";
                    jout << "    \"ask_shares\": " << bbo.ask.total_shares << "\n";
                    jout << "  },\n";
                }
            }

            jout << "  \"message_counts\": {\n";
            size_t idx = 0;
            for (const auto& [tag, count] : type_counts) {
                jout << "    \"" << tag << "\": " << count;
                if (++idx < type_counts.size()) jout << ",";
                jout << "\n";
            }
            jout << "  }\n";
            jout << "}\n";
        }
    }

    if (strict_mode && (!is_book_valid || !error_message.empty())) {
        return 1;
    }

    return 0;
}

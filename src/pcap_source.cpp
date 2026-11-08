#include "fastitch/pcap_source.hpp"
#include "fastitch/ethernet_ipv4_udp.hpp"
#include "fastitch/moldudp64.hpp"
#include "fastitch/itch_decoder.hpp"
#include "fastitch/order_book.hpp"
#include "fastitch/normalizer.hpp"
#include "fastitch/mmap_file.hpp"

#include <iostream>
#include <iomanip>

namespace fastitch {

Result<PcapRunStats> run_pcap_file(const std::string& path, OrderBookEngine& engine,
                                   std::string_view target_symbol, uint64_t print_events,
                                   uint64_t max_messages, bool strict_mode) {
    auto mmap_res = MmapFile::open(path);
    if (!mmap_res) return mmap_res.error();

    const auto& file = mmap_res.value();
    PcapParser pcap(file.bytes());

    auto global_hdr_res = pcap.parse_global_header();
    if (!global_hdr_res) return global_hdr_res.error();

    PcapRunStats stats;
    MoldSequenceTracker seq_tracker;
    ItchDecoder decoder(UnknownTypePolicy::Permissive);

    uint64_t total_messages_processed = 0;

    while (true) {
        if (max_messages > 0 && total_messages_processed >= max_messages) {
            break;
        }

        auto pkt_res = pcap.next_packet();
        if (!pkt_res) {
            if (strict_mode) {
                engine.invalidate();
                stats.book_valid = false;
                return pkt_res.error();
            }
            break;
        }

        if (!pkt_res.value().has_value()) {
            // Clean EOF of PCAP
            break;
        }

        const auto& pkt = pkt_res.value().value();
        ++stats.total_packets;

        // 1. Ethernet
        auto eth_res = NetworkLayerParser::parse_ethernet(pkt.packet_data, pkt.source_offset);
        if (!eth_res) {
            if (strict_mode) return eth_res.error();
            continue;
        }
        if (eth_res.value().ether_type != NetworkLayerParser::kEtherTypeIpv4) {
            // Non-IPv4, safely skip
            continue;
        }

        // 2. IPv4
        auto ip_res = NetworkLayerParser::parse_ipv4(eth_res.value().payload, pkt.source_offset);
        if (!ip_res) {
            if (strict_mode) return ip_res.error();
            continue;
        }
        if (ip_res.value().is_fragmented) {
            // IP fragmentation detected: do not parse partial datagram as MoldUDP64
            if (strict_mode) {
                engine.invalidate();
                stats.book_valid = false;
                return ParseError{SourceLayer::IPv4, ErrorCode::IpFragmented, pkt.source_offset, stats.total_packets,
                                  "Fragmented IP datagram encountered in PCAP capture"};
            }
            continue;
        }
        if (ip_res.value().protocol != NetworkLayerParser::kIpProtocolUdp) {
            // Non-UDP, safely skip
            continue;
        }

        // 3. UDP
        auto udp_res = NetworkLayerParser::parse_udp(ip_res.value().payload, pkt.source_offset);
        if (!udp_res) {
            if (strict_mode) return udp_res.error();
            continue;
        }
        ++stats.total_datagrams;

        // 4. MoldUDP64 Header
        auto mold_hdr_res = MoldUdp64Parser::parse_header(udp_res.value().payload, pkt.source_offset);
        if (!mold_hdr_res) {
            if (strict_mode) return mold_hdr_res.error();
            continue;
        }

        const auto& mold_hdr = mold_hdr_res.value();
        if (mold_hdr.is_end_of_session()) {
            stats.session_complete = true;
            continue;
        }
        if (mold_hdr.is_heartbeat()) {
            continue;
        }

        // Sequence Tracking
        auto seq_res = seq_tracker.track(mold_hdr, pkt.source_offset);
        if (!seq_res) {
            stats.sequence_gaps += seq_tracker.gap_count();
            engine.invalidate();
            stats.book_valid = false;
            if (strict_mode) {
                return seq_res.error();
            }
        }

        // 5. MoldUDP64 Message Blocks
        auto blocks_res = MoldUdp64Parser::parse_message_blocks(udp_res.value().payload, mold_hdr, pkt.source_offset);
        if (!blocks_res) {
            if (strict_mode) return blocks_res.error();
            continue;
        }

        for (const auto& block : blocks_res.value()) {
            if (max_messages > 0 && total_messages_processed >= max_messages) {
                break;
            }

            auto dec_res = decoder.decode(block.payload, pkt.source_offset, block.sequence_number);
            if (!dec_res) {
                if (strict_mode) return dec_res.error();
                continue;
            }

            ++stats.total_mold_messages;
            ++total_messages_processed;

            // Apply to Order Book
            engine.apply(dec_res.value(), pkt.source_offset, block.sequence_number);

            // Normalize
            auto norm_event = EventNormalizer::normalize(dec_res.value(), block.sequence_number);

            if (print_events > 0 && total_messages_processed <= print_events) {
                if (target_symbol.empty() || norm_event.stock == target_symbol) {
                    std::cout << "[MOLD #" << std::setw(6) << block.sequence_number << "] "
                              << std::setw(12) << to_string(norm_event.kind)
                              << " Sym: " << std::setw(6) << (norm_event.stock.empty() ? "-" : norm_event.stock)
                              << " Shares: " << std::setw(6) << norm_event.shares
                              << " Px: " << std::fixed << std::setprecision(4) << norm_event.price.to_double()
                              << "\n";
                }
            }
        }
    }

    stats.book_valid = engine.is_valid();
    return stats;
}

} // namespace fastitch

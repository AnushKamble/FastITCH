# Test Fixtures

This directory contains deterministic synthetic fixtures used by unit tests, integration tests, and CLI demonstrations.

## Generation
All fixtures in this directory are synthetic and can be regenerated deterministically:
```bash
python3 tools/generate_fixtures.py fixtures
```

## Generated Files
- `synthetic_session.bin`: Uncompressed BinaryFILE stream containing 12 messages across administrative, order lifecycle (A, F, E, X, U), and trade prints (P), terminating with a clean `0x0000` end-of-session marker.
- `synthetic_incomplete.bin`: BinaryFILE stream truncated prior to receiving the `0x0000` session terminator to verify truncation detection.
- `synthetic_moldudp64.pcap`: Classic PCAP capture containing Ethernet, IPv4, UDP, and MoldUDP64 downstream packets with a sequence continuity check, heartbeat, and end-of-session packet.
- `synthetic_gap.pcap`: PCAP capture containing a deliberate MoldUDP64 sequence gap (seq 1..2 followed by seq 10..10) to verify sequence gap detection and order-book invalidation.

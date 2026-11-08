# FastITCH Testing and Validation Strategy

## 1. Testing Philosophy
In low-latency quantitative finance systems, software failures cause direct capital loss. FastITCH enforces a multi-tier testing methodology:
1. **Bit-Exact Golden Unit Tests:** Every official protocol field and message type is tested against known bit patterns.
2. **Bounds & Truncation Invariance:** Every parser helper handles buffer bounds with checked arithmetic; truncated packets, missing terminators, and trailing bytes are detected and classified.
3. **Market State Invariant Verification:** State mutations are verified against a mathematical model (`OrderBookEngine::verify_invariants()`).
4. **Fuzz Testing & Property Testing:** Randomized malformed byte streams are passed through all decoding layers under AddressSanitizer and UndefinedBehaviorSanitizer to guarantee memory safety.
5. **Deterministic Integration Tests:** Synthetic session and capture files exercise the complete end-to-end replay pipeline without proprietary exchange data.

## 2. Test Suite Breakdown

| Suite | File | Tests | Purpose |
|---|---|:---:|---|
| Safe Byte Primitives | `tests/unit/test_bytes.cpp` | 10 | Bounds checks, big/little endian swapping, 48-bit timestamp decode, ASCII field trimming |
| Result & Error Model | `tests/unit/test_result.cpp` | 4 | Structured errors, error code to string, value/void results |
| BinaryFILE Source | `tests/unit/test_binaryfile.cpp` | 7 | Framing, EOS 0x0000, prefix/payload truncation, trailing bytes, mmap & buffered equivalence |
| ITCH 5.0 Message Registry | `tests/unit/test_itch_decoder.cpp` | 26 | Golden fixture for all 23 official message types (S, R, H, Y, L, V, W, K, J, h, A, F, E, C, X, D, U, P, Q, B, I, N, O), exact lengths, unknown tag policies |
| Order Book Engine | `tests/unit/test_order_book.cpp` | 9 | Add, Execute (E & C), Cancel (X), Delete (D), Replace (U), trade isolation (P, Q, B), best-level deletion, BBO shifts |
| Network Protocols | `tests/unit/test_network.cpp` | 3 | Ethernet (0-2 VLAN tags), IPv4 (IHL, total length, fragment detection), UDP length bounds |
| MoldUDP64 Envelope | `tests/unit/test_moldudp64.cpp` | 3 | 20-byte downstream header, heartbeat (count 0), EOS (count 0xFFFF), message blocks, sequence gap tracking |
| Classic PCAP Parser | `tests/unit/test_pcap.cpp` | 2 | BE & LE magic detection, microsecond & nanosecond timestamps, packet record reading, link type validation |
| Randomized Malformed Fuzzing | `tests/property/test_fuzz_malformed.cpp` | 5 | In-process fuzzing across BinaryFILE, ITCH decoder, network stack, PCAP, and random order lifecycle sequences |

## 3. How to Run the Test Suite

### Running Unit Tests & Sanitizers
```bash
# Debug build with ASan and UBSan
cmake -B build_debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build_debug
./build_debug/fastitch_tests
ctest --test-dir build_debug --output-on-failure

# Release build
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
./build_release/fastitch_tests
ctest --test-dir build_release --output-on-failure
```

### Running End-to-End Fixture Replay
```bash
# Generate deterministic synthetic fixtures
python3 tools/generate_fixtures.py fixtures

# Replay BinaryFILE session
./build_debug/fastitch --input fixtures/synthetic_session.bin --symbol AAPL --summary

# Replay MoldUDP64 PCAP capture
./build_debug/fastitch --input fixtures/synthetic_moldudp64.pcap --symbol AAPL --summary

# Test sequence gap detection
./build_debug/fastitch --input fixtures/synthetic_gap.pcap --symbol AAPL --summary
```

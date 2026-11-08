# FastITCH Technical Design Decisions

This document details the critical architectural and algorithmic trade-offs made during the design and implementation of FastITCH.

## 1. Zero-Copy Span Parsing vs. `#pragma pack(1)` Struct Overlay

### The Problem
Low-latency feed handlers often attempt to cast raw network byte buffers directly to packed structs:
```cpp
const auto* msg = reinterpret_cast<const AddOrderMsg*>(buffer);
```
While superficially fast, this pattern has severe real-world flaws:
1. **Undefined Behavior (UB):** Reading unaligned multi-byte members (e.g. `uint64_t` or `uint32_t` on odd byte offsets) violates the C++ standard and causes hardware traps or performance degradation on many CPU architectures.
2. **Buffer Overruns:** Struct casting does not bounds-check. A truncated network packet could cause reads beyond the page boundary, resulting in segmentation faults.
3. **Endianness:** Packed struct members cannot handle big-endian exchange data on little-endian CPUs without explicit field-by-field byte swapping.

### FastITCH Approach
FastITCH uses non-owning `std::span<const std::byte>` with inline, bounds-checked primitive readers (`read_be_u16`, `read_be_u32`, `read_be_u64`, `read_be_u48`).
- Compiler intrinsics (`__builtin_bswap32`, `__builtin_bswap64`) are used on little-endian platforms.
- Compilers optimize these short inline helpers into single register loads and `bswap` instructions, achieving zero overhead without undefined behavior or alignment hazards.

## 2. Fixed-Point Integer Prices

### The Problem
Using `float` or `double` for market prices introduces binary floating-point representation errors (e.g. `100.10` becomes `100.099999999999994`), leading to incorrect price comparison and non-deterministic order book state.

### FastITCH Approach
ITCH 5.0 specifies `Price (4)` as an unsigned 32-bit integer with 4 implied decimal places (i.e. tick size = $0.0001). FastITCH maintains all internal prices as raw unsigned 32-bit tick integers (`uint32_t price_raw`). Price comparison is exact integer comparison. Conversion to floating point or formatted strings is deferred strictly to the user display and serialization boundaries.

## 3. Order Book Hierarchy: L3 State to L1 (BBO) Derivation

### The Problem
A common novice shortcut is maintaining only a map of symbol to current best bid/ask, updating it on Add Order messages. However, NASDAQ ITCH is an order-driven (Level 3) feed:
- Modify messages (`E`, `C`, `X`, `D`, `U`) reference earlier orders by a 64-bit `Order Reference Number` without repeating the price or side (except Replace `U`, which supplies new price and shares but inherits side and locate).
- When the last order at the best price is cancelled or executed, the next best resting price level must become the new BBO.

### FastITCH Approach
1. **Active Order Index:** `std::unordered_map<uint64_t, OrderRecord>` maps each active `Order Reference Number` to its current state (`stock_locate`, `side`, `price_raw`, `remaining_shares`).
2. **Aggregated Price Levels:** Per `StockLocate` (0–65,535), bids are maintained in a `std::map<uint32_t, PriceLevel, std::greater<>>` (descending order) and asks in a `std::map<uint32_t, PriceLevel, std::less<>>` (ascending order).
3. **Top-of-Book Derivation:** BBO is simply the first non-empty element of the respective maps (`bids.begin()` and `asks.begin()`), guaranteeing instant and correct BBO updates upon order additions, reductions, cancellations, and deletions.

## 4. MoldUDP64 Sequence Gap Policy

### The Problem
In UDP packet capture replay, packets can be dropped by switches, OS buffers, or capture cards. If a sequence gap occurs, the feed handler has missed order creations, executions, or cancellations.

### FastITCH Approach
FastITCH tracks `next_expected_sequence`. Upon receiving a packet with `sequence > next_expected_sequence`:
- The gap size is computed and logged.
- In **strict mode** (`--strict`), processing halts immediately with a clear error.
- In **permissive diagnostic mode**, processing continues to allow feed analysis, but the internal book state is flagged as `untrusted/invalid`. The CLI and JSON outputs prominently mark the BBO as `INVALID (SEQUENCE GAP DETECTED)` and refuse to present it as verified market state.

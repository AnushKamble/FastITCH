# FastITCH Benchmark Methodology & Performance Report

## 1. System & Environment Metadata
- **CPU:** AMD Ryzen 9 8940HX (16 physical cores, 32 hardware threads)
- **Base/Boost Clock:** 3.3 GHz / Up to 5.2 GHz
- **Operating System:** Linux 6.18.33.1 WSL2 (Ubuntu 22.04 LTS x86_64)
- **Compiler:** GCC 11.4.0
- **Build Configuration:** CMake Release (`-O3 -DNDEBUG -std=c++20`)
- **Strict Compilation Flags:** `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow`
- **Timing Primitive:** `std::chrono::steady_clock` (monotonic nanosecond precision)
- **Trials per Benchmark:** 5 measured trials with a dedicated warm-up run
- **Workload:** 500,000 synthetic ITCH 5.0 messages with representative mix:
  - 60% Add Orders (`A` and `F`)
  - 20% Order Executions (`E`)
  - 10% Cancellations (`X`)
  - 5% Deletions (`D`)
  - 5% Replaces (`U`)
- **Total Workload Bytes:** 17,000,002 bytes (~17 MB)

## 2. Measured Benchmark Results

| Benchmark Category | Throughput (msgs/sec) | Median Latency (ns/msg) | Bandwidth (MB/sec) | Standard Deviation (ms) |
|---|:---:|:---:|:---:|:---:|
| **1. ITCH Decode-Only** | **18.57 M msgs/sec** | **53.9 ns** | **566.7 MB/s** | 0.6 ms |
| **2. BinaryFILE Framing + Decode** | **15.94 M msgs/sec** | **62.7 ns** | **516.8 MB/s** | 0.6 ms |
| **3. Order Book Apply (L3 -> L1 BBO)** | **17.55 M msgs/sec** | **57.0 ns** | **535.7 MB/s** | 19.6 ms |
| **4. End-to-End Replay (Framing + Decode + Book)** | **8.02 M msgs/sec** | **124.8 ns** | **259.9 MB/s** | 1.1 ms |

## 3. Analysis & Key Takeaways
1. **Decode-Only Speed:** The bounds-checked span reader and branch-free switch dispatcher achieve **18.57 million messages per second** (53.9 ns per message) without direct struct overlays.
2. **BinaryFILE Framing Cost:** Big-endian 16-bit length extraction and session state checks add only **8.8 ns per message** (from 53.9 ns to 62.7 ns).
3. **L3 Reconstruction Overhead:** Updating the active order map and ordered price level maps operates at **17.55 million updates per second** (57.0 ns per mutation).
4. **End-to-End Replay Pipeline:** Combining whole-file framing, parsing, state mutation, and event dispatch yields **8.02 million messages per second** (124.8 ns end-to-end), reconstructing the full market state at over 250 MB/s.

## 4. Benchmark Reproduction
```bash
# Build the release benchmark binary
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --target fastitch_bench

# Run across 500,000 messages
./build_release/fastitch_bench 500000
```

# FastITCH Benchmarks

This directory contains the reproduction harnesses and performance measurement tooling for FastITCH.

## Structure
- `fastitch_bench.cpp`: Multi-stage microbenchmark executable evaluating:
  1. **Decode-Only:** Isolated byte-span parsing across all ITCH 5.0 types.
  2. **Framing + Decode:** NASDAQ BinaryFILE 2-byte BE framing and payload parsing.
  3. **Order Book Apply:** Active order map tracking, price-level aggregation, and BBO derivation.
  4. **End-to-End Replay:** Ingestion from memory buffer through book reconstruction to event sink.

## How to Reproduce
```bash
# 1. Configure and build Release with -O3
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --target fastitch_bench

# 2. Execute benchmark with 500,000 messages (or custom message count)
./build_release/fastitch_bench 500000
```

Results are printed to stdout and saved in machine-readable JSON format at `results/benchmark_results.json`.

#include "fastitch/spsc_ring_buffer.hpp"
#include "tests/support/test_framework.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace fastitch;

TEST_CASE(SpscSingleThreadBasicOperations) {
    SpscRingBuffer<int> ring(4); // rounds to 4
    REQUIRE_EQ(ring.capacity(), 4U);
    REQUIRE_TRUE(ring.empty());
    REQUIRE_EQ(ring.size(), 0U);

    int val = 0;
    REQUIRE_FALSE(ring.try_pop(val));

    REQUIRE_TRUE(ring.try_push(10));
    REQUIRE_TRUE(ring.try_push(20));
    REQUIRE_TRUE(ring.try_push(30));
    REQUIRE_TRUE(ring.try_push(40));

    // Full: 5th push must fail
    REQUIRE_FALSE(ring.try_push(50));
    REQUIRE_EQ(ring.size(), 4U);

    REQUIRE_TRUE(ring.try_pop(val));
    REQUIRE_EQ(val, 10);
    REQUIRE_TRUE(ring.try_pop(val));
    REQUIRE_EQ(val, 20);

    // Can push again
    REQUIRE_TRUE(ring.try_push(50));
    REQUIRE_TRUE(ring.try_pop(val));
    REQUIRE_EQ(val, 30);
    REQUIRE_TRUE(ring.try_pop(val));
    REQUIRE_EQ(val, 40);
    REQUIRE_TRUE(ring.try_pop(val));
    REQUIRE_EQ(val, 50);

    REQUIRE_TRUE(ring.empty());
}

TEST_CASE(SpscRingWraparound) {
    SpscRingBuffer<uint64_t> ring(8);
    // Push and pop 10,000 items to force many wraparounds
    for (uint64_t i = 0; i < 10000; ++i) {
        REQUIRE_TRUE(ring.try_push(i));
        uint64_t out = 0;
        REQUIRE_TRUE(ring.try_pop(out));
        REQUIRE_EQ(out, i);
    }
    REQUIRE_TRUE(ring.empty());
}

TEST_CASE(SpscMultiThreadedProducerConsumer) {
    constexpr size_t kTotalItems = 100000;
    SpscRingBuffer<uint64_t> ring(1024);

    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        for (uint64_t i = 1; i <= kTotalItems; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::vector<uint64_t> consumed;
    consumed.reserve(kTotalItems);

    std::thread consumer([&]() {
        uint64_t val = 0;
        while (!producer_done.load(std::memory_order_acquire) || !ring.empty()) {
            if (ring.try_pop(val)) {
                consumed.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE_EQ(consumed.size(), kTotalItems);
    for (size_t i = 0; i < kTotalItems; ++i) {
        REQUIRE_EQ(consumed[i], static_cast<uint64_t>(i + 1));
    }
}

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace fastitch {

// Cache line size constant to prevent false sharing
#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
#else
constexpr size_t hardware_destructive_interference_size = 64;
#endif

template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity) {
        // Enforce power-of-two capacity for fast bitwise masking
        size_t cap = 1;
        while (cap < capacity) {
            cap <<= 1;
        }
        capacity_ = cap;
        mask_ = capacity_ - 1;
        buffer_ = new T[capacity_];
    }

    ~SpscRingBuffer() {
        delete[] buffer_;
    }

    // Move-only semantics
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    // Non-blocking try_push (called by producer only)
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_cached_;

        if (current_head - current_tail >= capacity_) {
            // Re-read tail with acquire ordering to check if consumer caught up
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if (current_head - tail_cached_ >= capacity_) {
                return false; // Queue full
            }
        }

        buffer_[current_head & mask_] = item;
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_push(T&& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_cached_;

        if (current_head - current_tail >= capacity_) {
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if (current_head - tail_cached_ >= capacity_) {
                return false;
            }
        }

        buffer_[current_head & mask_] = std::move(item);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Non-blocking try_pop (called by consumer only)
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_cached_;

        if (current_tail == current_head) {
            // Re-read head with acquire ordering
            head_cached_ = head_.load(std::memory_order_acquire);
            if (current_tail == head_cached_) {
                return false; // Queue empty
            }
        }

        out = std::move(buffer_[current_tail & mask_]);
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return tail_.load(std::memory_order_relaxed) == head_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return head >= tail ? (head - tail) : (capacity_ - (tail - head));
    }

private:
    T* buffer_{nullptr};
    size_t capacity_{0};
    size_t mask_{0};

    // Producer state on its own cache line
    alignas(hardware_destructive_interference_size) std::atomic<size_t> head_{0};
    size_t tail_cached_{0};

    // Consumer state on its own cache line
    alignas(hardware_destructive_interference_size) std::atomic<size_t> tail_{0};
    size_t head_cached_{0};
};

} // namespace fastitch

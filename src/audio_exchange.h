// Bounded single-producer/single-consumer exchanges for UI/audio ownership.
// The audio endpoint never waits, allocates, or retries. This is not MPMC.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace audio_exchange {
// Latest complete value, with three exclusively owned slots. The producer
// writes its private slot then exchanges it with the middle slot; the consumer
// exchanges its previous read slot only when a new publication is pending.
// Either side may run arbitrarily far ahead. Superseded values are coalesced.
// The generation is the dirty slot token, never a payload field such as length.
template<class T> class Latest {
    static_assert(std::is_trivially_copyable<T>::value, "snapshot must be trivially copyable");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "32-bit atomics must be lock-free");
    static constexpr uint32_t DIRTY = 4;
    T slots_[3]{};
    std::atomic<uint32_t> middle_{1};
    uint32_t writer_ = 2; // producer-owned
    uint32_t reader_ = 0; // consumer-owned
public:
    Latest() = default;
    Latest(const Latest&) = delete;
    Latest& operator=(const Latest&) = delete;
    void publish(const T &value) {
        slots_[writer_] = value;
        writer_ = middle_.exchange(writer_ | DIRTY, std::memory_order_acq_rel) & 3;
    }
    bool consume(T &value) {
        if (!(middle_.load(std::memory_order_acquire) & DIRTY)) return false;
        reader_ = middle_.exchange(reader_, std::memory_order_acq_rel) & 3;
        value = slots_[reader_];
        return true;
    }
};

// Ordered events with explicit drop-new overflow. N is usable capacity.
// A caller must check push()'s result: a successful enqueue is not execution.
template<class T, std::size_t N> class SpscQueue {
    static_assert(N > 0, "queue needs capacity");
    static_assert(std::is_trivially_copyable<T>::value, "event must be trivially copyable");
    T slots_[N + 1]{};
    std::atomic<uint32_t> head_{0}, tail_{0};
public:
    bool push(const T &value) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t next = (tail + 1) % (N + 1);
        if (next == head_.load(std::memory_order_acquire)) return false;
        slots_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &value) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        value = slots_[head];
        head_.store((head + 1) % (N + 1), std::memory_order_release);
        return true;
    }
};
} // namespace audio_exchange

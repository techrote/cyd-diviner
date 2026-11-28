// Bounded single-producer/single-consumer exchanges for UI/audio ownership.
// Neither endpoint waits or allocates. Publication takes at most two strong
// CAS attempts; consumption takes at most one. This is not MPMC.
#pragma once
#include "audio_storage.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#if defined(GRIM_DEVICE)
#include <sdkconfig.h>
#include <esp_idf_version.h>
#endif

// GCC 14 deliberately reports "sometimes lock-free" for ESP32-S3 with the
// IDF 5.5 PSRAM workaround: internal aligned 32-bit CAS is one S32C1I, while
// external addresses use a blocking critical section. Admit only this probed
// configuration, and enforce the address precondition before publication.
#if defined(GRIM_DEVICE) && defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    ESP_IDF_VERSION_MAJOR == 5 && ESP_IDF_VERSION_MINOR == 5 && \
    defined(CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND) && CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND
#include <esp_memory_utils.h>
#include <xtensa/config/core-isa.h>
#if XCHAL_HAVE_S32C1I == 1
#define DIVINER_INTERNAL_ATOMIC32 1
#endif
#endif

namespace audio_exchange {
namespace detail {
#if defined(DIVINER_INTERNAL_ATOMIC32)
static_assert(sizeof(std::atomic<uint32_t>) == 4 && alignof(std::atomic<uint32_t>) >= 4,
              "internal S32C1I requires an aligned 32-bit atomic");
inline void checkStorage(const std::atomic<uint32_t> *word) {
    // A hard invariant even with NDEBUG; runs only during construction.
    // Standalone exchanges in downstream callers must obey this too.
    if (!esp_ptr_internal(word) || (reinterpret_cast<uintptr_t>(word) & 3u)) std::abort();
}
#else
static_assert(std::atomic<uint32_t>::is_always_lock_free, "32-bit atomics must be lock-free");
inline void checkStorage(const std::atomic<uint32_t> *) {}
#endif
} // namespace detail

// Latest complete value, with three exclusively owned slots. The producer
// writes its private slot then publishes it through the middle slot; the
// consumer returns its previous read slot only when a publication is pending.
// Either side may run arbitrarily far ahead. Superseded values are coalesced.
// The generation is the dirty slot token, never a payload field such as length.
template<class T> class Latest {
    static_assert(std::is_trivially_copyable<T>::value, "snapshot must be trivially copyable");
    static constexpr uint32_t DIRTY = 4;
    T slots_[3]{};
    alignas(4) std::atomic<uint32_t> middle_{1};
    uint32_t writer_ = 2; // producer-owned
    uint32_t reader_ = 0; // consumer-owned
public:
    Latest() { detail::checkStorage(&middle_); }
    Latest(const Latest&) = delete;
    Latest& operator=(const Latest&) = delete;
    void publish(const T &value) {
        slots_[writer_] = value;
        uint32_t previous = middle_.load(std::memory_order_acquire);
        const uint32_t next = writer_ | DIRTY;
        if (!middle_.compare_exchange_strong(previous, next, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            // Only the consumer can cause this failure. It cleared DIRTY,
            // so it cannot modify middle_ again before our next publication.
            // Strong (not weak) CAS therefore succeeds on this second attempt.
            if (!middle_.compare_exchange_strong(previous, next, std::memory_order_acq_rel,
                                                std::memory_order_acquire)) std::abort();
        }
        writer_ = previous & 3;
    }
    bool consume(T &value) {
        uint32_t previous = middle_.load(std::memory_order_acquire);
        if (!(previous & DIRTY)) return false;
        // A concurrently newer publication may defer adoption until the next
        // block. Never retry against an arbitrarily fast producer.
        if (!middle_.compare_exchange_strong(previous, reader_, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) return false;
        reader_ = previous & 3;
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
    alignas(4) std::atomic<uint32_t> head_{0}, tail_{0};
public:
    SpscQueue() { detail::checkStorage(&head_); detail::checkStorage(&tail_); }
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

#undef DIVINER_INTERNAL_ATOMIC32

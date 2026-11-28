#ifdef GRIM_CYD_PCM

#include "cyd_pcm_serial.h"
#include "cyd_pcm_protocol.h"
#include "perf_profile.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>

namespace cyd_pcm_serial {
namespace {
constexpr int QueueDepth = 8;

struct Block {
    uint32_t session;
    uint32_t sequence;
    int16_t samples[CYD_PCM_SAMPLES];
};

QueueHandle_t pending = nullptr;
uint8_t activityPin;
std::atomic<uint32_t> activeSession{0}, lastPing{0};
uint32_t nextSession = 0;

bool timedOut(uint32_t now) {
    const auto elapsed = static_cast<int32_t>(now - lastPing.load(std::memory_order_relaxed));
    return elapsed >= CYD_PCM_TIMEOUT_MS;
}

void stop() {
    activeSession.store(0, std::memory_order_release);
    xQueueReset(pending);
    digitalWrite(activityPin, HIGH);
}

void writeBlocks(void *) {
    uint8_t frame[CYD_PCM_FRAME_BYTES];
    memcpy(frame, CYD_PCM_MAGIC, sizeof(CYD_PCM_MAGIC));
    Block block;
    for (;;) {
        if (xQueueReceive(pending, &block, portMAX_DELAY) != pdTRUE) continue;
        if (block.session != session() || timedOut(millis())) continue;

        cyd_pcm_write_u32(frame + CYD_PCM_SEQUENCE_OFFSET, block.sequence);
        for (int i = 0; i < CYD_PCM_SAMPLES; ++i) {
            cyd_pcm_write_u16(frame + CYD_PCM_DATA_OFFSET + i * 2,
                              static_cast<uint16_t>(block.samples[i]));
        }
        const uint16_t crc = cyd_pcm_crc16(frame + CYD_PCM_SEQUENCE_OFFSET,
                                          CYD_PCM_CRC_OFFSET - CYD_PCM_SEQUENCE_OFFSET);
        cyd_pcm_write_u16(frame + CYD_PCM_CRC_OFFSET, crc);
#ifdef GRIM_PERF
        const uint32_t at = micros();
#endif
        Serial.write(frame, sizeof(frame));
#ifdef GRIM_PERF
        perf_profile::observe(perf_profile::PcmWrite, micros() - at);
#endif
    }
}
} // namespace

bool begin(uint8_t ledPin) {
    activityPin = ledPin;
    pending = xQueueCreate(QueueDepth, sizeof(Block));
    if (!pending) return false;

    // Only this task waits on the UART. The audio producer and UI loop can
    // continue even if sending a frame takes longer than a sample block.
    if (xTaskCreatePinnedToCore(writeBlocks, "grim-pcm", 2304, nullptr, 2, nullptr, 1) == pdPASS) return true;
    vQueueDelete(pending);
    pending = nullptr;
    return false;
}

void expire(uint32_t now) {
    if (session() && timedOut(now)) stop();
}

bool handleCommand(const char *line, uint32_t now) {
    if (strcmp(line, CYD_PCM_START) == 0) {
        if (pending) {
            lastPing.store(now, std::memory_order_relaxed);
            if (!session()) {
                if (++nextSession == 0) ++nextSession;
                activeSession.store(nextSession, std::memory_order_release);
                digitalWrite(activityPin, LOW);
            }
        }
        return true;
    }
    if (strcmp(line, CYD_PCM_PING) == 0) {
        if (session()) lastPing.store(now, std::memory_order_relaxed);
        return true;
    }
    if (strcmp(line, CYD_PCM_STOP) == 0) {
        if (session()) stop();
        return true;
    }
    return false;
}

uint32_t session() {
    return activeSession.load(std::memory_order_acquire);
}

void submit(uint32_t token, const int16_t *samples) {
    static uint32_t previousSession = 0, nextSequence = 0;
    if (!token || token != session()) return;
    if (token != previousSession) {
        previousSession = token;
        nextSequence = 0;
    }
    Block block;
    block.session = token;
    // A full queue drops the whole block, and the receiver can detect the gap.
    block.sequence = nextSequence++;
    memcpy(block.samples, samples, sizeof(block.samples));
    const bool queued = xQueueSend(pending, &block, 0) == pdTRUE;
#ifdef GRIM_PERF
    if (!queued) perf_profile::observe(perf_profile::PcmDrop, 1);
    perf_profile::observe(perf_profile::PcmQueue, uxQueueMessagesWaiting(pending));
#else
    (void)queued;
#endif
}

} // namespace cyd_pcm_serial

#endif

#ifdef GRIM_PERF
#include "perf_profile.h"
#include "cyd_pcm_serial.h"
#include "cyd_controls.h"
#include "perf_kernels.h"
#include "perf_radio.h"
#ifdef GRIM_PERF_LINK
#include "perf_link.h"
#endif
#ifdef GRIM_PERF_COLOR
#include "perf_color.h"
#endif
#ifdef GRIM_PERF_AUDIO_MVP
#include "perf_audio_mvp.h"
#endif
#include "theme.h"
#include <Arduino.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>

#ifndef GRIM_PERF_SOURCE
#define GRIM_PERF_SOURCE "unknown"
#endif
#ifndef GRIM_PERF_PROFILE
#define GRIM_PERF_PROFILE "cyd-perf"
#endif
#ifndef GRIM_PERF_SOURCE_DIGEST
#define GRIM_PERF_SOURCE_DIGEST "unknown"
#endif
#ifndef GRIM_PERF_DIRTY
#define GRIM_PERF_DIRTY 0
#endif

extern bool grimPerfCommand(const char *);
extern "C" uint8_t temprature_sens_read();

namespace perf_profile {
namespace {
constexpr unsigned Bins = 32, MaxSamples = 480;
struct Histogram { uint32_t count, min, max, bins[Bins]; uint64_t sum; };
Histogram hist[Count]{};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> enabled{true}, collecting{false};
std::atomic<uint32_t> audioBlocks{0};
bool scheduled = false, completed = false, steadyStarted = false;
uint64_t warmUntil = 0, endAt = 0, startedAt = 0, finishedAt = 0;
uint32_t lastFrame = 0, frames = 0, directFrames = 0, warmupMs = 0, durationMs = 0;
char runId[32] = "none";
#ifdef GRIM_PERF_I2S_EVENTS
constexpr bool I2sEventsEnabled = true;
constexpr uint32_t I2sEventsCapacity = perf_i2s_events::QueueCapacity;
constexpr const char *TemperatureMethod = "RTC_library_raw_undocumented_uncalibrated";
bool i2sQueueReady = false;
perf_i2s_events::Counters i2sCounters{};
#else
constexpr bool I2sEventsEnabled = false;
constexpr uint32_t I2sEventsCapacity = 0;
constexpr const char *TemperatureMethod = "ESP32_ROM_raw_uncalibrated";
#endif
volatile uint32_t totalTicks[2]{}, busyTicks[2]{};
portMUX_TYPE cpuMux[2] = {portMUX_INITIALIZER_UNLOCKED, portMUX_INITIALIZER_UNLOCKED};
TaskHandle_t idleTasks[2]{}, audioHandle = nullptr, loopHandle = nullptr, stressTasks[2]{};
bool cpuHooks[2]{};
std::atomic<uint32_t> stressEnd{0};
std::atomic<uint32_t> stressIterations[2]{};
struct Sample {
    uint32_t elapsedMs, heap, minHeap, largest, busy[2], ticks[2], audioStack, loopStack;
    uint8_t temperatureRaw;
};
Sample samples[MaxSamples]{};
unsigned sampleCount = 0, samplesDropped = 0;
uint32_t nextSampleMs = 0, previousBusy[2]{}, previousTicks[2]{};
const char *const names[Count] = {"frame_interval_us", "frame_update_us", "frame_raster_us", "frame_present_us",
    "touch_poll_us", "touch_dispatch_us", "audio_period_us", "audio_lock_us", "audio_callback_us", "audio_overlay_us",
    "audio_mix_us", "audio_enqueue_us", "pcm_write_us", "audio_write_errors", "audio_short_writes",
    "audio_late_periods_inferred", "pcm_dropped_blocks", "pcm_queue_blocks", "frame_missed_periods_inferred"};

void IRAM_ATTR tick0() {
    portENTER_CRITICAL_ISR(&cpuMux[0]);
    ++totalTicks[0]; if (xTaskGetCurrentTaskHandleForCPU(0) != idleTasks[0]) ++busyTicks[0];
    portEXIT_CRITICAL_ISR(&cpuMux[0]);
}
void IRAM_ATTR tick1() {
    portENTER_CRITICAL_ISR(&cpuMux[1]);
    ++totalTicks[1]; if (xTaskGetCurrentTaskHandleForCPU(1) != idleTasks[1]) ++busyTicks[1];
    portEXIT_CRITICAL_ISR(&cpuMux[1]);
}
void cpuSnapshot(unsigned core, uint32_t &busy, uint32_t &ticks) {
    portENTER_CRITICAL(&cpuMux[core]); busy = busyTicks[core]; ticks = totalTicks[core]; portEXIT_CRITICAL(&cpuMux[core]);
}

void stressWorker(void *arg) {
    const unsigned core = reinterpret_cast<uintptr_t>(arg);
    volatile uint32_t value = 0x12345678u + core;
    for (;;) {
        const uint32_t until = stressEnd.load(std::memory_order_relaxed);
        if (!until || static_cast<int32_t>(until - millis()) <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        // 99 ms work + at least one tick for IDLE/WDT; no watchdog disabling.
        const uint32_t start = micros();
        do {
            for (unsigned i = 0; i < 256; ++i) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; }
            stressIterations[core].fetch_add(256, std::memory_order_relaxed);
        } while (micros() - start < 99000 && static_cast<int32_t>(until - millis()) > 0);
        vTaskDelay(1);
    }
}

void sample() {
    Sample s{};
    s.elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - startedAt) / 1000);
    s.heap = ESP.getFreeHeap(); s.minHeap = ESP.getMinFreeHeap(); s.largest = ESP.getMaxAllocHeap();
    s.temperatureRaw = temprature_sens_read();
    s.audioStack = audioHandle ? uxTaskGetStackHighWaterMark(audioHandle) : 0;
    s.loopStack = loopHandle ? uxTaskGetStackHighWaterMark(loopHandle) : 0;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t busy, ticks;
        cpuSnapshot(i, busy, ticks);
        s.busy[i] = busy - previousBusy[i]; s.ticks[i] = ticks - previousTicks[i];
        previousBusy[i] = busy; previousTicks[i] = ticks;
    }
    if (sampleCount < MaxSamples) samples[sampleCount++] = s; else ++samplesDropped;
}

uint32_t percentile(const Histogram &h, unsigned pct) {
    if (!h.count) return 0;
    uint64_t target = (uint64_t(h.count) * pct + 99) / 100, n = 0;
    for (unsigned i = 0; i < Bins; ++i) { n += h.bins[i]; if (n >= target) return i == 31 ? UINT32_MAX : (1u << i); }
    return h.max;
}

void ack(const char *action) {
    Serial.printf("PERF {\"schema\":1,\"type\":\"ack\",\"action\":\"%s\",\"ok\":true}\n", action);
}
void displaySettings() {
    const auto &p = theme::palette();
    const uint16_t roles[] = {p.background, p.ash, p.dusk, p.clot, p.blood, p.rust,
        p.ember, p.glow, p.bone, p.dim, p.umber, p.stone};
    Serial.printf("PERF {\"schema\":1,\"type\":\"display_settings\",\"theme_id\":%u,\"background_style\":%u,\"gradient_mode\":%u,\"gradient_top\":%u,\"gradient_bottom\":%u,\"orientation\":%u,\"menu_backdrop\":%u,\"canvas_depth\":%u,\"i2s_dma_buffers\":4,\"i2s_dma_frames_per_buffer\":128,\"i2s_slots\":2,\"dac_gpio\":26,\"palette_roles_rgb565\":[",
        unsigned(theme::colourScheme()), unsigned(p.backgroundStyle), unsigned(p.gradientMode),
        p.gradientTop, p.gradientBottom, unsigned(cyd_controls::orientation()),
        unsigned(cyd_controls::menuBackdrop()), unsigned(hal::canvas().getColorDepth()));
    for (unsigned i = 0; i < 12; ++i) Serial.printf(i ? ",%u" : "%u", roles[i]);
    Serial.print("],\"intensity_rgb565\":[");
    for (unsigned i = 0; i < p.intensity.size(); ++i) Serial.printf(i ? ",%u" : "%u", p.intensity[i]);
    Serial.println("]}");
}
bool i2sEventsAvailable() {
#ifdef GRIM_PERF_I2S_EVENTS
    bool available;
    portENTER_CRITICAL(&mux); available = i2sQueueReady; portEXIT_CRITICAL(&mux);
    return available;
#else
    return false;
#endif
}
void exportI2sEvents() {
#ifdef GRIM_PERF_I2S_EVENTS
    perf_i2s_events::Counters c;
    bool available;
    portENTER_CRITICAL(&mux); c = i2sCounters; available = i2sQueueReady; portEXIT_CRITICAL(&mux);
    Serial.printf("PERF {\"schema\":1,\"type\":\"i2s_events\",\"run_id\":\"%s\",\"enabled\":true,\"queue_available\":%s,\"queue_capacity\":%u,\"observed\":%s,\"drain_calls\":%u,\"events_observed\":%u,\"tx_underflow_indications\":%u,\"dma_descriptor_faults\":%u,\"tx_done_events\":%u,\"other_events\":%u,\"pending_max\":%u,\"queue_full_observations\":%u,\"drain_limit_hits\":%u,\"max_drain_us\":%u,\"max_drain_interval_us\":%u,\"first_drain_offset_us\":%u,\"last_drain_offset_us\":%u,\"event_loss_count\":null,\"analog_gap_count\":null,\"window_assignment\":\"drain_completion_time\",\"drain_timing_scope\":\"queue_reads_only_excludes_counter_fold\",\"underflow_semantics\":\"IDF4.4.7_TX_Q_OVF_free_TX_buffer_queue_full\",\"saturation_limit\":\"queue_full_between_drains_and_exact_event_loss_unknown\"}\n",
        runId, available ? "true" : "false", I2sEventsCapacity, available && c.drains ? "true" : "false",
        c.drains, c.events, c.txUnderflow, c.descriptorFault, c.txDone, c.other, c.pendingMax,
        c.queueFullObservations, c.drainLimitHits, c.maxDrainUs, c.maxDrainIntervalUs,
        c.firstDrainOffsetUs, c.lastDrainOffsetUs);
#endif
}
void exportResults() {
    poll();
    if (scheduled) { Serial.printf("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"run active; export after window\"}\n"); return; }
    for (unsigned i = 0; i < sampleCount; ++i) {
        const auto &s = samples[i];
        char cpuPct[2][24];
        for (unsigned core = 0; core < 2; ++core) {
            if (cpuHooks[core] && s.ticks[core]) snprintf(cpuPct[core], sizeof(cpuPct[core]), "%.3f", 100.0 * s.busy[core] / s.ticks[core]);
            else strcpy(cpuPct[core], "null");
        }
        Serial.printf("PERF {\"schema\":1,\"type\":\"sample\",\"run_id\":\"%s\",\"elapsed_ms\":%u,\"heap_free_bytes\":%u,\"heap_min_bytes\":%u,\"heap_largest_bytes\":%u,\"audio_stack_min_bytes\":%u,\"loop_stack_min_bytes\":%u,\"temperature_raw\":%u,\"temperature_c_uncalibrated\":%.3f,\"cpu0_busy_ticks\":%u,\"cpu0_total_ticks\":%u,\"cpu1_busy_ticks\":%u,\"cpu1_total_ticks\":%u,\"core0_busy_pct\":%s,\"core1_busy_pct\":%s}\n",
            runId, s.elapsedMs, s.heap, s.minHeap, s.largest, s.audioStack, s.loopStack,
            s.temperatureRaw, (s.temperatureRaw - 32) / 1.8, s.busy[0], s.ticks[0], s.busy[1], s.ticks[1], cpuPct[0], cpuPct[1]);
        delay(1);
    }
    for (unsigned i = 0; i < Count; ++i) {
        Histogram h;
        portENTER_CRITICAL(&mux); h = hist[i]; portEXIT_CRITICAL(&mux);
        Serial.printf("PERF {\"schema\":1,\"type\":\"metric\",\"run_id\":\"%s\",\"name\":\"%s\",\"unit\":\"%s\",\"count\":%u,\"sum\":%llu,\"min\":%u,\"max\":%u,\"p50_upper\":%u,\"p95_upper\":%u,\"p99_upper\":%u,\"histogram\":[",
            runId, names[i], i >= AudioWriteError ? "count_or_blocks" : "us", h.count, static_cast<unsigned long long>(h.sum),
            h.count ? h.min : 0, h.max, percentile(h, 50), percentile(h, 95), percentile(h, 99));
        for (unsigned j = 0; j < Bins; ++j) Serial.printf(j ? ",%u" : "%u", h.bins[j]);
        Serial.println("]}");
        delay(1);
    }
    exportI2sEvents();
#ifdef GRIM_PERF_COLOR
    perf_color::exportRun();
#endif
    Serial.printf("PERF {\"schema\":1,\"type\":\"end\",\"run_id\":\"%s\",\"complete\":%s,\"warmup_ms\":%u,\"duration_ms\":%u,\"elapsed_us\":%llu,\"frames\":%u,\"audio_completed_blocks\":%u,\"interleaved_frames\":%u,\"samples\":%u,\"samples_dropped\":%u,\"instrumentation\":%s,\"histograms_enabled\":%s,\"instrumentation_ab_scope\":\"histogram_insertion_only_timestamps_hooks_counts_retained\",\"dma_underruns\":null,\"physical_touch_latency_us\":null,\"cpu_method\":\"tick_sample_current_task_vs_idle\",\"temperature_method\":\"%s\",\"stress_iterations_core0\":%u,\"stress_iterations_core1\":%u}\n",
        runId, completed ? "true" : "false", warmupMs, durationMs,
        static_cast<unsigned long long>(finishedAt > warmUntil ? finishedAt - warmUntil : 0),
        frames, audioBlocks.load(), directFrames, sampleCount, samplesDropped, enabled.load() ? "true" : "false", enabled.load() ? "true" : "false", TemperatureMethod, stressIterations[0].load(), stressIterations[1].load());
}
}

uint32_t now() { return micros(); }
bool quiet() {
#ifdef GRIM_CYD_PCM
    return cyd_pcm_serial::session() == 0;
#else
    return true;
#endif
}
bool running() { return scheduled; }
void sceneSettings(const char *scene, const char *fields) {
    if (quiet() && !running()) Serial.printf("PERF {\"schema\":1,\"type\":\"scene_settings\",\"scene\":\"%s\",\"seed\":%u,\"reset\":\"fresh_DSP_state_each_entry\",\"song_persistence\":false,%s}\n", scene, seed(), fields);
}
void audioTask(void *handle) { audioHandle = static_cast<TaskHandle_t>(handle); }
void audioBlock() {
    if (!collecting.load(std::memory_order_acquire)) return;
    const uint64_t t = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if (collecting.load(std::memory_order_acquire) && t >= warmUntil && t < endAt) audioBlocks.fetch_add(1, std::memory_order_relaxed);
    portEXIT_CRITICAL(&mux);
}
#ifdef GRIM_PERF_I2S_EVENTS
void i2sEventQueueAvailable(bool available) {
    portENTER_CRITICAL(&mux); i2sQueueReady = available; portEXIT_CRITICAL(&mux);
}
void i2sEventDrain(const perf_i2s_events::Drain &drain, uint64_t drainedAtUs) {
    if (!collecting.load(std::memory_order_acquire)) return;
    portENTER_CRITICAL(&mux);
    // Recheck after acquiring the run/reset lock: a drain from an old run
    // must never land in freshly reset counters on the UI core.
    if (collecting.load(std::memory_order_acquire)) i2sCounters.add(drain, drainedAtUs, true, warmUntil, endAt);
    portEXIT_CRITICAL(&mux);
}
#endif
void begin() {
    loopHandle = xTaskGetCurrentTaskHandle();
    idleTasks[0] = xTaskGetIdleTaskHandleForCPU(0); idleTasks[1] = xTaskGetIdleTaskHandleForCPU(1);
    cpuHooks[0] = idleTasks[0] && esp_register_freertos_tick_hook_for_cpu(tick0, 0) == ESP_OK;
    cpuHooks[1] = idleTasks[1] && esp_register_freertos_tick_hook_for_cpu(tick1, 1) == ESP_OK;
}
void observe(Metric metric, uint32_t value) {
    if (!collecting.load(std::memory_order_acquire) || !enabled.load(std::memory_order_relaxed)) return;
    const uint64_t t = esp_timer_get_time();
    unsigned bin = 0;
    if (value > 1) bin = std::min<unsigned>(Bins - 1, 32 - __builtin_clz(value - 1));
    portENTER_CRITICAL(&mux);
    // A run reset on the UI core may have happened while this audio-core
    // writer was waiting for the lock. Recheck the window under that lock.
    if (!collecting.load(std::memory_order_acquire) || t < warmUntil || t >= endAt) { portEXIT_CRITICAL(&mux); return; }
    auto &h = hist[metric];
    if (!h.count || value < h.min) h.min = value;
    h.max = std::max(h.max, value); ++h.count; h.sum += value; ++h.bins[bin];
    portEXIT_CRITICAL(&mux);
}
void frame(uint32_t duration, bool interleaved) {
    observe(FramePresent, duration);
    const uint32_t at = micros();
    if (!collecting.load() || uint64_t(esp_timer_get_time()) < warmUntil || uint64_t(esp_timer_get_time()) >= endAt) { lastFrame = 0; return; }
    if (lastFrame) {
        const uint32_t period = at - lastFrame;
        observe(FrameInterval, period);
        if (period >= 66000) observe(FrameMiss, period / 33000 - 1);
    }
    lastFrame = at; ++frames; if (interleaved) ++directFrames;
}
void poll() {
    if (!scheduled) return;
    const uint64_t t = esp_timer_get_time();
    if (t >= endAt) { collecting.store(false); scheduled = false; completed = true; finishedAt = endAt; return; }
    if (t >= warmUntil && !steadyStarted) {
        steadyStarted = true;
        for (unsigned i = 0; i < 2; ++i) cpuSnapshot(i, previousBusy[i], previousTicks[i]);
        nextSampleMs = millis() + 1000;
    }
    if (steadyStarted && collecting.load() && static_cast<int32_t>(millis() - nextSampleMs) >= 0) { sample(); nextSampleMs = millis() + 1000; }
}
bool command(const char *line) {
    if (strncmp(line, "perf ", 5)) return false;
    if (!quiet()) return true;
#ifdef GRIM_PERF_AUDIO_MVP
    if (perf_audio_mvp::command(line)) return true;
#endif
#ifdef GRIM_PERF_LINK
    if (perf_link::command(line)) return true;
#endif
    if (!strcmp(line, "perf info")) {
        displaySettings();
        Serial.printf("PERF {\"schema\":1,\"type\":\"info\",\"source_sha\":\"%s\",\"profile\":\"%s\",\"i2s_event_queue_enabled\":%s,\"i2s_event_queue_capacity\":%u,\"i2s_event_queue_available\":%s,\"i2s_event_window_assignment\":\"drain_completion_time\",\"source_digest\":\"%s\",\"source_dirty\":%s,\"seed\":%u,\"scene_reset_policy\":\"fresh_audio_DSP_state_each_entry_seeded_no_song_persistence\",\"audio_window_alignment\":\"real_setup_and_warmup_elapsed_not_sample_aligned\",\"instrumentation_ab_scope\":\"histogram_insertion_only_timestamps_hooks_counts_retained\",\"radio\":\"%s\",\"cpu0_hook\":%s,\"cpu1_hook\":%s,\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"flash_hz\":%u,\"psram_bytes\":%u,\"heap_bytes\":%u,\"sdk\":\"%s\",\"lcd_spi_hz\":40000000,\"lcd_read_hz\":8000000,\"audio_rate\":24000,\"audio_block\":128,\"audio_core\":0,\"audio_priority\":5,\"pcm_core\":1,\"pcm_priority\":2,\"serial_baud\":533333,\"histogram_method\":\"ceil_log2_upper_bounds\",\"sample_capacity\":%u,\"sample_period_ms\":1000,\"cpu_method\":\"tick_sample_current_task_vs_idle\",\"temperature_method\":\"%s\",\"temperature_raw\":%u}\n",
            GRIM_PERF_SOURCE, GRIM_PERF_PROFILE, I2sEventsEnabled ? "true" : "false", I2sEventsCapacity, i2sEventsAvailable() ? "true" : "false", GRIM_PERF_SOURCE_DIGEST, GRIM_PERF_DIRTY ? "true" : "false", seed(), perf_radio::mode(), cpuHooks[0] ? "true" : "false", cpuHooks[1] ? "true" : "false", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize(), ESP.getFlashChipSpeed(), ESP.getPsramSize(), ESP.getFreeHeap(), ESP.getSdkVersion(), MaxSamples, TemperatureMethod, temprature_sens_read());
        return true;
    }
    unsigned warm, duration; char id[32];
    if (sscanf(line, "perf run %31s %u %u", id, &warm, &duration) == 3 || sscanf(line, "perf start %31s %u %u", id, &warm, &duration) == 3) {
        for (char c : id) { if (!c) break; if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') { Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"invalid run id\"}"); return true; } }
        if (scheduled || !duration || duration > 600000 || warm > 60000) { Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"invalid run duration or run active\"}"); return true; }
        collecting.store(false); portENTER_CRITICAL(&mux); memset(hist, 0, sizeof(hist));
#ifdef GRIM_PERF_I2S_EVENTS
        i2sCounters = {};
#endif
        portEXIT_CRITICAL(&mux);
        audioBlocks.store(0); strcpy(runId, id); warmupMs = warm; durationMs = duration; frames = directFrames = lastFrame = 0; sampleCount = samplesDropped = 0;
        startedAt = esp_timer_get_time(); warmUntil = startedAt + uint64_t(warm) * 1000; endAt = warmUntil + uint64_t(duration) * 1000;
        finishedAt = 0; scheduled = true; completed = false; steadyStarted = false; collecting.store(true);
#ifdef GRIM_PERF_LINK
        perf_link::onRun(runId, warmUntil, endAt);
#endif
#ifdef GRIM_PERF_COLOR
        perf_color::onRun(runId, warmUntil, endAt);
#endif
        Serial.printf("PERF {\"schema\":1,\"type\":\"run\",\"run_id\":\"%s\",\"warmup_ms\":%u,\"duration_ms\":%u,\"target_start_us\":%llu}\n", runId, warm, duration, static_cast<unsigned long long>(startedAt)); return true;
    }
    if (!strcmp(line, "perf export")) { exportResults(); return true; }
    if (!strcmp(line, "perf sample")) { poll(); Serial.printf("PERF {\"schema\":1,\"type\":\"state\",\"run_id\":\"%s\",\"active\":%s,\"samples\":%u,\"temperature_raw\":%u}\n",runId,scheduled?"true":"false",sampleCount,temprature_sens_read()); return true; }
    if (!strcmp(line, "perf stop")) { collecting.store(false); scheduled = false; completed = false; finishedAt = esp_timer_get_time(); stressEnd.store(0); ack("stop"); return true; }
    unsigned on;
    if (sscanf(line, "perf enable %u", &on) == 1 && on <= 1) { enabled.store(on); ack("enable"); return true; }
    if (sscanf(line, "perf stress %u", &duration) == 1 && duration <= 600000) {
        if (duration && duration < 360000) { Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"stress minimum 360000 ms\"}"); return true; }
        if (duration) for (unsigned i = 0; i < 2; ++i) if (!stressTasks[i]) {
            if (xTaskCreatePinnedToCore(stressWorker, i ? "perf-stress1" : "perf-stress0", 2048, reinterpret_cast<void *>(i), 1, &stressTasks[i], i) != pdPASS) {
                stressEnd.store(0);
                Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"stress task allocation failed\"}");
                return true;
            }
        }
        stressIterations[0].store(0); stressIterations[1].store(0); stressEnd.store(duration ? millis() + duration : 0); ack("stress"); return true;
    }
    char radioMode[16];
    if (sscanf(line, "perf radio %15s", radioMode) == 1) {
        const bool okay = perf_radio::set(radioMode);
        perf_radio::report();
        if (okay) ack("radio");
        else Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"radio initialization failed; see radio_state\"}");
        return true;
    }
    if (running() && !strncmp(line, "perf kernel", 11)) {
        Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"kernel timing requires an inactive scene window\"}");
        return true;
    }
    if (perf_kernels::command(line)) return true;
    if (grimPerfCommand(line)) { ack(line + 5); return true; }
    Serial.println("PERF {\"schema\":1,\"type\":\"error\",\"message\":\"unknown perf command\"}"); return true;
}
}
#endif

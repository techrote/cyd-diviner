#ifdef GRIM_PERF_COLOR
#include "perf_color.h"
#include "perf_color_packing.h"
#include "perf_profile.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>

namespace perf_color {
namespace {
using perf_color_packing::Format;
Format format = Format::Rgb444;
bool selected = false, twoLayers = false, frameReady = false;
unsigned widthPx = 0, heightPx = 0;
size_t backing = 0, packedStride = 0, coverageStride = 0;
size_t packedBand = 0, overlayBand = 0, coverageBand = 0, dmaBytes = 0, required = 0;
uint8_t *basePixels = nullptr, *overlayPixels = nullptr, *coverage = nullptr;
uint32_t framesApplied = 0, framesRejected = 0;
unsigned frameBaseBands = 0, frameOverlayBands = 0;
char measuredId[32] = "none";
uint64_t windowBegin = 0, windowEnd = 0;
uint32_t runApplied = 0, runNative = 0, runRejected = 0, runBands = 0;
uint32_t firstOffset = 0, lastOffset = 0;
const char *baseEffect = "none", *overlayEffect = "none";
const char *name() {
    return !selected ? "off" : format == Format::Rgb111 ? "rgb111" :
           format == Format::Rgb444 ? "rgb444" : "rgba5551";
}
bool layout(unsigned w, unsigned h, size_t bytes, bool layered) {
    widthPx = w; heightPx = h; backing = bytes; twoLayers = layered;
    dmaBytes = 2 * size_t(w) * BandRows * sizeof(lgfx::swap565_t);
    packedStride = selected ? perf_color_packing::rowBytes(format, w) : 0;
    packedBand = selected ? packedStride * BandRows : 0;
    overlayBand = selected && layered ? packedBand : 0;
    coverageStride = perf_color_packing::coverageRowBytes(w);
    coverageBand = selected && layered && format != Format::Rgba5551 ? coverageStride * BandRows : 0;
    required = dmaBytes + packedBand + overlayBand + coverageBand;
    return w && w <= 320 && h && h <= 320 && required <= bytes;
}
void receipt(bool measured = false) {
    char fields[1800];
    snprintf(fields, sizeof(fields),
        "\"enabled\":%s,\"format\":\"%s\",\"source_raster_format\":\"RGB565\",\"source_raster_bits_per_pixel\":16,"
        "\"output_spi_format\":\"RGB565\",\"output_spi_bits_per_pixel\":16,\"output_width_px\":%u,\"output_height_px\":%u,"
        "\"packed_colour_bits_per_pixel\":%u,\"packed_alpha_bits_per_pixel\":%u,\"external_coverage_bits_per_pixel\":%u,\"packed_storage_bits_per_pixel\":%u,"
        "\"base_effect\":\"%s\",\"overlay_effect\":\"%s\",\"effect_layers\":%u,\"surface_band_rows\":%u,\"packed_stride_bytes\":%u,"
        "\"packed_base_payload_bytes\":%u,\"packed_overlay_payload_bytes\":%u,\"coverage_payload_bytes\":%u,\"packed_total_payload_bytes\":%u,"
        "\"rgb565_dma_strip_count\":2,\"rgb565_dma_reserved_bytes\":%u,\"scratch_required_bytes\":%u,\"canvas_backing_reserved_bytes\":%u,"
        "\"backing_reservation_changed\":false,\"layout_fits\":%s,\"temporal_pixel_history\":false,"
        "\"storage_scope\":\"reused_8_row_packed_intermediates_in_existing_canvas\","
        "\"coverage_scope\":\"binary_changed_pixels_after_existing_RGB565_overlay_blending; not_native_A1_raster\","
        "\"fullscreen_trails_method\":\"existing_bounded_geometric_paths\",\"receipt_scope\":\"%s\","
        "\"link_info_formatter_capacity_bytes\":4096,\"link_summary_formatter_capacity_bytes\":3072",
        selected ? "true" : "false", name(), widthPx, heightPx,
        selected ? perf_color_packing::bitsPerPixel(format) - (format == Format::Rgba5551 ? 1u : 0u) : 0u,
        selected && format == Format::Rgba5551 ? 1u : 0u, coverageBand ? 1u : 0u,
        selected ? perf_color_packing::bitsPerPixel(format) : 0u,
        baseEffect, twoLayers ? overlayEffect : "none", twoLayers ? 2u : 1u, BandRows, unsigned(packedStride),
        unsigned(packedBand), unsigned(overlayBand), unsigned(coverageBand), unsigned(packedBand + overlayBand + coverageBand),
        unsigned(dmaBytes), unsigned(required), unsigned(backing), required <= backing ? "true" : "false",
        measured ? "postwindow_actual_renderer_layout" : "configuration_layout_before_next_frame");
    if (!measured) perf_profile::sceneSettings("color", fields);
    else Serial.printf("PERF {\"schema\":1,\"type\":\"color_run\",\"run_id\":\"%s\",%s,\"successful_applied_frames\":%u,\"native_frames\":%u,\"rejected_frames\":%u,\"bands_applied\":%u,\"first_applied_offset_us\":%u,\"last_applied_offset_us\":%u,\"window_assignment\":\"renderer_DMA_completion_time\",\"steady_begin_us\":%llu,\"end_us\":%llu}\n",
                       measuredId, fields, runApplied, runNative, runRejected, runBands, firstOffset, lastOffset,
                       static_cast<unsigned long long>(windowBegin), static_cast<unsigned long long>(windowEnd));
}
}
bool enabled() { return selected; }
bool configure(const char *requested, unsigned w, unsigned h, size_t bytes,
               bool layered, const char *base, const char *overlay) {
    if (perf_profile::running()) return false;
    const bool off = !strcmp(requested, "off");
    Format next;
    if (!strcmp(requested, "rgb111")) next = Format::Rgb111;
    else if (!strcmp(requested, "rgb444")) next = Format::Rgb444;
    else if (!strcmp(requested, "rgba5551")) next = Format::Rgba5551;
    else if (!off) return false;
    else next = format;
    selected = !off; format = next; frameReady = false;
    baseEffect = base; overlayEffect = overlay;
    framesApplied = framesRejected = 0;
    if (!layout(w, h, bytes, layered)) { selected = false; receipt(); return false; }
    receipt(); return true;
}
bool beginFrame(uint8_t *canvas, size_t bytes, unsigned w, unsigned h, bool layered,
                const char *base, const char *overlay) {
    baseEffect = base; overlayEffect = overlay;
    frameBaseBands = frameOverlayBands = 0;
    frameReady = canvas && layout(w, h, bytes, layered);
    if (!frameReady) return false;
    basePixels = canvas + dmaBytes;
    overlayPixels = basePixels + packedBand;
    coverage = overlayPixels + overlayBand;
    return selected;
}
void baseRoundtrip(lgfx::swap565_t *pixels, unsigned rows) {
    if (!frameReady || rows > BandRows) return;
    ++frameBaseBands;
    for (unsigned y = 0; y < rows; ++y) {
        uint8_t *row = basePixels + y * packedStride;
        memset(row, 0, packedStride);
        for (unsigned x = 0; x < widthPx; ++x)
            perf_color_packing::write(format, row, x, perf_color_packing::encode565(format, lgfx::getSwap16(pixels[y * widthPx + x].get()), true));
        for (unsigned x = 0; x < widthPx; ++x)
            pixels[y * widthPx + x] = lgfx::rgb565_t{perf_color_packing::expand565(format, perf_color_packing::read(format, row, x))};
    }
}
void compositeRoundtrip(lgfx::swap565_t *pixels, unsigned rows) {
    if (!frameReady || !twoLayers || rows > BandRows) return;
    ++frameOverlayBands;
    for (unsigned y = 0; y < rows; ++y) {
        const uint8_t *base = basePixels + y * packedStride;
        uint8_t *over = overlayPixels + y * packedStride;
        uint8_t *mask = coverageBand ? coverage + y * coverageStride : nullptr;
        memset(over, 0, packedStride); if (mask) memset(mask, 0, coverageStride);
        for (unsigned x = 0; x < widthPx; ++x) {
            const uint16_t before = perf_color_packing::expand565(format, perf_color_packing::read(format, base, x));
            const uint16_t after = lgfx::getSwap16(pixels[y * widthPx + x].get());
            const bool changed = before != after;
            perf_color_packing::write(format, over, x, perf_color_packing::encode565(format, after, changed));
            if (mask) perf_color_packing::writeCoverage(mask, x, changed);
        }
        for (unsigned x = 0; x < widthPx; ++x) {
            const uint16_t ink = perf_color_packing::read(format, over, x);
            const bool opaque = mask ? perf_color_packing::readCoverage(mask, x) : perf_color_packing::opaque(format, ink);
            const uint16_t output = opaque ? ink : perf_color_packing::read(format, base, x);
            pixels[y * widthPx + x] = lgfx::rgb565_t{perf_color_packing::expand565(format, output)};
        }
    }
}
void endFrame() {
    const uint64_t at = esp_timer_get_time();
    const bool applied = selected && frameReady && frameBaseBands == (heightPx + BandRows - 1) / BandRows &&
                         (!twoLayers || frameOverlayBands == frameBaseBands);
    if (applied) ++framesApplied;
    else if (selected) ++framesRejected;
    if (!windowBegin || at < windowBegin || at >= windowEnd) return;
    if (!selected) ++runNative;
    else if (applied) {
        if (!runApplied) firstOffset = static_cast<uint32_t>(at - windowBegin);
        lastOffset = static_cast<uint32_t>(at - windowBegin);
        ++runApplied; runBands += frameBaseBands;
    } else ++runRejected;
}
void onRun(const char *id, uint64_t beginUs, uint64_t endUs) {
    strncpy(measuredId, id, sizeof(measuredId) - 1);
    windowBegin = beginUs; windowEnd = endUs;
    runApplied = runNative = runRejected = runBands = firstOffset = lastOffset = 0;
}
void exportRun() { receipt(true); }
void report() {
    receipt();
    Serial.printf("PERF {\"schema\":1,\"type\":\"color_state\",\"format\":\"%s\",\"frames_applied_since_configuration\":%u,\"frames_rejected_since_configuration\":%u,\"frame_layout_ready\":%s,\"counter_scope\":\"includes_warmup_and_postwindow_frames; not_profiler_window\"}\n",
                  name(), framesApplied, framesRejected, frameReady ? "true" : "false");
}
}
#endif

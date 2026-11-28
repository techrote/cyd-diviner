/* SPDX-License-Identifier: MIT */
/* Host fixture for the actual VLC frame parser and playback functions.
 * Windows serial I/O and VLC's output queue are bounded fakes; no USB device
 * or VLC executable is used by this test. Production code is included from
 * the temporary fragments produced by pcm_protocol_test.py, not copied here.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cyd_pcm_protocol.h"

typedef int HANDLE;
typedef uint32_t DWORD;
typedef void es_out_id_t;
typedef int64_t vlc_tick_t;
typedef struct demux_sys_t demux_sys_t;
typedef struct { demux_sys_t *p_sys; void *out; } demux_t;
#define CLOCK_FREQ INT64_C(1000000)
#define VLC_DEMUXER_SUCCESS 1
#define VLC_DEMUXER_EGENERIC -1
#define ES_OUT_RESET_PCR 1
#define BLOCK_FLAG_DISCONTINUITY 1
#define __MAX(a,b) ((a) > (b) ? (a) : (b))
#define msg_Err(...) ((void)0)
#define msg_Dbg(...) ((void)0)
#include "pcm-types.inc"

typedef struct {
    uint8_t *p_buffer;
    size_t bytes;
    unsigned i_nb_samples, i_flags;
    vlc_tick_t i_length, i_pts, i_dts;
} block_t;
static vlc_tick_t now_us;
static const uint8_t *incoming;
static size_t remaining, chunk_limit;
static bool fail_read, fail_write, short_write, fail_alloc;
static unsigned sends, writes, resets, errors;
static uint64_t samples;
static char last_command[32];
static uint8_t last_audio[CYD_PCM_PAYLOAD_BYTES * (MAX_SILENT_BLOCKS + 1)];
static block_t last_block;

static vlc_tick_t mdate(void) { return now_us; }
static DWORD GetLastError(void) { return 7; }
static void SerialError(demux_t *d, const char *why, DWORD e) {
    (void)d; (void)why; (void)e; ++errors;
}
static bool WriteFile(HANDLE h, const void *data, DWORD n, DWORD *written, void *overlap) {
    (void)h; (void)overlap; ++writes;
    assert(n < sizeof(last_command));
    memcpy(last_command, data, n); last_command[n] = 0;
    *written = short_write && n ? n - 1 : n;
    return !fail_write;
}
static bool ReadFile(HANDLE h, void *data, DWORD n, DWORD *read, void *overlap) {
    (void)h; (void)overlap;
    if (fail_read) return false;
    size_t amount = remaining < n ? remaining : n;
    if (amount > chunk_limit) amount = chunk_limit;
    if (amount) { memcpy(data, incoming, amount); incoming += amount; remaining -= amount; }
    *read = (DWORD)amount; return true;
}
static block_t *block_Alloc(size_t bytes) {
    if (fail_alloc) return NULL;
    assert(bytes <= sizeof(last_audio));
    block_t *b = calloc(1, sizeof(*b)); assert(b);
    b->p_buffer = malloc(bytes); assert(b->p_buffer); b->bytes = bytes;
    return b;
}
static void es_out_Control(void *out, int command) {
    (void)out; assert(command == ES_OUT_RESET_PCR); ++resets;
}
static void es_out_SetPCR(void *out, vlc_tick_t value) { (void)out; (void)value; }
static void es_out_Send(void *out, es_out_id_t *audio, block_t *b) {
    (void)out; (void)audio;
    assert(b->i_dts == b->i_pts && b->i_length > 0);
    if (sends) assert(b->i_pts >= last_block.i_pts);
    memcpy(last_audio, b->p_buffer, b->bytes);
    last_block = *b; last_block.p_buffer = NULL;
    samples += b->i_nb_samples; ++sends;
    free(b->p_buffer); free(b);
}
#include "pcm-functions.inc"

static void reset(demux_sys_t *s) {
    memset(s, 0, sizeof(*s));
    now_us = 1000000; s->last_frame = now_us; s->next_ping = now_us + 100000000;
    incoming = NULL; remaining = 0; chunk_limit = 1024;
    fail_read = fail_write = short_write = fail_alloc = false;
    sends = writes = resets = errors = 0; samples = 0;
    memset(&last_block, 0, sizeof(last_block)); memset(last_command, 0, sizeof(last_command));
}
static void frame(uint8_t *p, uint32_t sequence) {
    memcpy(p, CYD_PCM_MAGIC, sizeof(CYD_PCM_MAGIC));
    cyd_pcm_write_u32(p + CYD_PCM_SEQUENCE_OFFSET, sequence);
    for (unsigned i = 0; i < CYD_PCM_SAMPLES; ++i)
        cyd_pcm_write_u16(p + CYD_PCM_DATA_OFFSET + 2*i, (uint16_t)(i*521u + sequence));
    cyd_pcm_write_u16(p + CYD_PCM_CRC_OFFSET,
                     cyd_pcm_crc16(p + CYD_PCM_SEQUENCE_OFFSET, CYD_PCM_CRC_OFFSET - CYD_PCM_SEQUENCE_OFFSET));
}
static int play(demux_t *d, uint32_t sequence) {
    frame(d->p_sys->buffer, sequence); d->p_sys->used = CYD_PCM_FRAME_BYTES;
    assert(FindFrame(d->p_sys)); return PlayFrame(d);
}
static void protocol(void) {
    assert(CYD_PCM_RATE == 24000 && CYD_PCM_SAMPLES == 128 && CYD_PCM_FRAME_BYTES == 266);
    assert(cyd_pcm_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    uint8_t b[4]; cyd_pcm_write_u32(b, UINT32_C(0x89abcdef));
    assert(b[0] == 0xef && b[1] == 0xcd && b[2] == 0xab && b[3] == 0x89);
    assert(cyd_pcm_read_u32(b) == UINT32_C(0x89abcdef));
    cyd_pcm_write_u16(b, UINT16_C(0xfedc)); assert(b[0] == 0xdc && b[1] == 0xfe);
    assert(cyd_pcm_read_u16(b) == UINT16_C(0xfedc));
    assert(SampleTime(24000) == CLOCK_FREQ);
    assert(SampleTime(UINT64_C(24000)*3600) == INT64_C(3600000000));
    assert(SampleTime(128) == 5333 && SampleTime(256) == 10666);
    uint8_t good[CYD_PCM_FRAME_BYTES]; frame(good, 17);
    for (unsigned bit = 0; bit < CYD_PCM_FRAME_BYTES*8; ++bit) {
        demux_sys_t s = {0}; memcpy(s.buffer, good, sizeof(good)); s.used = sizeof(good);
        s.buffer[bit/8] ^= (uint8_t)(1u << (bit%8));
        assert(!FindFrame(&s)); // every single-bit wire error is rejected
    }
}
static void fragmented_serial(void) {
    const uint8_t noise[] = {'x','C','Y',0,'D','1','C','Y','D','2',0,0,0,'C','Y'};
    uint8_t stream[sizeof(noise) + CYD_PCM_FRAME_BYTES*5];
    memcpy(stream, noise, sizeof(noise));
    uint8_t *p = stream + sizeof(noise);
    frame(p,100); p += CYD_PCM_FRAME_BYTES;
    frame(p,101); p[CYD_PCM_DATA_OFFSET+7] ^= 4; p += CYD_PCM_FRAME_BYTES;
    frame(p,101); p += CYD_PCM_FRAME_BYTES;
    frame(p,101); p += CYD_PCM_FRAME_BYTES; // duplicate
    frame(p,103);
    for (size_t chunk = 1; chunk <= CYD_PCM_FRAME_BYTES*2+17; ++chunk) {
        demux_sys_t s; reset(&s); demux_t d = {&s, NULL};
        incoming = stream; remaining = sizeof(stream); chunk_limit = chunk;
        unsigned guard = 0;
        while (remaining || s.used >= sizeof(CYD_PCM_MAGIC)) {
            assert(++guard < sizeof(stream)*3);
            assert(Demux(&d) == VLC_DEMUXER_SUCCESS);
        }
        assert(s.frames == 3 && s.corrupt == 1 && s.missing == 1 && !s.discontinuities);
        assert(sends == 3 && samples == CYD_PCM_SAMPLES*4);
        assert(last_block.i_nb_samples == CYD_PCM_SAMPLES*2);
        for (unsigned i=0; i<CYD_PCM_PAYLOAD_BYTES; ++i) assert(last_audio[i] == 0);
        assert(memcmp(last_audio + CYD_PCM_PAYLOAD_BYTES, p + CYD_PCM_DATA_OFFSET,
                      CYD_PCM_PAYLOAD_BYTES) == 0);
    }
    incoming = NULL;
}
static void sequencing(void) {
    demux_sys_t s; reset(&s); demux_t d = {&s, NULL};
    assert(play(&d,UINT32_MAX-1) == VLC_DEMUXER_SUCCESS);
    assert(play(&d,UINT32_MAX) == VLC_DEMUXER_SUCCESS);
    assert(play(&d,0) == VLC_DEMUXER_SUCCESS);
    assert(play(&d,0) == VLC_DEMUXER_SUCCESS);
    assert(s.frames == 3 && !s.missing && !resets);
    assert(play(&d,3) == VLC_DEMUXER_SUCCESS);
    assert(s.missing == 2 && last_block.i_nb_samples == CYD_PCM_SAMPLES*3);
    for (unsigned i=0; i<CYD_PCM_PAYLOAD_BYTES*2; ++i) assert(last_audio[i] == 0);
    assert(play(&d,13) == VLC_DEMUXER_SUCCESS); // gap 9 exceeds the eight-block fill limit
    assert(resets == 1 && s.discontinuities == 1 && s.missing == 11);
    assert(last_block.bytes == CYD_PCM_PAYLOAD_BYTES && last_block.i_flags == BLOCK_FLAG_DISCONTINUITY);
    assert(play(&d,0) == VLC_DEMUXER_SUCCESS); // board/session restart
    assert(resets == 2 && s.discontinuities == 2 && last_block.i_flags == BLOCK_FLAG_DISCONTINUITY);
    fail_alloc = true;
    assert(play(&d,1) == VLC_DEMUXER_EGENERIC && s.used == CYD_PCM_FRAME_BYTES);
}
static void commands_and_errors(void) {
    demux_sys_t s; reset(&s); demux_t d = {&s, NULL}; s.next_ping = now_us;
    assert(Demux(&d) == VLC_DEMUXER_SUCCESS && writes == 1);
    assert(strcmp(last_command,"pcm start\n") == 0);
    now_us += 999000; assert(Demux(&d) == VLC_DEMUXER_SUCCESS && writes == 1);
    now_us += 1000; assert(Demux(&d) == VLC_DEMUXER_SUCCESS && writes == 2);
    assert(strcmp(last_command,"pcm start\n") == 0); // retry before first valid frame
    uint8_t first[CYD_PCM_FRAME_BYTES]; frame(first,0); incoming = first; remaining = sizeof(first);
    assert(Demux(&d) == VLC_DEMUXER_SUCCESS && s.frames == 1);
    now_us += 1000000; assert(Demux(&d) == VLC_DEMUXER_SUCCESS && writes == 3);
    assert(strcmp(last_command,"pcm ping\n") == 0);
    now_us = s.last_frame + CYD_PCM_TIMEOUT_MS*INT64_C(1000);
    assert(Demux(&d) == VLC_DEMUXER_EGENERIC);
    reset(&s); fail_read = true; assert(Demux(&d) == VLC_DEMUXER_EGENERIC && errors == 1);
    reset(&s); s.next_ping = now_us; fail_write = true;
    assert(Demux(&d) == VLC_DEMUXER_EGENERIC && errors == 1);
    reset(&s); s.next_ping = now_us; short_write = true;
    assert(Demux(&d) == VLC_DEMUXER_EGENERIC);
    reset(&s); assert(SendCommand(&d, CYD_PCM_STOP)); assert(strcmp(last_command,"pcm stop\n") == 0);
}
int main(void) {
    protocol(); fragmented_serial(); sequencing(); commands_and_errors();
    puts("PASS actual VLC frame/playback functions: CRC golden vector, 2128 single-bit errors, 549 serial chunk sizes, noise/corruption recovery, duplicate/wrap/restart/gap timing, bounded silence, allocation/read/write failure, start/ping/stop commands and timeout");
    puts("NOT RUN: Windows DLL loading, physical UART, FreeRTOS producer/queue scheduling, real VLC playback and acoustic continuity");
    return 0;
}

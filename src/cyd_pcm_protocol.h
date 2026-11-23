#ifndef CYD_PCM_PROTOCOL_H
#define CYD_PCM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CYD_PCM_START "pcm start\n"
#define CYD_PCM_PING "pcm ping\n"
#define CYD_PCM_STOP "pcm stop\n"

enum {
    CYD_PCM_BAUD = 533333,
    CYD_PCM_RATE = 24000,
    CYD_PCM_SAMPLES = 128,
    CYD_PCM_PAYLOAD_BYTES = CYD_PCM_SAMPLES * 2,
    CYD_PCM_SEQUENCE_OFFSET = 4,
    CYD_PCM_DATA_OFFSET = CYD_PCM_SEQUENCE_OFFSET + 4,
    CYD_PCM_CRC_OFFSET = CYD_PCM_DATA_OFFSET + CYD_PCM_PAYLOAD_BYTES,
    CYD_PCM_FRAME_BYTES = CYD_PCM_CRC_OFFSET + 2,
    CYD_PCM_PING_MS = 1000,
    CYD_PCM_TIMEOUT_MS = 4000,
};

static const uint8_t CYD_PCM_MAGIC[CYD_PCM_SEQUENCE_OFFSET] = {'C', 'Y', 'D', '1'};

static inline uint16_t cyd_pcm_read_u16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t cyd_pcm_read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline void cyd_pcm_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void cyd_pcm_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

// CRC-16/CCITT-FALSE, applied to the sequence number and PCM payload.
static inline uint16_t cyd_pcm_crc16(const uint8_t *bytes, size_t length) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

#endif

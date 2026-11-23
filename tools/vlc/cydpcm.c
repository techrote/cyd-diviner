/* SPDX-License-Identifier: MIT */

// The Windows SDK headers refer to VLC's internal poll compatibility wrapper.
struct pollfd;
int poll(struct pollfd *, unsigned, int);

#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_demux.h>
#include <vlc_fourcc.h>
#include <vlc_plugin.h>
#include <windows.h>

#include "../../src/cyd_pcm_protocol.h"

enum {
    READ_TIMEOUT_MS = 30,
    WRITE_TIMEOUT_MS = 100,
    MAX_SILENT_BLOCKS = 8,
    PORT_NAME_BYTES = sizeof("COM99999"),
};

struct demux_sys_t {
    HANDLE serial;
    es_out_id_t *audio;
    char port[PORT_NAME_BYTES];
    uint8_t buffer[CYD_PCM_FRAME_BYTES * 8];
    size_t used;
    uint32_t last_sequence;
    uint64_t frames, corrupt, missing, discontinuities;
    uint64_t clock_samples;
    vlc_tick_t clock_origin, opened, last_frame, next_ping, caching;
};

static int Open(vlc_object_t *object);
static void Close(vlc_object_t *object);

#define MODULE_STRING "cydpcm"
vlc_module_begin()
    set_shortname("Diviner CYD audio")
    set_description("Diviner CYD serial audio input")
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_ACCESS)
    set_capability("access_demux", 0)
    add_shortcut("cydpcm")
    set_callbacks(Open, Close)
vlc_module_end()

static bool PortName(const char *location, char port[PORT_NAME_BYTES]) {
    if (!location || strncasecmp(location, "COM", 3) != 0)
        return false;

    const char *number = location + 3;
    size_t digits = strspn(number, "0123456789");
    if (!digits || digits > 5 || number[0] == '0' || number[digits] != '\0')
        return false;

    snprintf(port, PORT_NAME_BYTES, "COM%s", number);
    return true;
}

static void SerialError(demux_t *demux, const char *operation, DWORD error) {
    msg_Err(demux, "%s on %s (Windows error %lu)", operation,
            demux->p_sys->port, (unsigned long)error);
}

static bool SerialSetup(demux_t *demux) {
    HANDLE port = demux->p_sys->serial;
    DCB config = { .DCBlength = sizeof(config) };
    if (!GetCommState(port, &config)) {
        SerialError(demux, "Cannot read serial settings", GetLastError());
        return false;
    }
    config.BaudRate = CYD_PCM_BAUD;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = FALSE;
    config.fOutxCtsFlow = FALSE;
    config.fOutxDsrFlow = FALSE;
    config.fDtrControl = DTR_CONTROL_DISABLE;
    config.fDsrSensitivity = FALSE;
    config.fOutX = FALSE;
    config.fInX = FALSE;
    config.fErrorChar = FALSE;
    config.fNull = FALSE;
    config.fRtsControl = RTS_CONTROL_DISABLE;
    config.fAbortOnError = FALSE;

    // Read pending bytes immediately, or wait only for the first incoming byte.
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout = MAXDWORD,
        .ReadTotalTimeoutMultiplier = MAXDWORD,
        .ReadTotalTimeoutConstant = READ_TIMEOUT_MS,
        .WriteTotalTimeoutConstant = WRITE_TIMEOUT_MS,
    };
    if (!SetCommState(port, &config) || !SetCommTimeouts(port, &timeouts) ||
        !PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
        SerialError(demux, "Cannot configure serial port", GetLastError());
        return false;
    }
    return true;
}

static bool SendCommand(demux_t *demux, const char *command) {
    DWORD sent;
    DWORD length = (DWORD)strlen(command);
    if (!WriteFile(demux->p_sys->serial, command, length, &sent, NULL)) {
        SerialError(demux, "Cannot write to serial port", GetLastError());
        return false;
    }
    if (sent != length) {
        msg_Err(demux, "Serial command timed out on %s", demux->p_sys->port);
        return false;
    }
    return true;
}

static void Consume(demux_sys_t *sys, size_t count) {
    sys->used -= count;
    memmove(sys->buffer, sys->buffer + count, sys->used);
}

static bool FindFrame(demux_sys_t *sys) {
    size_t pos = 0;
    while (sys->used - pos >= sizeof(CYD_PCM_MAGIC)) {
        const uint8_t *bytes = sys->buffer + pos;
        if (memcmp(bytes, CYD_PCM_MAGIC, sizeof(CYD_PCM_MAGIC)) == 0) {
            if (sys->used - pos < CYD_PCM_FRAME_BYTES)
                break;

            uint16_t crc = cyd_pcm_crc16(bytes + CYD_PCM_SEQUENCE_OFFSET,
                                        CYD_PCM_CRC_OFFSET - CYD_PCM_SEQUENCE_OFFSET);
            if (crc == cyd_pcm_read_u16(bytes + CYD_PCM_CRC_OFFSET)) {
                Consume(sys, pos);
                return true;
            }
            ++sys->corrupt;
        }
        ++pos;
    }
    // Retain any partial magic or frame at the end of the buffer for the next read.
    Consume(sys, pos);
    return false;
}

static vlc_tick_t SampleTime(uint64_t samples) {
    return (vlc_tick_t)(samples / CYD_PCM_RATE) * CLOCK_FREQ +
           (vlc_tick_t)(samples % CYD_PCM_RATE) * CLOCK_FREQ / CYD_PCM_RATE;
}

static int PlayFrame(demux_t *demux) {
    demux_sys_t *sys = demux->p_sys;
    const uint32_t sequence = cyd_pcm_read_u32(sys->buffer + CYD_PCM_SEQUENCE_OFFSET);
    uint32_t gap = 0;
    bool discontinuity = false;
    const vlc_tick_t now = mdate();

    if (sys->frames) {
        const uint32_t advance = sequence - sys->last_sequence;
        if (advance == 0) {
            Consume(sys, CYD_PCM_FRAME_BYTES);
            return VLC_DEMUXER_SUCCESS;
        }
        if (advance < UINT32_C(0x80000000)) {
            gap = advance - 1;
            sys->missing += gap;
        }
        discontinuity = advance >= UINT32_C(0x80000000) || gap > MAX_SILENT_BLOCKS;
    } else {
        sys->clock_origin = now;
        msg_Dbg(demux, "Receiving %u Hz mono PCM on %s", CYD_PCM_RATE, sys->port);
    }

    if (discontinuity) {
        ++sys->discontinuities;
        const vlc_tick_t next = sys->clock_origin + SampleTime(sys->clock_samples);
        sys->clock_origin = __MAX(now, next);
        sys->clock_samples = 0;
        gap = 0;
        es_out_Control(demux->out, ES_OUT_RESET_PCR);
    }

    size_t silence = gap * CYD_PCM_PAYLOAD_BYTES;
    block_t *block = block_Alloc(silence + CYD_PCM_PAYLOAD_BYTES);
    if (!block) {
        msg_Err(demux, "Cannot allocate a CYD audio block");
        return VLC_DEMUXER_EGENERIC;
    }
    memset(block->p_buffer, 0, silence);
    memcpy(block->p_buffer + silence, sys->buffer + CYD_PCM_DATA_OFFSET, CYD_PCM_PAYLOAD_BYTES);
    Consume(sys, CYD_PCM_FRAME_BYTES);

    block->i_nb_samples = (gap + 1) * CYD_PCM_SAMPLES;
    vlc_tick_t start = SampleTime(sys->clock_samples);
    sys->clock_samples += block->i_nb_samples;
    block->i_length = SampleTime(sys->clock_samples) - start;
    block->i_pts = block->i_dts = sys->clock_origin + start;
    if (discontinuity)
        block->i_flags |= BLOCK_FLAG_DISCONTINUITY;

    sys->last_sequence = sequence;
    sys->last_frame = now;
    ++sys->frames;
    es_out_SetPCR(demux->out, block->i_pts);
    es_out_Send(demux->out, sys->audio, block);
    return VLC_DEMUXER_SUCCESS;
}

static int Demux(demux_t *demux) {
    demux_sys_t *sys = demux->p_sys;
    vlc_tick_t now = mdate();

    if (now - sys->last_frame >= CYD_PCM_TIMEOUT_MS * INT64_C(1000)) {
        msg_Err(demux, "%s on %s at %u baud", sys->frames ? "CYD audio stopped" :
                "No valid CYD audio received. Check the Diviner firmware", sys->port, CYD_PCM_BAUD);
        return VLC_DEMUXER_EGENERIC;
    }

    if (now >= sys->next_ping) {
        // A retry allows the board to finish booting if it was just connected.
        if (!SendCommand(demux, sys->frames ? CYD_PCM_PING : CYD_PCM_START))
            return VLC_DEMUXER_EGENERIC;
        sys->next_ping = now + CYD_PCM_PING_MS * INT64_C(1000);
    }

    if (FindFrame(sys))
        return PlayFrame(demux);

    DWORD received;
    if (!ReadFile(sys->serial, sys->buffer + sys->used, (DWORD)(sizeof(sys->buffer) - sys->used),
                  &received, NULL)) {
        SerialError(demux, "Cannot read serial port", GetLastError());
        return VLC_DEMUXER_EGENERIC;
    }
    sys->used += received;
    return FindFrame(sys) ? PlayFrame(demux) : VLC_DEMUXER_SUCCESS;
}

static int Control(demux_t *demux, int query, va_list args) {
    switch (query) {
    case DEMUX_CAN_SEEK:
    case DEMUX_CAN_PAUSE:
    case DEMUX_CAN_CONTROL_PACE:
    case DEMUX_CAN_CONTROL_RATE:
        *va_arg(args, bool *) = false;
        return VLC_SUCCESS;
    case DEMUX_GET_PTS_DELAY:
        *va_arg(args, int64_t *) = demux->p_sys->caching;
        return VLC_SUCCESS;
    case DEMUX_GET_TIME:
        *va_arg(args, int64_t *) = mdate() - demux->p_sys->opened;
        return VLC_SUCCESS;
    default:
        return VLC_EGENERIC;
    }
}

static int Open(vlc_object_t *object) {
    demux_t *demux = (demux_t *)object;
    char port[PORT_NAME_BYTES];
    if (demux->b_preparsing)
        return VLC_EGENERIC;
    if (!PortName(demux->psz_location, port)) {
        msg_Err(demux, "Expected a Windows COM port, for example cydpcm://COM21");
        return VLC_EGENERIC;
    }

    demux_sys_t *sys = calloc(1, sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;
    demux->p_sys = sys;
    strcpy(sys->port, port);

    char device[4 + PORT_NAME_BYTES];
    snprintf(device, sizeof(device), "\\\\.\\%s", port);
    sys->serial = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (sys->serial == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        SerialError(demux, error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ?
                    "Serial port is busy or access was denied" : "Cannot open serial port", error);
        free(sys);
        return VLC_EGENERIC;
    }
    if (!SerialSetup(demux))
        goto error;

    es_format_t format;
    es_format_Init(&format, AUDIO_ES, VLC_CODEC_S16L);
    format.audio.i_format = VLC_CODEC_S16L;
    format.audio.i_rate = CYD_PCM_RATE;
    format.audio.i_channels = 1;
    format.audio.i_physical_channels = AOUT_CHAN_CENTER;
    format.audio.i_bitspersample = 16;
    format.audio.i_blockalign = 2;
    format.i_bitrate = CYD_PCM_RATE * 16;
    sys->audio = es_out_Add(demux->out, &format);
    es_format_Clean(&format);
    if (!sys->audio || !SendCommand(demux, CYD_PCM_START))
        goto error;

    sys->opened = sys->last_frame = mdate();
    sys->next_ping = sys->opened + CYD_PCM_PING_MS * INT64_C(1000);
    sys->caching = var_InheritInteger(demux, "live-caching") * INT64_C(1000);
    demux->pf_demux = Demux;
    demux->pf_control = Control;
    return VLC_SUCCESS;

error:
    if (sys->audio)
        es_out_Del(demux->out, sys->audio);
    CloseHandle(sys->serial);
    free(sys);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *object) {
    demux_t *demux = (demux_t *)object;
    demux_sys_t *sys = demux->p_sys;
    SendCommand(demux, CYD_PCM_STOP);
    CloseHandle(sys->serial);
    es_out_Del(demux->out, sys->audio);
    msg_Dbg(demux, "%s closed, %" PRIu64 " frames, %" PRIu64 " CRC errors, %" PRIu64
            " missing frames, %" PRIu64 " discontinuities", sys->port, sys->frames,
            sys->corrupt, sys->missing, sys->discontinuities);
    free(sys);
}

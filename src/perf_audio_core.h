// Portable audio experiment core: no radio, I2S, allocation or clock dependency.
#pragma once
#include <cstddef>
#include <cstdint>

namespace perf_audio_core {
constexpr size_t HeaderBytes=20, MaxPacketBytes=250, MaxPayloadBytes=230;
constexpr size_t MaxChannels=4, MaxFrames=32, MaxSamples=128, MaxBlockBytes=512, Slots=8;
enum class Kind:uint8_t { Pcm16=1, Pdm=2 };
struct Config {
    uint32_t rate=24000;
    uint8_t channels=1,frames=16;
    Kind kind=Kind::Pcm16;
    uint8_t oversample=0;
};
bool valid(const Config &);
bool same(const Config &,const Config &);
size_t blockBytes(const Config &);
size_t fragmentCount(const Config &);
// Header bytes: AP, v1, kind, session LE32, sequence LE32, ratecode(0/1),
// channels, frames, fragment index, payload bytes, OSR, CRC16 LE.
// CCITT-FALSE: poly1021/initFFFF, header CRC bytes treated as zero, then payload.
size_t packetize(const Config &,uint32_t session,uint32_t sequence,unsigned fragment,
                 const uint8_t *block,uint8_t *out,size_t capacity);
struct PacketView {
    Config config;
    uint32_t session=0,sequence=0;
    uint8_t fragment=0,fragments=0;
    size_t offset=0,length=0;
    const uint8_t *payload=nullptr;
};
enum class ParseResult:uint8_t { Okay, Invalid, BadCrc };
ParseResult parse(const uint8_t *,size_t,PacketView &);
size_t encodePcm(const Config &,const int16_t *interleaved,uint8_t *out,size_t capacity);
bool decodeBlock(const Config &,const uint8_t *,size_t,int16_t *interleaved);
class PdmEncoder {
    int32_t error[MaxChannels]{};
public:
    void reset();
    // First-order state persists across blocks. Bits: frame, OSR tick, channel;
    // LSB-first within bytes. Reconstruction is one causal OSR-sample boxcar.
    size_t encode(const Config &,const int16_t *,uint8_t *,size_t capacity);
};
class Source {
    uint32_t phase[MaxChannels]{};
public:
    void reset(uint32_t seed);
    void skip(const Config &,uint64_t sampleFrames);
    void seek(const Config &,uint32_t seed,uint64_t sampleIndex);
    void fill(const Config &,int16_t *interleaved);
    static unsigned frequency(unsigned channel) { return 220+37*channel; }
};
struct Stats {
    uint32_t accepted_packets=0,completed_blocks=0,invalid_packets=0,crc_errors=0;
    uint32_t wrong_session=0,wrong_config=0,duplicate_packets=0,stale_packets=0,future_packets=0;
    uint32_t rendered_blocks=0,concealed_blocks=0,concealed_frames=0,incomplete_blocks=0;
    uint32_t skipped_blocks=0,late_playout_blocks=0;
    uint64_t max_render_lateness_us=0,max_slot_dwell_us=0;
};
struct OutputInfo {
    uint32_t sequence=0;
    bool concealed=false;
    uint64_t scheduled_local_us=0,lateness_us=0,slot_dwell_us=0;
};
class Receiver {
    struct Slot {
        uint8_t data[MaxBlockBytes]{};
        uint32_t sequence=0;
        uint64_t first_local_us=0;
        uint8_t fragments=0;
        bool used=false;
    } slots[Slots];
    Config config{};
    uint32_t session=0,base_sequence=0;
    uint64_t anchor_us=0,next_index=0;
    uint8_t delay_blocks=2;
    bool configured=false,anchored=false,was_concealed=false;
    int16_t previous[MaxChannels]{};
    bool recovering[MaxChannels]{};
    Stats counters{};
    uint64_t due(uint64_t index) const;
    void clearSlot(Slot &,uint64_t now);
public:
    bool reset(const Config &,uint32_t session,uint8_t delayBlocks=2);
    bool receive(const uint8_t *,size_t,uint64_t localNowUs);
    // Emits at most one block/call. A stall >2 block periods skips obsolete
    // blocks arithmetically and scans at most Slots entries, never catch-up loops.
    bool render(uint64_t localNowUs,int16_t *out,OutputInfo *info=nullptr);
    const Stats &stats() const { return counters; }
    bool hasAnchor() const { return anchored; }
    uint64_t nextDeadlineUs() const { return anchored ? due(next_index) : 0; }
};
static_assert(sizeof(Receiver)+sizeof(PdmEncoder)+sizeof(Source)<8192,"audio core working state exceeds budget");
}

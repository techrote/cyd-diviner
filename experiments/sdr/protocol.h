// Greenfield experimental NRZ framing. No borrowed SDR implementation.
#pragma once
#include <cstddef>
#include <cstdint>
namespace sdr_protocol {
constexpr uint32_t SyncWord=0xd391c5a7u;
constexpr size_t TrainingSymbols=32,SyncSymbols=32,HeaderBytes=6,MaxPayloadBits=256;
constexpr size_t MaxFrameSymbols=TrainingSymbols+SyncSymbols+HeaderBytes*8+MaxPayloadBits;
constexpr size_t MaxChannels=4,MaxAudioSamples=64;
struct Config {
    uint32_t source_rate_hz=2000;
    uint8_t channels=2,oversample=4,stream_id=1;
    uint16_t payload_bits=256;
};
bool valid(const Config &);
size_t audioFrames(const Config &);
size_t frameSymbols(const Config &);
double requiredBaud(const Config &); // includes training/sync/header overhead.
uint8_t headerCrc8(const uint8_t *,size_t);
// Wire: alternating32(0 first), sync32 MSB-first, then header bytes MSB-first:
// sequenceLE16, streamID8, configLE16, CRC8(first5bytes,poly07/init0).
// Config bits: channels0..1,OSR2..3,rate4..7,payloadLength8,reserved9..15=0.
// Payload bits are LSB-first in their own bytes and deliberately have no CRC.
size_t buildFrame(const Config &,uint16_t sequence,const uint8_t *payload,
                  uint8_t *symbols,size_t symbolCapacity);
bool decodeHeader(const uint8_t *header,Config &,uint16_t &sequence);
class PdmSource {
    uint32_t phase[MaxChannels]{};
    int32_t error[MaxChannels]{};
public:
    void reset(uint32_t seed);
    // Continuous sigma-delta state across frames; payload order is PCM frame,
    // OSR tick, channel. Optional reference contains audioFrames*channels PCM16.
    bool payload(const Config &,uint8_t *out,size_t bytes,int16_t *reference=nullptr);
    static unsigned frequency(unsigned ch) { return 30+17*ch; }
};
bool reconstruct(const Config &,const uint8_t *payload,int16_t *pcm,size_t sampleCapacity);
struct IQ { float i,q; };
enum class Modulation:uint8_t { Ook, Fsk2 };
struct ReceiveOptions {
    double sample_rate_hz=250000,symbol_rate=23000;
    double clock_search_ppm=4000,clock_step_ppm=500;
    Modulation modulation=Modulation::Ook;
    uint8_t stream_id=1;
    bool filter_stream_id=true;
};
struct Frame {
    Config config;
    uint16_t sequence=0;
    uint8_t payload[MaxPayloadBits/8]{};
    uint64_t first_sample=0;
    double recovered_samples_per_symbol=0,training_fit_error=0,payload_confidence_mean=0;
    uint32_t low_confidence_payload_bits=0;
};
struct DecodeStats {
    uint64_t input_samples=0,training_candidates=0,sync_candidates=0,header_crc_rejects=0;
    uint64_t header_config_rejects=0,wrong_stream=0,frames=0,payload_bits=0,sequence_gaps=0,duplicates=0;
};
using FrameSink=void(*)(const Frame &,void *);
// Host/batch IQ analysis. Uses caller's samples and host-side vector scratch;
// does not claim on-device real-time demodulation or RF latency.
bool demodulate(const IQ *,size_t,const ReceiveOptions &,FrameSink,void *,DecodeStats &);
}

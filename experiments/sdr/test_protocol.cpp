// Greenfield host oracle and IQ model. No physical RF/latency claims.
// g++ -std=c++17 -O2 protocol.cpp test_protocol.cpp -o sdr-model
#include "protocol.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace s=sdr_protocol;
constexpr double Pi=3.14159265358979323846;
static uint64_t checks=0;
static void need(bool v){++checks;if(!v){std::fprintf(stderr,"SDR assertion %llu failed\n",(unsigned long long)checks);std::abort();}}
static uint32_t rng(uint32_t &x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
static double normal(uint32_t &x){const double a=(rng(x)+1.)/4294967297.,b=(rng(x)+1.)/4294967297.;return std::sqrt(-2*std::log(a))*std::cos(2*Pi*b);}
static bool same(const s::Config&a,const s::Config&b){return a.source_rate_hz==b.source_rate_hz&&a.channels==b.channels&&a.oversample==b.oversample&&a.stream_id==b.stream_id&&a.payload_bits==b.payload_bits;}
static void pcm(std::FILE *f,const int16_t *p,size_t n){for(size_t i=0;i<n;++i){const uint16_t v=uint16_t(p[i]);if(std::fputc(v&255,f)==EOF||std::fputc(v>>8,f)==EOF)throw std::string("PCM write failed");}}
struct Capture {
    std::vector<s::Frame> frames;
    static void sink(const s::Frame &f,void *p){static_cast<Capture*>(p)->frames.push_back(f);}
};
struct Model {
    s::Config config;
    std::vector<uint8_t> symbols;
    std::vector<std::vector<uint8_t>> payloads;
    std::vector<int16_t> ideal;
    std::vector<s::IQ> iq;
    double baud=0,sample_rate=250000,noise_sigma=0,true_period=0;
    unsigned changed_payload_symbols=0;
};
static Model model(double snr,double cfo,double drift,unsigned burst,s::Modulation modulation,bool corruptHeader=false,double sampleRate=250000,double lead=37.25) {
    Model m;m.sample_rate=sampleRate;m.baud=s::requiredBaud(m.config);m.true_period=m.sample_rate/(m.baud*(1+drift*1e-6));
    s::PdmSource source;source.reset(23063);constexpr unsigned Count=8;
    for(unsigned seq=0;seq<Count;++seq){uint8_t bits[32],wire[s::MaxFrameSymbols];int16_t ideal[s::MaxAudioSamples];need(source.payload(m.config,bits,sizeof(bits),ideal));m.payloads.emplace_back(bits,bits+32);m.ideal.insert(m.ideal.end(),ideal,ideal+s::audioFrames(m.config)*m.config.channels);const size_t n=s::buildFrame(m.config,uint16_t(seq),bits,wire,sizeof(wire));need(n==368);m.symbols.insert(m.symbols.end(),wire,wire+n);}
    const size_t payloadStart=s::TrainingSymbols+s::SyncSymbols+s::HeaderBytes*8;
    for(unsigned i=0;i<std::min<unsigned>(burst,m.config.payload_bits);++i){m.symbols[2*368+payloadStart+i]^=1;++m.changed_payload_symbols;}
    if(corruptHeader)m.symbols[3*368+s::TrainingSymbols+s::SyncSymbols+3]^=1;
    const size_t count=size_t(std::ceil(lead+m.true_period*m.symbols.size()+m.true_period*4));m.iq.resize(count);
    m.noise_sigma=.8/std::sqrt(2*std::pow(10.,snr/10));uint32_t state=0x56b47u;double phase=0;
    for(size_t n=0;n<count;++n){const double symbol=(double(n)-lead)/m.true_period;const bool in=symbol>=0&&symbol<m.symbols.size();const unsigned bit=in?m.symbols[size_t(symbol)]:0;const double frequency=cfo+(modulation==s::Modulation::Fsk2?(bit?m.sample_rate/32:-m.sample_rate/32):0);phase+=2*Pi*frequency/m.sample_rate;const double amplitude=modulation==s::Modulation::Fsk2?.8:(bit?.8:.04);double i=amplitude*std::cos(phase)+normal(state)*m.noise_sigma,q=amplitude*std::sin(phase)+normal(state)*m.noise_sigma;
        // Quantized int8 IQ is the actual model/demod boundary, matching the
        // supported host capture input; clipping is part of this simulation.
        i=std::max(-128.,std::min(127.,std::round(i*127)));q=std::max(-128.,std::min(127.,std::round(q*127)));m.iq[n]={float(i/127),float(q/127)};}
    return m;
}
struct Quality {uint64_t compared_bits=0,bit_errors=0,received_frames=0,missing_frames=0;double rms=0,snr=0;int maximum=0;std::vector<int16_t> decoded;};
static Quality quality(const Model &m,const Capture &capture) {
    Quality q;q.decoded.resize(m.ideal.size(),0);std::vector<bool> seen(m.payloads.size(),false);
    for(const auto &f:capture.frames){if(f.sequence>=m.payloads.size()||!same(f.config,m.config)||seen[f.sequence])continue;seen[f.sequence]=true;++q.received_frames;for(unsigned b=0;b<f.config.payload_bits;++b){q.bit_errors+=unsigned(((f.payload[b/8]^m.payloads[f.sequence][b/8])>>(b%8))&1);++q.compared_bits;}int16_t output[s::MaxAudioSamples];need(s::reconstruct(f.config,f.payload,output,s::MaxAudioSamples));std::copy(output,output+s::audioFrames(f.config)*f.config.channels,q.decoded.begin()+f.sequence*s::audioFrames(f.config)*f.config.channels);}
    q.missing_frames=m.payloads.size()-q.received_frames;long double e=0,signal=0;
    for(size_t n=0;n<m.ideal.size();++n){const int d=int(q.decoded[n])-m.ideal[n];q.maximum=std::max(q.maximum,std::abs(d));e+=static_cast<long double>(d)*d;signal+=static_cast<long double>(m.ideal[n])*m.ideal[n];}
    q.rms=std::sqrt(double(e/m.ideal.size()));q.snr=e&&signal?10*std::log10(double(signal/e)):0;return q;
}
static void tests() {
    need(s::headerCrc8(reinterpret_cast<const uint8_t*>("123456789"),9)==0xf4);
    for(uint32_t rate:{250u,500u,1000u,2000u,4000u,8000u,16000u,24000u,48000u})for(uint8_t ch:{1,2,4})for(uint8_t osr:{4,8,16,32})for(uint16_t bits:{128,256}) {
        s::Config c{rate,ch,osr,0x7a,bits};need(s::valid(c));s::PdmSource source;source.reset(9);uint8_t payload[32],wire[s::MaxFrameSymbols];need(source.payload(c,payload,sizeof(payload)));const size_t n=s::buildFrame(c,0xfedc,payload,wire,sizeof(wire));need(n==s::frameSymbols(c));uint8_t header[6]{};for(unsigned i=0;i<48;++i)header[i/8]|=uint8_t(wire[64+i]<<(7-i%8));s::Config restored;uint16_t seq=0;need(s::decodeHeader(header,restored,seq)&&same(c,restored)&&seq==0xfedc);for(unsigned bit=0;bit<48;++bit){header[bit/8]^=uint8_t(1u<<(bit%8));need(!s::decodeHeader(header,restored,seq));header[bit/8]^=uint8_t(1u<<(bit%8));}for(unsigned bit=0;bit<bits;++bit)need(wire[112+bit]==((payload[bit/8]>>(bit%8))&1));
    }
    std::printf("{\"schema\":\"greenfield_sdr_model_tests/1\",\"evidence_class\":\"OFFLINE_MODEL\",\"no_RF_measurement\":true,\"cases\":[");
    struct Case{const char *name;double snr,cfo,drift;unsigned burst;s::Modulation modulation;bool badHeader;};
    const Case cases[]={{"clean",100,0,0,0,s::Modulation::Ook,false},{"awgn20",20,0,0,0,s::Modulation::Ook,false},{"cfo5000",30,5000,0,0,s::Modulation::Ook,false},{"clock2000ppm",30,1000,2000,0,s::Modulation::Ook,false},{"payload_burst16",100,0,0,16,s::Modulation::Ook,false},{"header_error",100,0,0,0,s::Modulation::Ook,true},{"fsk_model",30,1000,1000,0,s::Modulation::Fsk2,false}};
    bool first=true;
    for(const auto &c:cases){const auto m=model(c.snr,c.cfo,c.drift,c.burst,c.modulation,c.badHeader);s::ReceiveOptions opts;opts.symbol_rate=m.baud;opts.modulation=c.modulation;Capture decoded;s::DecodeStats stats;need(s::demodulate(m.iq.data(),m.iq.size(),opts,Capture::sink,&decoded,stats));const auto q=quality(m,decoded);need(q.received_frames==(c.badHeader?7u:8u));if(c.burst)need(q.bit_errors>=c.burst);else need(q.bit_errors==0);std::printf("%s{\"case\":\"%s\",\"frames_received\":%llu,\"frames_missing\":%llu,\"payload_bits_compared\":%llu,\"payload_bit_errors\":%llu,\"full_source_zero_filled_missing_error_rms_lsb\":%.12g,\"full_source_snr_db\":%.12g,\"header_crc_reject_candidates\":%llu}",first?"":",",c.name,(unsigned long long)q.received_frames,(unsigned long long)q.missing_frames,(unsigned long long)q.compared_bits,(unsigned long long)q.bit_errors,q.rms,q.snr,(unsigned long long)stats.header_crc_rejects);first=false;}
    std::vector<s::IQ> noise(30000);uint32_t state=971;
    for(auto &v:noise)v={float(normal(state)),float(normal(state))};
    s::ReceiveOptions bounded;bounded.clock_search_ppm=0;bounded.clock_step_ppm=1e-12;Capture empty;s::DecodeStats noiseStats;
    need(s::demodulate(noise.data(),noise.size(),bounded,Capture::sink,&empty,noiseStats));need(empty.frames.empty());
    bounded.clock_search_ppm=4000;bounded.clock_step_ppm=1e-12;need(!s::demodulate(noise.data(),noise.size(),bounded,Capture::sink,&empty,noiseStats));
    std::printf("],\"checks\":%llu,\"valid\":true}\n",(unsigned long long)checks);
}
// Separate cohort: retain the original host-validation.json as historical evidence.
static void lowSpsTests() {
    std::printf("{\"schema\":\"greenfield_sdr_low_sps_tests/1\",\"evidence_class\":\"OFFLINE_MODEL\",\"no_RF_measurement\":true,\"wire_or_source_algorithm_changed\":false,\"cases\":[");
    struct Case {const char *name;double rate,snr,drift,lead;unsigned burst;bool header;};
    std::vector<Case> cases;
    // Independently vary the first edge over an entire symbol, including
    // sub-sample phases. These are separate IQ realizations, not shifted
    // decoder output compared against itself.
    for(unsigned phase=0;phase<16;++phase)
        cases.push_back({"phase_sweep",125000,100,0,37.+phase*(125000./23000)/16.,0,false});
    for(unsigned phase=0;phase<8;++phase) {
        const double lead=19.125+phase*(125000./23000)/8.;
        cases.push_back({"awgn20_drift_positive",125000,20,3500,lead,0,false});
        cases.push_back({"awgn20_drift_negative",125000,20,-3500,lead,0,false});
    }
    cases.push_back({"payload_burst16",125000,100,0,37.25,16,false});
    cases.push_back({"header_error_resync",125000,100,0,37.25,0,true});
    cases.push_back({"combined_impairments",125000,20,2000,41.75,16,true});
    for(unsigned phase=0;phase<8;++phase)
        cases.push_back({"four_sps_boundary",92000,30,0,23.+phase/2.,0,false});
    bool first=true;
    for(const auto &c:cases) {
        const auto m=model(c.snr,5000,c.drift,c.burst,s::Modulation::Ook,c.header,c.rate,c.lead);
        s::ReceiveOptions opts;opts.sample_rate_hz=m.sample_rate;opts.symbol_rate=m.baud;
        Capture decoded;s::DecodeStats stats;
        need(s::demodulate(m.iq.data(),m.iq.size(),opts,Capture::sink,&decoded,stats));
        const auto q=quality(m,decoded);
        if(q.received_frames!=(c.header?7u:8u)||(!c.burst&&q.bit_errors)||q.bit_errors<c.burst) {
            std::fprintf(stderr,"Low-SPS case %s rate %.1f phase %.12g drift %.1f: frames %llu errors %llu\n",c.name,c.rate,c.lead,c.drift,(unsigned long long)q.received_frames,(unsigned long long)q.bit_errors);
            need(false);
        }
        need(q.received_frames==(c.header?7u:8u));need(c.burst?q.bit_errors>=c.burst:q.bit_errors==0);
        for(const auto &frame:decoded.frames) {
            need(frame.config.channels==2&&frame.config.oversample==4&&frame.config.source_rate_hz==2000);
            need(frame.recovered_samples_per_symbol>=3.9&&frame.recovered_samples_per_symbol<=5.6);
        }
        std::printf("%s{\"case\":\"%s\",\"iq_sample_rate_hz\":%.12g,\"symbol_rate\":%.12g,\"nominal_samples_per_symbol\":%.12g,\"first_symbol_sample\":%.12g,\"snr_on_carrier_db\":%.12g,\"cfo_hz\":5000,\"clock_drift_ppm\":%.12g,\"payload_symbols_inverted\":%u,\"header_symbol_inverted\":%s,\"frames_source\":8,\"frames_received\":%llu,\"frames_missing\":%llu,\"payload_bits_compared\":%llu,\"payload_bit_errors\":%llu,\"sequence_gaps\":%llu,\"header_crc_reject_candidates\":%llu}",first?"":",",c.name,m.sample_rate,m.baud,m.sample_rate/m.baud,c.lead,c.snr,c.drift,c.burst,c.header?"true":"false",(unsigned long long)q.received_frames,(unsigned long long)q.missing_frames,(unsigned long long)q.compared_bits,(unsigned long long)q.bit_errors,(unsigned long long)stats.sequence_gaps,(unsigned long long)stats.header_crc_rejects);
        first=false;
    }
    // Boundary validation is explicit; <4 SPS is unsupported. Pure noise
    // at the newly admitted sample density must not become a known frame.
    std::vector<s::IQ> noise(20000);uint32_t state=0x92b7;
    for(auto &v:noise)v={float(normal(state)),float(normal(state))};
    s::ReceiveOptions opts;opts.sample_rate_hz=125000;opts.symbol_rate=23000;
    Capture empty;s::DecodeStats stats;need(s::demodulate(noise.data(),noise.size(),opts,Capture::sink,&empty,stats));need(empty.frames.empty());
    opts.sample_rate_hz=91999;need(!s::demodulate(noise.data(),noise.size(),opts,Capture::sink,&empty,stats));
    std::printf("],\"noise_only_frames\":0,\"below_four_sps_rejected\":true,\"checks\":%llu,\"valid\":true}\n",(unsigned long long)checks);
}
static int demodCli(int argc,char **argv) {
    if(argc!=8)return 2;
    std::ifstream in(argv[2],std::ios::binary);if(!in)throw std::string("cannot open IQ file");const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());if(bytes.empty()||bytes.size()%2||bytes.size()>20000000)throw std::string("IQ input must be1..10000000 signed8 IQ pairs");
    std::vector<s::IQ> iq(bytes.size()/2);for(size_t n=0;n<iq.size();++n){const int i=uint8_t(bytes[2*n]),q=uint8_t(bytes[2*n+1]);iq[n]={float((i<128?i:i-256)/127.),float((q<128?q:q-256)/127.)};}
    s::ReceiveOptions options;options.sample_rate_hz=std::stod(argv[3]);options.symbol_rate=std::stod(argv[4]);const unsigned stream=unsigned(std::stoul(argv[7]));if(stream>255)return 2;options.stream_id=uint8_t(stream);Capture capture;s::DecodeStats stats;if(!s::demodulate(iq.data(),iq.size(),options,Capture::sink,&capture,stats))throw std::string("invalid demodulation options/input");
    std::FILE *out=std::fopen(argv[5],"wb"),*records=std::fopen(argv[6],"wb");if(!out||!records){if(out)std::fclose(out);if(records)std::fclose(records);throw std::string("cannot create decoded artifacts");}
    uint64_t pcmFrames=0,configChanges=0;const s::Config config=capture.frames.empty()?s::Config{}:capture.frames.front().config;
    for(const auto &f:capture.frames){const bool accepted=same(config,f.config);if(!accepted)++configChanges;int16_t audio[s::MaxAudioSamples];if(accepted){need(s::reconstruct(f.config,f.payload,audio,s::MaxAudioSamples));pcm(out,audio,s::audioFrames(f.config)*f.config.channels);}std::fprintf(records,"{\"schema\":\"greenfield_sdr_frame/1\",\"sequence\":%u,\"stream_id\":%u,\"source_rate_hz\":%u,\"channels\":%u,\"oversample\":%u,\"payload_bits\":%u,\"first_iq_sample\":%llu,\"recovered_samples_per_symbol\":%.12g,\"training_fit_error\":%.12g,\"payload_confidence_mean\":%.12g,\"low_confidence_payload_bits\":%u,\"header_crc_valid\":true,\"payload_crc_present\":false,\"pcm_written\":%s,\"pcm_offset_frames\":%llu,\"payload_hex\":\"",unsigned(f.sequence),unsigned(f.config.stream_id),f.config.source_rate_hz,unsigned(f.config.channels),unsigned(f.config.oversample),unsigned(f.config.payload_bits),(unsigned long long)f.first_sample,f.recovered_samples_per_symbol,f.training_fit_error,f.payload_confidence_mean,f.low_confidence_payload_bits,accepted?"true":"false",(unsigned long long)pcmFrames);for(unsigned b=0;b<f.config.payload_bits/8;++b)std::fprintf(records,"%02x",f.payload[b]);std::fprintf(records,"\"}\n");if(accepted)pcmFrames+=s::audioFrames(f.config);}
    const bool closedOut=std::fclose(out)==0,closedRecords=std::fclose(records)==0;if(!closedOut||!closedRecords)throw std::string("artifact close failed");
    std::printf("{\"schema\":\"greenfield_sdr_demod/1\",\"evidence_class\":\"HOST_DEMODULATION\",\"rf_origin_verified\":false,\"iq_format\":\"signed8_interleaved_IQ\",\"input_iq_samples\":%llu,\"sample_rate_hz\":%.12g,\"requested_symbol_rate\":%.12g,\"stream_id\":%u,\"frames_decoded\":%llu,\"sequence_gaps\":%llu,\"duplicate_frames\":%llu,\"header_crc_reject_candidates\":%llu,\"header_config_reject_candidates\":%llu,\"config_changes\":%llu,\"source_rate_hz\":%u,\"channels\":%u,\"oversample\":%u,\"payload_bits\":%u,\"pcm_frames\":%llu,\"pcm_bytes\":%llu,\"pcm_layout\":\"concatenated_received_frames_no_gap_concealment\",\"one_way_latency_measured\":false,\"payload_bit_errors_accepted\":true}\n",(unsigned long long)iq.size(),options.sample_rate_hz,options.symbol_rate,stream,(unsigned long long)stats.frames,(unsigned long long)stats.sequence_gaps,(unsigned long long)stats.duplicates,(unsigned long long)stats.header_crc_rejects,(unsigned long long)stats.header_config_rejects,(unsigned long long)configChanges,config.source_rate_hz,unsigned(config.channels),unsigned(config.oversample),unsigned(config.payload_bits),(unsigned long long)pcmFrames,(unsigned long long)(pcmFrames*config.channels*2));
    return capture.frames.empty()||configChanges?3:0;
}
static int modelCli(int argc,char **argv) {
    if(argc!=8)return 2;
    const std::string prefix=argv[2];const double snr=std::stod(argv[3]),cfo=std::stod(argv[4]),drift=std::stod(argv[5]);const unsigned burst=unsigned(std::stoul(argv[6]));const s::Modulation modulation=!std::strcmp(argv[7],"ook")?s::Modulation::Ook:s::Modulation::Fsk2;if(std::strcmp(argv[7],"ook")&&std::strcmp(argv[7],"fsk"))return 2;
    if(!std::isfinite(snr)||snr<0||snr>120||!std::isfinite(cfo)||std::abs(cfo)>20000||!std::isfinite(drift)||std::abs(drift)>4000||burst>256)return 2;
    const auto m=model(snr,cfo,drift,burst,modulation);s::ReceiveOptions opts;opts.symbol_rate=m.baud;opts.modulation=modulation;Capture capture;s::DecodeStats stats;need(s::demodulate(m.iq.data(),m.iq.size(),opts,Capture::sink,&capture,stats));const auto q=quality(m,capture);
    std::FILE *iq=std::fopen((prefix+"-iq.i8").c_str(),"wb"),*ref=std::fopen((prefix+"-reference.pcm").c_str(),"wb"),*decoded=std::fopen((prefix+"-decoded.pcm").c_str(),"wb");if(!iq||!ref||!decoded)throw std::string("cannot open model artifacts");for(const auto &v:m.iq){std::fputc(uint8_t(int(std::round(v.i*127))),iq);std::fputc(uint8_t(int(std::round(v.q*127))),iq);}pcm(ref,m.ideal.data(),m.ideal.size());pcm(decoded,q.decoded.data(),q.decoded.size());const bool c1=std::fclose(iq)==0,c2=std::fclose(ref)==0,c3=std::fclose(decoded)==0;if(!c1||!c2||!c3)throw std::string("model artifact close failed");
    std::printf("{\"schema\":\"greenfield_sdr_iq_model/1\",\"evidence_class\":\"OFFLINE_MODEL\",\"no_RF_measurement\":true,\"source_rate_hz\":%u,\"channels\":%u,\"oversample\":%u,\"payload_bits\":%u,\"frame_symbols\":%zu,\"symbol_rate\":%.12g,\"iq_sample_rate_hz\":%.12g,\"snr_on_carrier_db_configured\":%.12g,\"awgn_sigma_per_component\":%.12g,\"cfo_hz\":%.12g,\"clock_drift_ppm\":%.12g,\"payload_burst_bits\":%u,\"modulation\":\"%s\",\"RF_filter_model\":\"ideal_passband_no_hardware_selectivity\",\"frames_source\":8,\"frames_received\":%llu,\"frames_missing\":%llu,\"payload_bits_compared\":%llu,\"payload_bit_errors\":%llu,\"full_source_zero_filled_missing_error_rms_lsb\":%.12g,\"full_source_snr_db\":%.12g,\"pcm_frames\":%zu,\"pcm_bytes\":%zu}\n",m.config.source_rate_hz,unsigned(m.config.channels),unsigned(m.config.oversample),unsigned(m.config.payload_bits),s::frameSymbols(m.config),m.baud,m.sample_rate,snr,m.noise_sigma,cfo,drift,burst,argv[7],(unsigned long long)q.received_frames,(unsigned long long)q.missing_frames,(unsigned long long)q.compared_bits,(unsigned long long)q.bit_errors,q.rms,q.snr,m.ideal.size()/m.config.channels,m.ideal.size()*2);return 0;
}
static int emitCli(int argc,char **argv) {
    if(argc!=11){std::fprintf(stderr,"--emit PREFIX SOURCE_RATE CH OSR PAYLOAD_BITS BAUD FRAME_COUNT SEED STREAM_ID\n");return 2;}
    if(argv[7][0]<'0'||argv[7][0]>'9')return 2;
    size_t baud_end=0;const auto baud_wide=std::stoull(argv[7],&baud_end);
    // This is stimulus metadata, bounded by the probe's 240 MHz CPU clock;
    // packed symbols and PCM generation do not depend on requested baud.
    if(baud_end!=std::strlen(argv[7])||!baud_wide||baud_wide>240000000ull)return 2;
    const std::string prefix=argv[2];const unsigned rate=unsigned(std::stoul(argv[3])),ch=unsigned(std::stoul(argv[4])),osr=unsigned(std::stoul(argv[5])),payload=unsigned(std::stoul(argv[6])),baud=unsigned(baud_wide),count=unsigned(std::stoul(argv[8])),seed=unsigned(std::stoul(argv[9])),stream=unsigned(std::stoul(argv[10]));
    if(ch>255||osr>255||payload>65535||stream>255||!count||count>65536)return 2;
    s::Config config{rate,uint8_t(ch),uint8_t(osr),uint8_t(stream),uint16_t(payload)};if(!s::valid(config))return 2;
    std::FILE *packed=std::fopen((prefix+"-symbols.bits").c_str(),"wb"),*reference=std::fopen((prefix+"-reference.pcm").c_str(),"wb"),*boxcar=std::fopen((prefix+"-boxcar.pcm").c_str(),"wb");if(!packed||!reference||!boxcar)throw std::string("cannot open emitted stream artifacts");
    s::PdmSource source;source.reset(seed);
    for(unsigned seq=0;seq<count;++seq){uint8_t bits[32],symbols[s::MaxFrameSymbols];int16_t ideal[s::MaxAudioSamples],decoded[s::MaxAudioSamples];need(source.payload(config,bits,sizeof(bits),ideal));const size_t n=s::buildFrame(config,uint16_t(seq),bits,symbols,sizeof(symbols));need(n&&n%8==0);for(size_t i=0;i<n;i+=8){uint8_t byte=0;for(unsigned bit=0;bit<8;++bit)byte|=uint8_t(symbols[i+bit]<<(7-bit));if(std::fputc(byte,packed)==EOF)throw std::string("packed stream write failed");}need(s::reconstruct(config,bits,decoded,s::MaxAudioSamples));pcm(reference,ideal,s::audioFrames(config)*ch);pcm(boxcar,decoded,s::audioFrames(config)*ch);}
    const bool a=std::fclose(packed)==0,b=std::fclose(reference)==0,c=std::fclose(boxcar)==0;if(!a||!b||!c)throw std::string("emitted artifact close failed");
    const uint64_t symbols=uint64_t(count)*s::frameSymbols(config),audioFrames=uint64_t(count)*s::audioFrames(config);const double effective=double(baud)*config.payload_bits/(s::frameSymbols(config)*ch*osr);
    std::printf("{\"schema\":\"greenfield_sdr_emit/1\",\"evidence_class\":\"GENERATED_STIMULUS\",\"symbol_bit_order\":\"MSB_first_per_byte\",\"source_rate_hz\":%u,\"effective_audio_rate_hz_at_requested_baud\":%.12g,\"channels\":%u,\"oversample\":%u,\"payload_bits\":%u,\"baud_requested\":%u,\"baud_required_for_source_rate\":%.12g,\"frame_count\":%u,\"frame_symbols\":%zu,\"symbols_total\":%llu,\"packed_bytes\":%llu,\"pcm_frames\":%llu,\"pcm_bytes\":%llu,\"seed\":%u,\"stream_id\":%u,\"source_frequency0_hz\":30,\"no_RF_measurement\":true,\"payload_crc_present\":false}\n",rate,effective,ch,osr,payload,baud,s::requiredBaud(config),count,s::frameSymbols(config),(unsigned long long)symbols,(unsigned long long)(symbols/8),(unsigned long long)audioFrames,(unsigned long long)(audioFrames*ch*2),seed,stream);return 0;
}
int main(int argc,char **argv) {
    try {if(argc==1){tests();return 0;}if(argc==2&&!std::strcmp(argv[1],"--low-sps-tests")){lowSpsTests();return 0;}if(!std::strcmp(argv[1],"--demod-i8"))return demodCli(argc,argv);if(!std::strcmp(argv[1],"--model"))return modelCli(argc,argv);if(!std::strcmp(argv[1],"--emit"))return emitCli(argc,argv);std::fprintf(stderr,"Unknown command\n");return 2;}
    catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 2;}catch(const std::string&e){std::fprintf(stderr,"%s\n",e.c_str());return 2;}
}

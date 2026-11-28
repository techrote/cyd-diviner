// Portable correctness tests and continuous raw-bit impairment model.
// No radio/I2S measurements are produced by this executable.
// Compile core with -DGRIM_PERF_AUDIO_CORE_STANDALONE=1 for this host runner.
#include "perf_audio_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
namespace a=perf_audio_core;
static unsigned long long checks=0;
static void need(bool okay) {++checks;if(!okay){std::fprintf(stderr,"audio assertion %llu failed\n",checks);std::abort();}}
static uint32_t random32(uint32_t &s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static uint64_t number(const char *s){char *end=nullptr;errno=0;const auto n=std::strtoull(s,&end,10);if(s[0]<'0'||s[0]>'9'||!end||*end||errno==ERANGE){std::fprintf(stderr,"invalid integer\n");std::exit(2);}return n;}
static void pcmWrite(std::FILE *f,const int16_t *p,size_t n){for(size_t i=0;i<n;++i){const uint16_t v=uint16_t(p[i]);if(std::fputc(v&255,f)==EOF||std::fputc(v>>8,f)==EOF){std::fprintf(stderr,"PCM write failed\n");std::exit(2);}}}
static int simulate(int argc,char **argv) {
    if(argc!=12){std::fprintf(stderr,"--simulate RATE CH BLOCK_FRAMES OSR TOTAL_PCM_FRAMES SEED IMPAIRMENT STRENGTH REF_PATH OUT_PATH\n");return 2;}
    const uint64_t rate=number(argv[2]),channels=number(argv[3]),frames=number(argv[4]),osr=number(argv[5]),total=number(argv[6]),seed=number(argv[7]),strength=number(argv[9]);
    if(rate>UINT32_MAX||channels>255||frames>255||osr>255||seed>UINT32_MAX)return 2;
    a::Config c{uint32_t(rate),uint8_t(channels),uint8_t(frames),a::Kind::Pdm,uint8_t(osr)};
    const char *mode=argv[8];const bool clean=!std::strcmp(mode,"clean"),flip=!std::strcmp(mode,"flip"),burst=!std::strcmp(mode,"burst"),stuck=!std::strcmp(mode,"stuck"),erase=!std::strcmp(mode,"erase");
    if(!a::valid(c)||!total||total>10000000||total%frames||!(clean||flip||burst||stuck||erase)||(flip&&strength>1000000))return 2;
    if(!std::strcmp(argv[10],argv[11]))return 2;
    std::FILE *ref=std::fopen(argv[10],"wb"),*out=std::fopen(argv[11],"wb");
    if(!ref||!out){if(ref)std::fclose(ref);if(out)std::fclose(out);std::fprintf(stderr,"cannot open PCM output\n");return 2;}
    a::Source source;source.reset(uint32_t(seed));a::PdmEncoder encoder;
    int16_t ideal[a::MaxSamples],decoded[a::MaxSamples];uint8_t bits[a::MaxBlockBytes];
    const uint64_t count=total*channels*osr,length=std::min(strength,count),begin=(count-length)/2,end=begin+length;
    uint64_t at=0,altered=0,lost=0;uint32_t rng=uint32_t(seed)?uint32_t(seed):0x6d2b79f5u;bool last[a::MaxChannels]{};
    long double energy=0,error2=0,bias=0;int maximum=0;
    for(uint64_t f=0;f<total;f+=frames) {
        source.fill(c,ideal);const size_t bytes=encoder.encode(c,ideal,bits,sizeof(bits));if(!bytes)return 2;
        for(size_t i=0;i<bytes*8;++i,++at) {
            const bool original=(bits[i/8]>>(i%8))&1;bool value=original;const unsigned ch=unsigned(at%channels);
            if(flip&&random32(rng)%1000000<strength)value=!value;
            if((burst||stuck||erase)&&at>=begin&&at<end) {
                if(burst)value=!value;
                if(stuck)value=false;
                if(erase){value=last[ch];++lost;}
            }else last[ch]=value;
            if(!erase)last[ch]=value;
            if(value!=original)++altered;
            const uint8_t mask=uint8_t(1u<<(i%8));bits[i/8]=uint8_t((bits[i/8]&uint8_t(~mask))|(value?mask:0));
        }
        if(!a::decodeBlock(c,bits,bytes,decoded))return 2;
        pcmWrite(ref,ideal,frames*channels);pcmWrite(out,decoded,frames*channels);
        for(size_t i=0;i<frames*channels;++i) {
            const int d=int(decoded[i])-ideal[i];maximum=std::max(maximum,std::abs(d));error2+=static_cast<long double>(d)*d;energy+=static_cast<long double>(ideal[i])*ideal[i];bias+=d;
        }
    }
    const bool closedRef=std::fclose(ref)==0,closedOut=std::fclose(out)==0;if(!closedRef||!closedOut)return 2;
    const double n=double(total*channels),rms=std::sqrt(double(error2)/n),signal=std::sqrt(double(energy)/n);
    char snr[64];if(error2>0&&energy>0)std::snprintf(snr,sizeof(snr),"%.12g",10*std::log10(double(energy/error2)));else std::strcpy(snr,"null");
    std::printf("{\"schema\":\"offline_audio_model/1\",\"evidence_class\":\"OFFLINE_MODEL\",\"sample_rate_hz\":%u,\"channels\":%u,\"block_frames\":%u,\"oversample\":%u,\"total_pcm_frames\":%llu,\"seed\":%u,\"impairment\":\"%s\",\"strength\":%llu,\"impairment_scope\":\"RAWBITSTREAM_ONLY_no_packet_CRC\",\"encoder\":\"continuous_first_order_sigma_delta_bipolar32768\",\"reconstruction\":\"causal_nonoverlapping_OSR_boxcar_clamp_pcm16\",\"source\":\"integer_phase_triangle_peak24000\",\"source_frequencies_hz\":[",
        c.rate,unsigned(c.channels),unsigned(c.frames),unsigned(c.oversample),(unsigned long long)total,uint32_t(seed),mode,(unsigned long long)strength);
    for(unsigned ch=0;ch<c.channels;++ch)std::printf(ch?",%u":"%u",a::Source::frequency(ch));
    std::printf("],\"bit_count\":%llu,\"altered_bits\":%llu,\"lost_bits\":%llu,\"segment_start_bit\":%llu,\"segment_end_bit_exclusive\":%llu,\"flip_strength_unit\":\"parts_per_million\",\"segment_strength_unit\":\"bits\",\"erase_policy\":\"known_missing_positions_hold_last_bit_per_channel_clock_retained\",\"pcm_samples\":%llu,\"reference_pcm_bytes\":%llu,\"decoded_pcm_bytes\":%llu,\"signal_rms_lsb\":%.12g,\"error_rms_lsb\":%.12g,\"error_bias_lsb\":%.12g,\"max_abs_error_lsb\":%d,\"snr_db\":%s,\"one_way_latency_measured\":false}\n",
        (unsigned long long)count,(unsigned long long)altered,(unsigned long long)lost,(unsigned long long)begin,(unsigned long long)end,(unsigned long long)(total*channels),(unsigned long long)(total*channels*2),(unsigned long long)(total*channels*2),signal,rms,double(bias)/n,maximum,snr);
    return 0;
}
static uint64_t timeFor(const a::Config &c,uint64_t i){return i*c.frames*1000000/c.rate;}
static void packetTests() {
    // Golden CRC independently obtained with Python binascii.crc_hqx(header
    // with zero CRC + payload bytes0..15, 0xffff), the standard CCITT oracle.
    {
        a::Config c{24000,1,8,a::Kind::Pcm16,0};uint8_t block[16],wire[36];
        for(unsigned i=0;i<16;++i)block[i]=uint8_t(i);
        need(a::packetize(c,0x01020304,0x05060708,0,block,wire,sizeof(wire))==36);
        const uint8_t header[]={65,80,1,1,4,3,2,1,8,7,6,5,0,1,8,0,16,0,0xe7,0xc5};
        need(std::memcmp(wire,header,20)==0&&std::memcmp(wire+20,block,16)==0);
    }
    uint32_t rng=23063;
    for(uint32_t rate:{24000u,48000u})for(uint8_t ch:{1,2,4})for(uint8_t frames:{8,16,32})for(unsigned mode=0;mode<3;++mode) {
        a::Config c{rate,ch,frames,mode?a::Kind::Pdm:a::Kind::Pcm16,uint8_t(mode?mode*16:0)};
        need(a::valid(c));int16_t input[a::MaxSamples],output[a::MaxSamples];uint8_t block[a::MaxBlockBytes];
        for(auto &s:input)s=int16_t(int(random32(rng)%65536)-32768);
        a::PdmEncoder e;const size_t bytes=mode?e.encode(c,input,block,sizeof(block)):a::encodePcm(c,input,block,sizeof(block));need(bytes==a::blockBytes(c));
        need(a::decodeBlock(c,block,bytes,output));if(!mode)need(std::memcmp(input,output,size_t(ch)*frames*2)==0);
        a::Receiver receiver;need(receiver.reset(c,0x01020304));uint64_t now=10000;
        for(unsigned fragment=0;fragment<a::fragmentCount(c);++fragment) {
            uint8_t p[a::MaxPacketBytes];const size_t n=a::packetize(c,0x01020304,0xfffffffdu,fragment,block,p,sizeof(p));need(n&&n<=250);
            need(p[0]=='A'&&p[1]=='P'&&p[4]==4&&p[5]==3&&p[6]==2&&p[7]==1);
            a::PacketView view;need(a::parse(p,n,view)==a::ParseResult::Okay);need(view.sequence==0xfffffffdu&&view.fragment==fragment&&a::same(c,view.config));
            need(receiver.receive(p,n,now));need(!receiver.receive(p,n,now));
            for(size_t cut=0;cut<n;++cut)need(a::parse(p,cut,view)!=a::ParseResult::Okay);
            for(size_t bit=0;bit<n*8;++bit){p[bit/8]^=uint8_t(1u<<(bit%8));need(a::parse(p,n,view)!=a::ParseResult::Okay);p[bit/8]^=uint8_t(1u<<(bit%8));}
        }
        need(!receiver.render(receiver.nextDeadlineUs()-1,input));a::OutputInfo info;need(receiver.render(receiver.nextDeadlineUs(),input,&info));need(!info.concealed&&info.sequence==0xfffffffdu);need(std::memcmp(input,output,size_t(ch)*frames*2)==0);
        need(receiver.stats().completed_blocks==1&&receiver.stats().duplicate_packets==a::fragmentCount(c));
        const uint64_t deadline=receiver.nextDeadlineUs();need(receiver.render(deadline,input,&info));need(info.concealed&&receiver.stats().concealed_blocks==1);
        need(receiver.render(deadline+10000000,input,&info));need(receiver.stats().skipped_blocks>8);need(!receiver.render(deadline+10000000,input));
    }
}
static void stateTests() {
    a::Config c{48000,2,16,a::Kind::Pcm16,0};a::Receiver r;need(r.reset(c,77));
    int16_t input[a::MaxSamples],output[a::MaxSamples];for(auto &v:input)v=30000;uint8_t b[a::MaxBlockBytes],p[a::MaxPacketBytes];
    const size_t bytes=a::encodePcm(c,input,b,sizeof(b));need(bytes==64);size_t n=a::packetize(c,78,0,0,b,p,sizeof(p));need(!r.receive(p,n,1000)&&r.stats().wrong_session==1&&!r.hasAnchor());
    a::Config wrong=c;wrong.rate=24000;n=a::packetize(wrong,77,0,0,b,p,sizeof(p));need(!r.receive(p,n,1000)&&r.stats().wrong_config==1&&!r.hasAnchor());
    n=a::packetize(c,77,10,0,b,p,sizeof(p));need(r.receive(p,n,1000));const uint64_t due=r.nextDeadlineUs();
    n=a::packetize(c,77,18,0,b,p,sizeof(p));need(!r.receive(p,n,1000)&&r.stats().future_packets==1);
    need(r.render(due,output));need(output[0]==30000&&output[31]==30000);
    n=a::packetize(c,77,10,0,b,p,sizeof(p));need(!r.receive(p,n,due)&&r.stats().stale_packets==1);
    need(r.render(r.nextDeadlineUs(),output));need(output[0]==30000);
    for(unsigned f=1;f<c.frames;++f)need(output[f*2]<=output[(f-1)*2]&&output[f*2]>=0);
    for(auto &v:input)v=-30000;
    a::encodePcm(c,input,b,sizeof(b));n=a::packetize(c,77,12,0,b,p,sizeof(p));need(r.receive(p,n,r.nextDeadlineUs()-1));
    const int16_t previous=output[30];need(r.render(r.nextDeadlineUs(),output));need(std::abs(int(output[0])-previous)<=2048);for(unsigned f=1;f<c.frames;++f)need(std::abs(int(output[f*2])-output[(f-1)*2])<=2048);
    // A fragmented block is concealed wholly if any fragment is missing.
    c.channels=4;c.frames=32;need(r.reset(c,9));a::encodePcm(c,input,b,sizeof(b));n=a::packetize(c,9,0,0,b,p,sizeof(p));need(r.receive(p,n,0));a::OutputInfo info;need(r.render(r.nextDeadlineUs(),output,&info));need(info.concealed&&r.stats().incomplete_blocks==1);
    // Clean monotonic stream crosses uint32 sequence wrap without stale replay.
    c={24000,1,8,a::Kind::Pcm16,0};need(r.reset(c,4));
    for(unsigned i=0;i<42;++i){const uint64_t now=10000+timeFor(c,i);if(i<40){for(unsigned k=0;k<8;++k)input[k]=int16_t(i*100+k);a::encodePcm(c,input,b,sizeof(b));n=a::packetize(c,4,0xfffffff0u+i,0,b,p,sizeof(p));need(r.receive(p,n,now));}if(i>=2){need(r.render(now,output,&info));need(!info.concealed&&info.sequence==0xfffffff0u+i-2);for(unsigned k=0;k<8;++k)need(output[k]==int16_t((i-2)*100+k));}}
    need(r.stats().rendered_blocks==40&&r.stats().concealed_blocks==0);
}
static void pdmTests() {
    for(uint8_t channels:{1,2,4})for(uint8_t osr:{16,32}) {
        a::Config c{48000,channels,16,a::Kind::Pdm,osr};a::PdmEncoder enc;int64_t error[4]{};int16_t input[a::MaxSamples],decoded[a::MaxSamples];uint8_t bits[a::MaxBlockBytes];
        for(unsigned block=0;block<20;++block) {
            for(unsigned f=0;f<c.frames;++f)for(unsigned ch=0;ch<channels;++ch)input[f*channels+ch]=int16_t(int((block*1973+f*4001+ch*9991)%65536)-32768);
            const size_t n=enc.encode(c,input,bits,sizeof(bits));size_t index=0;
            for(unsigned f=0;f<c.frames;++f)for(unsigned tick=0;tick<osr;++tick)for(unsigned ch=0;ch<channels;++ch,++index) {
                const int64_t desired=error[ch]+input[f*channels+ch];const bool expected=desired>=0;error[ch]=desired-(expected?32768:-32768);
                need(bool((bits[index/8]>>(index%8))&1)==expected);need(error[ch]>=-32768&&error[ch]<=32767);
            }
            need(a::decodeBlock(c,bits,n,decoded));
            for(unsigned f=0;f<c.frames;++f)for(unsigned ch=0;ch<channels;++ch) {
                unsigned ones=0;for(unsigned k=0;k<osr;++k){const size_t i=(size_t(f)*osr+k)*channels+ch;ones+=(bits[i/8]>>(i%8))&1;}
                need(decoded[f*channels+ch]==std::min(32767,(int(ones)*65536/int(osr))-32768));
            }
        }
        a::Source first,seek;first.reset(23063);for(unsigned i=0;i<31;++i)first.fill(c,input);first.fill(c,input);seek.seek(c,23063,31*c.frames);seek.fill(c,decoded);need(std::memcmp(input,decoded,size_t(channels)*c.frames*2)==0);
    }
    // Encoder state continuity is independent of 8- vs 32-frame packet boundaries.
    a::Config large{24000,4,32,a::Kind::Pdm,32},small=large;small.frames=8;a::Source source;source.reset(1);int16_t samples[a::MaxSamples];source.fill(large,samples);a::PdmEncoder e1,e2;uint8_t one[512],parts[512];need(e1.encode(large,samples,one,sizeof(one))==512);
    for(unsigned i=0;i<4;++i)need(e2.encode(small,samples+i*8*4,parts+i*128,128)==128);
    need(std::memcmp(one,parts,512)==0);
}
int main(int argc,char **argv) {
    if(argc>1)return std::strcmp(argv[1],"--simulate")?2:simulate(argc,argv);
    packetTests();stateTests();pdmTests();
    std::printf("{\"schema\":1,\"type\":\"host_audio_core_validation\",\"evidence_class\":\"HOST_CORRECTNESS_ONLY\",\"checks\":%llu,\"receiver_bytes\":%zu,\"encoder_bytes\":%zu,\"source_bytes\":%zu,\"wire_header_bytes\":%zu,\"max_packet_bytes\":%zu,\"valid\":true}\n",checks,sizeof(a::Receiver),sizeof(a::PdmEncoder),sizeof(a::Source),a::HeaderBytes,a::MaxPacketBytes);
}

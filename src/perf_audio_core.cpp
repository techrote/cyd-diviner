#if defined(GRIM_PERF_AUDIO_MVP) || defined(GRIM_PERF_AUDIO_CORE_STANDALONE)
#include "perf_audio_core.h"
#include <algorithm>
#include <cstring>

namespace perf_audio_core {
namespace {
uint32_t get32(const uint8_t *p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8)); }
uint16_t crc(const uint8_t *p,size_t size) {
    uint16_t c=0xffff;
    for(size_t i=0;i<size;++i) {
        c=uint16_t(c^(uint16_t(i==18||i==19 ? 0:p[i])<<8));
        for(unsigned b=0;b<8;++b)c=uint16_t((c&0x8000)?(uint32_t(c)<<1)^0x1021u:uint32_t(c)<<1);
    }
    return c;
}
size_t frameBytes(const Config &c) { return c.kind==Kind::Pcm16 ? 2*c.channels:size_t(c.channels)*c.oversample/8; }
size_t fragmentSpan(const Config &c) { const size_t f=frameBytes(c);return f ? MaxPayloadBytes/f*f:0; }
int64_t difference(uint32_t a,uint32_t b) {
    const uint32_t d=a-b; return d<0x80000000u ? int64_t(d):-int64_t(0x100000000ull-d);
}
int16_t signed16(unsigned bits) { return int16_t(bits<32768 ? int(bits):int(bits)-65536); }
int16_t slew(int16_t from,int16_t to,int limit) {
    const int delta=int(to)-from;return int16_t(int(from)+std::max(-limit,std::min(limit,delta)));
}
uint64_t offset(const Config &c,uint64_t index) {
    const uint64_t numerator=uint64_t(c.frames)*1000000;
    return (index/c.rate)*numerator+((index%c.rate)*numerator)/c.rate;
}
}
bool valid(const Config &c) {
    return (c.rate==24000||c.rate==48000)&&(c.channels==1||c.channels==2||c.channels==4)&&
           (c.frames==8||c.frames==16||c.frames==32)&&
           ((c.kind==Kind::Pcm16&&c.oversample==0)||(c.kind==Kind::Pdm&&(c.oversample==16||c.oversample==32)));
}
bool same(const Config &a,const Config &b) { return a.rate==b.rate&&a.channels==b.channels&&a.frames==b.frames&&a.kind==b.kind&&a.oversample==b.oversample; }
size_t blockBytes(const Config &c) { return valid(c)?frameBytes(c)*c.frames:0; }
size_t fragmentCount(const Config &c) { const size_t n=blockBytes(c),span=valid(c)?fragmentSpan(c):0;return span?(n+span-1)/span:0; }
size_t packetize(const Config &c,uint32_t session,uint32_t sequence,unsigned fragment,const uint8_t *block,uint8_t *out,size_t capacity) {
    if(!valid(c)||!block||!out||fragment>=fragmentCount(c))return 0;
    const size_t start=fragment*fragmentSpan(c),n=std::min(fragmentSpan(c),blockBytes(c)-start);
    if(capacity<HeaderBytes+n)return 0;
    out[0]='A';out[1]='P';out[2]=1;out[3]=uint8_t(c.kind);put32(out+4,session);put32(out+8,sequence);
    out[12]=uint8_t(c.rate==48000);out[13]=c.channels;out[14]=c.frames;out[15]=uint8_t(fragment);out[16]=uint8_t(n);out[17]=c.oversample;out[18]=out[19]=0;
    std::memcpy(out+HeaderBytes,block+start,n);const uint16_t sum=crc(out,HeaderBytes+n);out[18]=uint8_t(sum);out[19]=uint8_t(sum>>8);return HeaderBytes+n;
}
ParseResult parse(const uint8_t *p,size_t size,PacketView &v) {
    v=PacketView{};
    if(!p||size<HeaderBytes||size>MaxPacketBytes||p[0]!='A'||p[1]!='P'||p[2]!=1||p[12]>1)return ParseResult::Invalid;
    Config c{p[12]?48000u:24000u,p[13],p[14],Kind(p[3]),p[17]};
    if(!valid(c)||p[15]>=fragmentCount(c))return ParseResult::Invalid;
    const size_t start=size_t(p[15])*fragmentSpan(c),n=std::min(fragmentSpan(c),blockBytes(c)-start);
    if(p[16]!=n||size!=HeaderBytes+n)return ParseResult::Invalid;
    if(crc(p,size)!=(unsigned(p[18])|(unsigned(p[19])<<8)))return ParseResult::BadCrc;
    v.config=c;v.session=get32(p+4);v.sequence=get32(p+8);v.fragment=p[15];v.fragments=uint8_t(fragmentCount(c));v.offset=start;v.length=n;v.payload=p+HeaderBytes;
    return ParseResult::Okay;
}
size_t encodePcm(const Config &c,const int16_t *in,uint8_t *out,size_t capacity) {
    const size_t n=blockBytes(c);if(c.kind!=Kind::Pcm16||!n||!in||!out||capacity<n)return 0;
    for(size_t i=0;i<size_t(c.frames)*c.channels;++i) {const uint16_t v=uint16_t(in[i]);out[2*i]=uint8_t(v);out[2*i+1]=uint8_t(v>>8);}return n;
}
void PdmEncoder::reset() { std::memset(error,0,sizeof(error)); }
size_t PdmEncoder::encode(const Config &c,const int16_t *in,uint8_t *out,size_t capacity) {
    const size_t n=blockBytes(c);if(c.kind!=Kind::Pdm||!n||!in||!out||capacity<n)return 0;
    std::memset(out,0,n);size_t bit=0;
    for(unsigned f=0;f<c.frames;++f)for(unsigned tick=0;tick<c.oversample;++tick)for(unsigned ch=0;ch<c.channels;++ch,++bit) {
        error[ch]+=in[f*c.channels+ch];const bool one=error[ch]>=0;error[ch]+=one?-32768:32768;
        if(one)out[bit/8]|=uint8_t(1u<<(bit%8));
    }return n;
}
bool decodeBlock(const Config &c,const uint8_t *in,size_t bytes,int16_t *out) {
    if(!valid(c)||!in||!out||bytes!=blockBytes(c))return false;
    if(c.kind==Kind::Pcm16) {
        for(size_t i=0;i<size_t(c.frames)*c.channels;++i)out[i]=signed16(unsigned(in[2*i])|(unsigned(in[2*i+1])<<8));
    }else for(unsigned f=0;f<c.frames;++f)for(unsigned ch=0;ch<c.channels;++ch) {
        unsigned ones=0;
        for(unsigned tick=0;tick<c.oversample;++tick) {const size_t b=(size_t(f)*c.oversample+tick)*c.channels+ch;ones+=(in[b/8]>>(b%8))&1;}
        const int value=(2*int(ones)-c.oversample)*32768/int(c.oversample);out[f*c.channels+ch]=int16_t(std::min(32767,value));
    }return true;
}
void Source::reset(uint32_t seed) { for(unsigned ch=0;ch<MaxChannels;++ch)phase[ch]=seed^(0x9e3779b9u*ch); }
void Source::skip(const Config &c,uint64_t frames) {
    if(!valid(c))return;
    for(unsigned ch=0;ch<c.channels;++ch) {const uint32_t increment=uint32_t((uint64_t(frequency(ch))<<32)/c.rate);phase[ch]+=uint32_t(uint64_t(increment)*uint32_t(frames));}
}
void Source::seek(const Config &c,uint32_t seed,uint64_t index) { reset(seed);skip(c,index); }
void Source::fill(const Config &c,int16_t *out) {
    if(!valid(c)||!out)return;
    for(unsigned f=0;f<c.frames;++f)for(unsigned ch=0;ch<c.channels;++ch) {
        const int p=int(phase[ch]>>16),triangle=p<32768?-32768+2*p:98303-2*p;
        out[f*c.channels+ch]=int16_t(triangle*24000/32768);
        phase[ch]+=uint32_t((uint64_t(frequency(ch))<<32)/c.rate);
    }
}
uint64_t Receiver::due(uint64_t index) const { return anchor_us+offset(config,index); }
void Receiver::clearSlot(Slot &s,uint64_t now) {
    if(s.used&&now>=s.first_local_us)counters.max_slot_dwell_us=std::max(counters.max_slot_dwell_us,now-s.first_local_us);
    s.used=false;s.fragments=0;
}
bool Receiver::reset(const Config &c,uint32_t nextSession,uint8_t delay) {
    configured=false;if(!valid(c)||delay==0||delay>Slots-1)return false;
    config=c;session=nextSession;delay_blocks=delay;anchored=false;was_concealed=false;anchor_us=next_index=0;base_sequence=0;
    for(auto &s:slots) {s.used=false;s.fragments=0;s.sequence=0;s.first_local_us=0;}
    std::memset(previous,0,sizeof(previous));std::memset(recovering,0,sizeof(recovering));counters=Stats{};configured=true;return true;
}
bool Receiver::receive(const uint8_t *p,size_t size,uint64_t now) {
    PacketView v;const auto parsed=parse(p,size,v);
    if(!configured||parsed==ParseResult::Invalid) {++counters.invalid_packets;return false;}
    if(parsed==ParseResult::BadCrc) {++counters.crc_errors;return false;}
    if(v.session!=session) {++counters.wrong_session;return false;}
    if(!same(v.config,config)) {++counters.wrong_config;return false;}
    if(!anchored) {base_sequence=v.sequence;anchor_us=now+offset(config,delay_blocks);next_index=0;anchored=true;}
    const uint32_t expected=base_sequence+uint32_t(next_index);const int64_t delta=difference(v.sequence,expected);
    if(delta<0) {++counters.stale_packets;return false;}
    if(delta>=int64_t(Slots)) {++counters.future_packets;return false;}
    if(now>=due(next_index+uint64_t(delta))) {++counters.stale_packets;return false;}
    Slot &s=slots[v.sequence%Slots];
    if(s.used&&s.sequence!=v.sequence)clearSlot(s,now);
    if(!s.used) {s.used=true;s.sequence=v.sequence;s.first_local_us=now;s.fragments=0;}
    const uint8_t bit=uint8_t(1u<<v.fragment);
    if(s.fragments&bit) {++counters.duplicate_packets;return false;}
    std::memcpy(s.data+v.offset,v.payload,v.length);s.fragments|=bit;++counters.accepted_packets;
    if(s.fragments==uint8_t((1u<<v.fragments)-1))++counters.completed_blocks;
    return true;
}
bool Receiver::render(uint64_t now,int16_t *out,OutputInfo *info) {
    if(!configured||!anchored||!out||now<due(next_index))return false;
    counters.max_render_lateness_us=std::max(counters.max_render_lateness_us,now-due(next_index));
    const uint64_t period=(uint64_t(config.frames)*1000000+config.rate-1)/config.rate;
    if(now-due(next_index)>2*period) {
        const uint64_t delta=now-anchor_us,numerator=uint64_t(config.frames)*1000000;
        uint64_t current=(delta/numerator)*config.rate+((delta%numerator)*config.rate)/numerator;
        if(due(current+1)<=now)++current; // floor deadline rounding can admit one extra index.
        if(current>next_index) {counters.skipped_blocks+=uint32_t(current-next_index);next_index=current;}
        const uint32_t expected=base_sequence+uint32_t(next_index);
        for(auto &s:slots)if(s.used&&(difference(s.sequence,expected)<0||difference(s.sequence,expected)>=int64_t(Slots)))clearSlot(s,now);
    }
    const uint64_t scheduled=due(next_index),late=now-scheduled;
    if(late)++counters.late_playout_blocks;
    const uint32_t sequence=base_sequence+uint32_t(next_index);Slot &s=slots[sequence%Slots];
    const bool found=s.used&&s.sequence==sequence,complete=found&&s.fragments==uint8_t((1u<<fragmentCount(config))-1);
    int16_t decoded[MaxSamples];const bool good=complete&&decodeBlock(config,s.data,blockBytes(config),decoded);
    const uint64_t dwell=found&&now>=s.first_local_us?now-s.first_local_us:0;
    if(!good) {++counters.concealed_blocks;counters.concealed_frames+=config.frames;if(found)++counters.incomplete_blocks;}
    for(unsigned f=0;f<config.frames;++f)for(unsigned ch=0;ch<config.channels;++ch) {
        if(good) {
            const int16_t target=decoded[f*config.channels+ch];
            previous[ch]=recovering[ch]?slew(previous[ch],target,2048):target;
            if(previous[ch]==target)recovering[ch]=false;
            out[f*config.channels+ch]=previous[ch];
        } else {
            // State always equals the last emitted sample. This preserves the
            // recovery slew bound across the missing-to-valid block boundary.
            if(f||was_concealed)previous[ch]=slew(previous[ch],0,256);
            recovering[ch]=true;out[f*config.channels+ch]=previous[ch];
        }
    }
    was_concealed=!good;
    if(found)clearSlot(s,now);
    if(info) {*info=OutputInfo{};info->sequence=sequence;info->concealed=!good;info->scheduled_local_us=scheduled;info->lateness_us=late;info->slot_dwell_us=dwell;}
    ++next_index;++counters.rendered_blocks;return true;
}
}
#endif

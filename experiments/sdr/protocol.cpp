#include "protocol.h"
#include <algorithm>
#include <cstring>
#ifndef SDR_PROTOCOL_TRANSMIT_ONLY
#include <cmath>
#include <vector>
#endif
namespace sdr_protocol {
namespace {
constexpr uint32_t Rates[]={250,500,1000,2000,4000,8000,16000,24000,48000};
unsigned rateCode(uint32_t rate) {for(unsigned i=0;i<9;++i)if(Rates[i]==rate)return i;return 15;}
unsigned channelCode(unsigned channels) {return channels==1?0:channels==2?1:channels==4?2:3;}
unsigned osrCode(unsigned osr) {return osr==4?0:osr==8?1:osr==16?2:osr==32?3:4;}
}
bool valid(const Config &c) {return rateCode(c.source_rate_hz)<9&&channelCode(c.channels)<3&&osrCode(c.oversample)<4&&(c.payload_bits==128||c.payload_bits==256);}
size_t audioFrames(const Config &c) {return valid(c)?c.payload_bits/(unsigned(c.channels)*c.oversample):0;}
size_t frameSymbols(const Config &c) {return valid(c)?TrainingSymbols+SyncSymbols+HeaderBytes*8+c.payload_bits:0;}
double requiredBaud(const Config &c) {return valid(c)?double(c.source_rate_hz)*c.channels*c.oversample*frameSymbols(c)/c.payload_bits:0;}
uint8_t headerCrc8(const uint8_t *bytes,size_t n) {
    uint8_t crc=0;for(size_t i=0;i<n;++i){crc=uint8_t(crc^bytes[i]);for(unsigned j=0;j<8;++j)crc=uint8_t((crc&128)?(unsigned(crc)<<1)^7:unsigned(crc)<<1);}return crc;
}
size_t buildFrame(const Config &c,uint16_t sequence,const uint8_t *payload,uint8_t *symbols,size_t capacity) {
    const size_t n=frameSymbols(c);if(!n||!payload||!symbols||capacity<n)return 0;
    const unsigned cfg=channelCode(c.channels)|(osrCode(c.oversample)<<2)|(rateCode(c.source_rate_hz)<<4)|(unsigned(c.payload_bits==256)<<8);
    uint8_t header[]={uint8_t(sequence),uint8_t(sequence>>8),c.stream_id,uint8_t(cfg),uint8_t(cfg>>8),0};header[5]=headerCrc8(header,5);
    size_t p=0;for(unsigned i=0;i<TrainingSymbols;++i)symbols[p++]=uint8_t(i%2);
    for(unsigned i=0;i<32;++i)symbols[p++]=uint8_t((SyncWord>>(31-i))&1);
    for(uint8_t byte:header)for(unsigned b=0;b<8;++b)symbols[p++]=uint8_t((byte>>(7-b))&1);
    for(unsigned b=0;b<c.payload_bits;++b)symbols[p++]=uint8_t((payload[b/8]>>(b%8))&1);
    return p;
}
bool decodeHeader(const uint8_t *h,Config &c,uint16_t &sequence) {
    if(!h||headerCrc8(h,5)!=h[5])return false;
    const unsigned cfg=unsigned(h[3])|(unsigned(h[4])<<8),cc=cfg&3,oc=(cfg>>2)&3,rc=(cfg>>4)&15;
    if((cfg&0xfe00)||cc>=3||rc>=9)return false;
    c.source_rate_hz=Rates[rc];c.channels=uint8_t(1u<<cc);c.oversample=uint8_t(4u<<oc);c.payload_bits=uint16_t((cfg&256)?256:128);c.stream_id=h[2];sequence=uint16_t(unsigned(h[0])|(unsigned(h[1])<<8));return true;
}
void PdmSource::reset(uint32_t seed) {for(unsigned ch=0;ch<MaxChannels;++ch){phase[ch]=seed^(0x9e3779b9u*ch);error[ch]=0;}}
bool PdmSource::payload(const Config &c,uint8_t *out,size_t bytes,int16_t *reference) {
    if(!valid(c)||!out||bytes<c.payload_bits/8)return false;
    std::memset(out,0,c.payload_bits/8);size_t bit=0;
    for(unsigned f=0;f<audioFrames(c);++f) {
        int16_t sample[MaxChannels];
        for(unsigned ch=0;ch<c.channels;++ch) {
            const int p=int(phase[ch]>>16),triangle=p<32768?-32768+2*p:98303-2*p;sample[ch]=int16_t(triangle*24000/32768);
            phase[ch]+=uint32_t((uint64_t(frequency(ch))<<32)/c.source_rate_hz);
            if(reference)reference[f*c.channels+ch]=sample[ch];
        }
        for(unsigned tick=0;tick<c.oversample;++tick)for(unsigned ch=0;ch<c.channels;++ch,++bit) {
            error[ch]+=sample[ch];const bool one=error[ch]>=0;error[ch]+=one?-32768:32768;if(one)out[bit/8]|=uint8_t(1u<<(bit%8));
        }
    }return true;
}
bool reconstruct(const Config &c,const uint8_t *bits,int16_t *pcm,size_t capacity) {
    if(!valid(c)||!bits||!pcm||capacity<audioFrames(c)*c.channels)return false;
    for(unsigned f=0;f<audioFrames(c);++f)for(unsigned ch=0;ch<c.channels;++ch) {
        unsigned ones=0;for(unsigned tick=0;tick<c.oversample;++tick){const unsigned b=(f*c.oversample+tick)*c.channels+ch;ones+=(bits[b/8]>>(b%8))&1;}
        const int value=(2*int(ones)-c.oversample)*32768/c.oversample;pcm[f*c.channels+ch]=int16_t(std::min(32767,value));
    }return true;
}
#ifndef SDR_PROTOCOL_TRANSMIT_ONLY
namespace {
struct Search {
    const std::vector<double> &sum;
    const ReceiveOptions &options;
    DecodeStats &stats;
    double mean(double start,double period,unsigned symbol) const {
        const size_t a=size_t(start+period*symbol),b=size_t(start+period*(symbol+1));
        return b>a&&b<sum.size()?(sum[b]-sum[a])/double(b-a):0;
    }
    bool candidate(double start,double period,Frame &frame,double &threshold,double &contrast) {
        if(start+period*(TrainingSymbols+SyncSymbols+HeaderBytes*8)>=sum.size())return false;
        double low=0,high=0;
        for(unsigned b=0;b<TrainingSymbols;++b)(b%2?high:low)+=mean(start,period,b);
        low/=TrainingSymbols/2;high/=TrainingSymbols/2;contrast=high-low;
        if(!std::isfinite(contrast)||contrast<=1e-9)return false;
        threshold=(low+high)/2;unsigned good=0;double fit=0;
        for(unsigned b=0;b<TrainingSymbols;++b) {const double v=mean(start,period,b);good+=unsigned((v>threshold)==bool(b%2));const double e=(v-(b%2?high:low))/contrast;fit+=e*e;}
        if(good<30||fit/TrainingSymbols>.13)return false;
        ++stats.training_candidates;unsigned syncErrors=0;
        for(unsigned b=0;b<32;++b) {
            const bool expected=(SyncWord>>(31-b))&1;const double v=mean(start,period,TrainingSymbols+b);syncErrors+=unsigned((v>threshold)!=expected);const double e=(v-(expected?high:low))/contrast;fit+=e*e;
        }
        if(syncErrors>1)return false;
        ++stats.sync_candidates;
        uint8_t header[HeaderBytes]{};
        for(unsigned b=0;b<HeaderBytes*8;++b) {const double v=mean(start,period,TrainingSymbols+SyncSymbols+b);header[b/8]|=uint8_t(unsigned(v>threshold)<<(7-b%8));}
        if(headerCrc8(header,5)!=header[5]){++stats.header_crc_rejects;return false;}
        if(!decodeHeader(header,frame.config,frame.sequence)){++stats.header_config_rejects;return false;}
        if(options.filter_stream_id&&frame.config.stream_id!=options.stream_id){++stats.wrong_stream;return false;}
        const size_t symbols=frameSymbols(frame.config);
        if(start+period*symbols>=sum.size())return false;
        for(unsigned b=0;b<HeaderBytes*8;++b) {const bool expected=(header[b/8]>>(7-b%8))&1;const double v=mean(start,period,TrainingSymbols+SyncSymbols+b);const double e=(v-(expected?high:low))/contrast;fit+=e*e;}
        frame.training_fit_error=fit/(TrainingSymbols+SyncSymbols+HeaderBytes*8);frame.first_sample=uint64_t(start);frame.recovered_samples_per_symbol=period;return true;
    }
    void payload(double start,double period,double threshold,double contrast,Frame &f) const {
        std::memset(f.payload,0,sizeof(f.payload));f.payload_confidence_mean=0;f.low_confidence_payload_bits=0;
        for(unsigned bit=0;bit<f.config.payload_bits;++bit) {
            const double value=mean(start,period,TrainingSymbols+SyncSymbols+HeaderBytes*8+bit),confidence=std::abs(value-threshold)/contrast;
            if(value>threshold)f.payload[bit/8]|=uint8_t(1u<<(bit%8));
            f.payload_confidence_mean+=confidence;if(confidence<.15)++f.low_confidence_payload_bits;
        }f.payload_confidence_mean/=f.config.payload_bits;
    }
};
}
bool demodulate(const IQ *iq,size_t count,const ReceiveOptions &o,FrameSink sink,void *context,DecodeStats &stats) {
    stats=DecodeStats{};stats.input_samples=count;
    if(!iq||!count||count>10000000||!sink||!std::isfinite(o.sample_rate_hz)||!std::isfinite(o.symbol_rate)||o.symbol_rate<=0||o.sample_rate_hz/o.symbol_rate<8||o.sample_rate_hz/o.symbol_rate>1024||!std::isfinite(o.clock_search_ppm)||o.clock_search_ppm<0||o.clock_search_ppm>20000||!std::isfinite(o.clock_step_ppm)||o.clock_step_ppm<=0||o.clock_step_ppm>20000||2*o.clock_search_ppm/o.clock_step_ppm>128)return false;
    std::vector<double> sum(count+1,0);
    for(size_t i=0;i<count;++i) {
        if(!std::isfinite(iq[i].i)||!std::isfinite(iq[i].q))return false;
        double metric=double(iq[i].i)*iq[i].i+double(iq[i].q)*iq[i].q;
        if(o.modulation==Modulation::Fsk2) {
            if(!i)metric=0;
            else metric=std::atan2(double(iq[i].q)*iq[i-1].i-double(iq[i].i)*iq[i-1].q,double(iq[i].i)*iq[i-1].i+double(iq[i].q)*iq[i-1].q);
        }sum[i+1]=sum[i]+metric;
    }
    const double nominal=o.sample_rate_hz/o.symbol_rate,scanStep=std::max(1.,std::floor(nominal/4));Search search{sum,o,stats};
    bool previous=false;uint16_t last=0;
    for(double position=0;position+nominal*(TrainingSymbols+SyncSymbols+HeaderBytes*8)<count;position+=scanStep) {
        bool found=false;Frame best;double bestStart=0,bestPeriod=0,bestThreshold=0,bestContrast=0;
        auto consider=[&](double start) {
            const unsigned intervals=unsigned(std::ceil(2*o.clock_search_ppm/o.clock_step_ppm));
            for(unsigned grid=0;grid<=intervals;++grid) {
                const double ppm=-o.clock_search_ppm+std::min(2*o.clock_search_ppm,grid*o.clock_step_ppm);
                const double period=nominal*(1+ppm*1e-6);Frame f;double threshold=0,contrast=0;
                if(search.candidate(start,period,f,threshold,contrast)&&(!found||f.training_fit_error<best.training_fit_error)) {found=true;best=f;bestStart=start;bestPeriod=period;bestThreshold=threshold;bestContrast=contrast;}
            }
        };
        consider(position);
        if(!found)continue;
        // Refine sample phase after finding a CRC-valid header; no payload CRC
        // or known payload is used to choose the clock or accept the frame.
        for(double p=std::max(0.,position-scanStep);p<=position+nominal/2;p+=1)consider(p);
        search.payload(bestStart,bestPeriod,bestThreshold,bestContrast,best);
        if(previous){const uint16_t delta=uint16_t(best.sequence-last);if(!delta)++stats.duplicates;else if(delta>1&&delta<32768)stats.sequence_gaps+=delta-1;}
        previous=true;last=best.sequence;++stats.frames;stats.payload_bits+=best.config.payload_bits;sink(best,context);
        position=std::max(position,bestStart+bestPeriod*frameSymbols(best.config)-nominal/3-scanStep);
    }return true;
}
#endif
}

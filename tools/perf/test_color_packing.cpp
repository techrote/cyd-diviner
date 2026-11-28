// Host correctness oracle, never a target performance measurement.
// g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
//     -fno-omit-frame-pointer -I src tools/perf/test_color_packing.cpp -o packing-test
#include "perf_color_packing.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
using perf_color_packing::Format;
namespace pc = perf_color_packing;
static unsigned long long checks = 0;
static void require(bool okay) { ++checks; if (!okay) { std::fprintf(stderr,"packing assertion %llu failed\n",checks); std::abort(); } }
// Independent reference maps one bit at a time; it never loads adjacent bytes.
static unsigned referenceRead(const uint8_t *row, size_t x, unsigned bits) {
    unsigned value = 0;
    for (unsigned j=0;j<bits;++j) {
        const size_t k=x*bits+j;
        value += unsigned((row[k/8] / (1u << (k%8))) % 2) * (1u << j);
    }
    return value;
}
static void referenceWrite(uint8_t *row,size_t x,unsigned bits,unsigned value) {
    for(unsigned j=0;j<bits;++j) {
        const size_t k=x*bits+j; const uint8_t mask=uint8_t(1u << (k%8));
        row[k/8]=uint8_t((row[k/8] & uint8_t(~mask)) | (((value/(1u<<j))%2) ? mask : 0));
    }
}
static uint16_t referenceEncode(Format f,unsigned rgb,bool a) {
    const unsigned r=rgb/2048,g=(rgb/32)%64,b=rgb%32;
    if(f==Format::Rgb111)return uint16_t((r/16)*4+(g/32)*2+b/16);
    if(f==Format::Rgb444)return uint16_t((r/2)*256+(g/4)*16+b/2);
    return uint16_t(r*2048+(g/2)*64+b*2+unsigned(a));
}
static uint16_t referenceExpand(Format f,unsigned v) {
    unsigned r,g,b;
    if(f==Format::Rgb111) {r=((v/4)%2)*31;g=((v/2)%2)*63;b=(v%2)*31;}
    else if(f==Format::Rgb444) {
        r=(v/256)%16;g=(v/16)%16;b=v%16;
        // Independent channel bit lists define exact bit replication.
        const unsigned rBits[]={r/8%2,r/4%2,r/2%2,r%2,r/8%2};
        const unsigned gBits[]={g/8%2,g/4%2,g/2%2,g%2,g/8%2,g/4%2};
        const unsigned bBits[]={b/8%2,b/4%2,b/2%2,b%2,b/8%2};
        r=g=b=0;
        for(unsigned bit:rBits)r=r*2+bit;
        for(unsigned bit:gBits)g=g*2+bit;
        for(unsigned bit:bBits)b=b*2+bit;
    } else {r=v/2048;g=(v/64)%32;g=g*2+g/16;b=(v/2)%32;}
    return uint16_t(r*2048+g*32+b);
}
int main() {
    // Fixed byte vectors independently pin compatibility with existing packed
    // RGB444 history and the documented LSB-first 3/16-bit representations.
    uint8_t known444[3]{};
    pc::write(Format::Rgb444,known444,0,0xabc);pc::write(Format::Rgb444,known444,1,0x123);
    require(known444[0]==0xbc && known444[1]==0x3a && known444[2]==0x12);
    uint8_t known111[3]{};
    for(unsigned i=0;i<8;++i)pc::write(Format::Rgb111,known111,i,uint16_t(i));
    require(known111[0]==0x88 && known111[1]==0xc6 && known111[2]==0xfa);
    uint8_t known5551[2]{};
    pc::write(Format::Rgba5551,known5551,0,pc::encode565(Format::Rgba5551,0xf800,true));
    require(known5551[0]==1 && known5551[1]==0xf8);
    const std::array<Format,3> formats={Format::Rgb111,Format::Rgb444,Format::Rgba5551};
    for(Format f:formats) {
        const unsigned bits=pc::bitsPerPixel(f),mask=(1u<<bits)-1;
        for(unsigned rgb=0;rgb<65536;++rgb)for(bool a:{false,true}) {
            const auto native=pc::encode565(f,uint16_t(rgb),a);
            require(native==referenceEncode(f,rgb,a));
            require(pc::expand565(f,native)==referenceExpand(f,native));
            require(pc::opaque(f,native)==(f!=Format::Rgba5551 || a));
        }
        for(unsigned value=0;value<=mask;++value) {
            const uint16_t expanded=pc::expand565(f,uint16_t(value));
            require(expanded==referenceExpand(f,value));
            require(pc::encode565(f,expanded,pc::opaque(f,uint16_t(value)))==value);
        }
        for(size_t width:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(7),size_t(8),size_t(9),size_t(17),size_t(31),size_t(319),size_t(320),size_t(321)}) {
            const size_t bytes=(width*bits+7)/8;
            require(pc::rowBytes(f,width)==bytes);
            // Exact allocation catches even read-only end overreads under ASan.
            std::unique_ptr<uint8_t[]> exact(bytes ? new uint8_t[bytes] : nullptr);
            std::vector<uint8_t> guarded(bytes+2,0xa5),reference(bytes,0xa5);
            if(bytes)std::memset(exact.get(),0xa5,bytes);
            for(unsigned pass=0;pass<4;++pass)for(size_t index=0;index<width;++index) {
                const size_t x=(pass%2) ? width-1-index:index;
                const unsigned value=(unsigned(x)*4051u+pass*9127u+65535u)&65535u;
                referenceWrite(reference.data(),x,bits,value);
                pc::write(f,exact.get(),x,uint16_t(value));pc::write(f,guarded.data()+1,x,uint16_t(value));
                require(pc::read(f,exact.get(),x)==(value&mask));
                require(pc::read(f,guarded.data()+1,x)==referenceRead(reference.data(),x,bits));
                require(std::memcmp(exact.get(),reference.data(),bytes)==0);
                require(std::memcmp(guarded.data()+1,reference.data(),bytes)==0);
                require(guarded.front()==0xa5 && guarded.back()==0xa5);
                for(size_t other=0;other<width;++other)require(pc::read(f,exact.get(),other)==referenceRead(reference.data(),other,bits));
            }
        }
    }
    for(size_t width=0;width<=321;++width) {
        const size_t bytes=(width+7)/8; require(pc::coverageRowBytes(width)==bytes);
        std::unique_ptr<uint8_t[]> exact(bytes ? new uint8_t[bytes] : nullptr);
        std::vector<uint8_t> reference(bytes,0xa5);if(bytes)std::memset(exact.get(),0xa5,bytes);
        for(size_t x=0;x<width;++x)for(bool a:{true,false,true}) {
            pc::writeCoverage(exact.get(),x,a);referenceWrite(reference.data(),x,1,unsigned(a));
            require(pc::readCoverage(exact.get(),x)==a);
            require(std::memcmp(exact.get(),reference.data(),bytes)==0);
        }
    }
    // Alpha=0 must preserve any background, including when RGB bits are nonzero.
    for(unsigned n=0;n<65536;++n) {
        const uint16_t background=uint16_t(n*17u);
        const uint16_t actual=pc::opaque(Format::Rgba5551,uint16_t(n)) ? pc::expand565(Format::Rgba5551,uint16_t(n)):background;
        require(actual==(n%2 ? referenceExpand(Format::Rgba5551,n):background));
    }
    require(pc::encode565(Format::Rgba5551,0,true)==1);
    require(pc::encode565(Format::Rgba5551,0,false)==0);
    require(pc::rowBytes(Format::Rgb111,320)==120 && pc::rowBytes(Format::Rgb444,320)==480 && pc::rowBytes(Format::Rgba5551,320)==640);
    require(pc::rowBytes(Format::Rgb111,240)*116==10440 && pc::rowBytes(Format::Rgb444,240)*116==41760 && pc::rowBytes(Format::Rgba5551,240)*116==55680);
    require(pc::rowBytes(Format::Rgba5551,std::numeric_limits<size_t>::max())==0);
    std::printf("{\"schema\":1,\"type\":\"host_color_packing_validation\",\"evidence_class\":\"host_correctness_only\",\"checks\":%llu,\"formats\":3,\"native_words_exhaustive\":true,\"rgb565_inputs_exhaustive\":true,\"edge_tail_neighbor_guards\":true,\"valid\":true}\n",checks);
}

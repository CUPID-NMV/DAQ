// rtp_producer_multistream.cpp
// 10 stream @1kHz + 10 stream @5kHz
// SSRC distinti, CSRC[0] coerente col repo cupid_digi_io

#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>

static uint32_t be32(uint32_t x){ return htonl(x); }
static uint16_t be16(uint16_t x){ return htons(x); }

constexpr double FS_VOLT = 5.0;
constexpr uint32_t ADC_BITS = 24;
constexpr double ADC_FS = (1u<<ADC_BITS)-1;
constexpr double ADC_MID = ADC_FS/2;

struct Channel {
    uint32_t ssrc;
    uint32_t csrc0;
    double sample_rate;
    uint64_t sample_index = 0;

    bool event_active=false;
    double event_t0_s=0;
    double last_event_start_s=-1e9;
    double A0=0;
    double decay_hz=0;
};

int main(){
    const char* dst_ip="127.0.0.1";
    int dst_port=5004;

    int fd=socket(AF_INET,SOCK_DGRAM,0);
    sockaddr_in dst{};
    dst.sin_family=AF_INET;
    dst.sin_port=htons(dst_port);
    inet_pton(AF_INET,dst_ip,&dst.sin_addr);

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> uni01(0,1);
    std::uniform_real_distribution<double> noiseU(-0.01*(ADC_FS/2),0.01*(ADC_FS/2));
    std::uniform_real_distribution<double> A0U(0.5*(ADC_FS/2),0.9*(ADC_FS/2));
    std::uniform_real_distribution<double> decayU(1.0,5.0);

    std::vector<Channel> chans;

    // 10 stream @1kHz
    for(int i=0;i<10;i++){
        chans.push_back({
            uint32_t(0x100+i),
            3000,
            1000.0
        });
    }

    // 10 stream @5kHz
    for(int i=0;i<10;i++){
        chans.push_back({
            uint32_t(0x200+i),
            600,
            5000.0
        });
    }

    constexpr int RTP_HDR=16;
    constexpr int SPP=64;           // samples per packet
    constexpr uint32_t TS_STEP=360;

    std::vector<uint8_t> buf(RTP_HDR + (1+SPP)*4);

    uint16_t seq=0;
    uint32_t ts=0;

    const auto tick_period=std::chrono::microseconds(200);

    while(true){
        for(auto& ch: chans){
            // decide se questo canale genera a questo tick
            if(ch.sample_rate==1000.0 && (ch.sample_index%5)!=0){
                ch.sample_index++;
                continue;
            }

            // RTP header
            buf[0]=(2<<6)|1;
            buf[1]=96;
            *(uint16_t*)&buf[2]=be16(seq++);
            *(uint32_t*)&buf[4]=be32(ts);
            *(uint32_t*)&buf[8]=be32(ch.ssrc);
            *(uint32_t*)&buf[12]=be32(ch.csrc0);

            *(uint32_t*)&buf[16]=0; // meta word

            double now_s=ch.sample_index/ch.sample_rate;

            if(!ch.event_active &&
               now_s-ch.last_event_start_s>3 &&
               uni01(rng)<0.0007){
                ch.event_active=true;
                ch.event_t0_s=now_s;
                ch.last_event_start_s=now_s;
                ch.A0=A0U(rng);
                ch.decay_hz=decayU(rng);
            }

            for(int i=0;i<SPP;i++){
                double x=ADC_MID+noiseU(rng);
                if(ch.event_active){
                    double dt=(ch.sample_index/ch.sample_rate)-ch.event_t0_s;
                    double env=std::exp(-dt/(1.0/ch.decay_hz));
                    x+=ch.A0*env;
                    if(env<1e-4) ch.event_active=false;
                }
                x=std::clamp(x,0.0,ADC_FS);
                uint32_t v=((uint32_t)llround(x)&0xFFFFFF)<<8;
                *(uint32_t*)&buf[16+(1+i)*4]=be32(v);
                ch.sample_index++;
            }

            sendto(fd,buf.data(),buf.size(),0,(sockaddr*)&dst,sizeof(dst));
            ts+=TS_STEP;
        }
        std::this_thread::sleep_for(tick_period);
    }
}

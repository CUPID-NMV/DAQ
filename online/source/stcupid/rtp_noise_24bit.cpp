// rtp_noise_24bit.cpp
//
// Generates RTP-like UDP streams similar to your capture structure, but with
// 24-bit "offset-binary" samples mapping:
//   0x000000 => -10 V
//   0xFFFFFF => +10 V
// and white Gaussian noise (default 10 mV RMS) around 0 V (~0x800000).
//
// Payload format per sample (4 bytes):
//   [channel_id][MSB][MID][LSB]   where sample = 0xMSB MID LSB (big-endian 24-bit)
//
// Packet payload size: samples_per_packet*4 + 1 trailing 0x00 (as observed)
//
// Build (macOS/Linux):
//   g++ -O2 -std=c++17 rtp_noise_24bit.cpp -o rtp_noise_24bit
// Run:
//   ./rtp_noise_24bit <dst_ip> [channels] [pps_per_channel] [dst_port]
//
// Example:
//   ./rtp_noise_24bit 192.168.1.1 16 10 6666

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <algorithm>

// ---- Helpers: big-endian write ----
static inline void put_u16_be(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}
static inline void put_u32_be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

// Map volts in [-FS, +FS] to 24-bit offset-binary [0 .. 0xFFFFFF]
static inline uint32_t volts_to_u24_offset(double v, double full_scale_volts) {
    if (v >  full_scale_volts) v =  full_scale_volts;
    if (v < -full_scale_volts) v = -full_scale_volts;

    const double span = 2.0 * full_scale_volts;         // 20 V if FS=10
    const double norm = (v + full_scale_volts) / span;  // 0..1
    const double max24 = 16777215.0;                    // 2^24 - 1

    double code = norm * max24;
    code = std::clamp(code, 0.0, max24);

    return static_cast<uint32_t>(std::llround(code));   // 0..0xFFFFFF
}

struct ChannelState {
    uint32_t ssrc;
    uint16_t seq;
    uint32_t ts;
    uint8_t  channel_id; // placed in payload[0] of each 4-byte word
};

struct Config {
    std::string dst_ip = "127.0.0.1";
    uint16_t dst_port = 5004;

    int channels = 8;
    int packets_per_channel = 1000;

    // Payload format / pacing similar to your capture
    int samples_per_packet = 180;     // 180 words * 4 bytes = 720
    uint32_t ts_increment = 360;      // observed increment per packet per SSRC
    uint8_t payload_type = 23;        // PT observed
    uint32_t csrc0 = 0x800005DC;      // CSRC[0] observed
    bool marker = false;              // M bit

    // Digitizer model
    double full_scale_volts = 10.0;   // +/-10V
    double noise_rms_volts  = 0.01;   // 10 mV RMS

    // Scheduling
    double packets_per_second_per_channel = 10.0; // per-channel pacing
};

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog << " <dst_ip> [channels] [pps_per_channel] [dst_port]\n\n"
        << "Defaults:\n"
        << "  channels=8, pps_per_channel=10, dst_port=6666\n";
}

int main(int argc, char** argv) {
    Config cfg;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    cfg.dst_ip = argv[1];
    if (argc >= 3) cfg.channels = std::max(1, std::stoi(argv[2]));
    if (argc >= 4) cfg.packets_per_second_per_channel = std::max(0.1, std::stod(argv[3]));
    if (argc >= 5) cfg.dst_port = static_cast<uint16_t>(std::stoi(argv[4]));

    // UDP socket
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(cfg.dst_port);
    if (::inet_pton(AF_INET, cfg.dst_ip.c_str(), &dst.sin_addr) != 1) {
        std::cerr << "Invalid dst_ip: " << cfg.dst_ip << "\n";
        ::close(fd);
        return 1;
    }

    // RNG: gaussian white noise (sigma = RMS)
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::normal_distribution<double> gauss(0.0, cfg.noise_rms_volts);

    // Init channels: one SSRC per channel
    std::vector<ChannelState> ch;
    ch.reserve(cfg.channels);
    for (int i = 0; i < cfg.channels; i++) {
        ChannelState s{};
        s.ssrc = 0xBDAC0000u + static_cast<uint32_t>(i + 1); // any unique base
        s.seq = static_cast<uint16_t>(1 + i);
        s.ts  = 0;
        s.channel_id = static_cast<uint8_t>(i & 0xFF);
        ch.push_back(s);
    }

    // Packet layout: RTP header (12) + CSRC (4) + payload (samples*4 + 1)
    const size_t rtp_header_len = 12;
    const size_t csrc_len = 4; // CC=1
    const size_t payload_len = static_cast<size_t>(cfg.samples_per_packet) * 4 + 1;
    const size_t pkt_len = rtp_header_len + csrc_len + payload_len;

    std::vector<uint8_t> pkt(pkt_len);

    // RTP fixed fields
    // V=2 (0b10), P=0, X=0, CC=1 => 0x80 | 0x01 = 0x81
    pkt[0] = 0x81;

    // M + PT
    pkt[1] = static_cast<uint8_t>((cfg.marker ? 0x80 : 0x00) | (cfg.payload_type & 0x7F));

    // CSRC[0]
    put_u32_be(pkt.data() + 12, cfg.csrc0);

    const auto per_chan_period = std::chrono::duration<double>(1.0 / cfg.packets_per_second_per_channel);

    // Helpful numeric note
    const double span = 2.0 * cfg.full_scale_volts;               // 20V
    const double lsb_volts = span / 16777215.0;                   // volts per LSB
    const double noise_rms_lsb = cfg.noise_rms_volts / lsb_volts; // ~counts RMS

    std::cout
        << "Sending RTP-like 24-bit offset-binary noise\n"
        << "  dst=" << cfg.dst_ip << ":" << cfg.dst_port << "\n"
        << "  channels=" << cfg.channels
        << "  pps/ch=" << cfg.packets_per_second_per_channel
        << "  packets/ch=" << cfg.packets_per_channel << "\n"
        << "  payload=" << payload_len << " bytes ("
        << cfg.samples_per_packet << " samples *4 + 1)\n"
        << "  volts FS=+/-" << cfg.full_scale_volts << "V, noise=" << (cfg.noise_rms_volts * 1000.0) << " mV RMS"
        << " (~" << noise_rms_lsb << " LSB RMS)\n"
        << "  RTP: PT=" << int(cfg.payload_type) << " CC=1 CSRC0=0x" << std::hex << cfg.csrc0 << std::dec
        << " ts_inc=" << cfg.ts_increment << "\n";

    // Round-robin: each "frame" sends one packet per channel, then sleeps so that
    // each channel achieves ~pps/ch.
    for (int n = 0; n < cfg.packets_per_channel; n++) {
        auto t0 = std::chrono::steady_clock::now();

        for (int i = 0; i < cfg.channels; i++) {
            ChannelState& s = ch[i];

            // Sequence / timestamp / SSRC
            put_u16_be(pkt.data() + 2, s.seq);
            put_u32_be(pkt.data() + 4, s.ts);
            put_u32_be(pkt.data() + 8, s.ssrc);

            // Payload fill
            uint8_t* pl = pkt.data() + rtp_header_len + csrc_len;

            for (int k = 0; k < cfg.samples_per_packet; k++) {
                // White Gaussian noise centered at 0 V
                double v = gauss(gen);

                // Convert to 24-bit offset-binary
                uint32_t u24 = volts_to_u24_offset(v, cfg.full_scale_volts); // 0..0xFFFFFF

                // Pack: [channel_id][MSB][MID][LSB]
                pl[k * 4 + 0] = s.channel_id;
                pl[k * 4 + 1] = static_cast<uint8_t>((u24 >> 16) & 0xFF);
                pl[k * 4 + 2] = static_cast<uint8_t>((u24 >>  8) & 0xFF);
                pl[k * 4 + 3] = static_cast<uint8_t>( u24        & 0xFF);
            }

            // trailing byte as seen in your dump
            pl[cfg.samples_per_packet * 4] = 0x00;

            ssize_t sent = ::sendto(fd, pkt.data(), pkt.size(), 0,
                                    reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
            if (sent < 0) {
                perror("sendto");
                ::close(fd);
                return 1;
            }

            // Advance counters
            s.seq = static_cast<uint16_t>(s.seq + 1);
            s.ts += cfg.ts_increment;
        }

        auto t1 = std::chrono::steady_clock::now();
        auto elapsed = t1 - t0;
        auto sleep_for = per_chan_period - elapsed;

        if (sleep_for > std::chrono::duration<double>(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }

    ::close(fd);
    return 0;
}

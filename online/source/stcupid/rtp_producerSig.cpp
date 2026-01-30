// rtp_producer.cpp
// Producer UDP:
//  - rumore bianco signed (±5% FS/2)
//  - impulsi positivi con decadimento esponenziale (monotono)
//  - distanza minima tra due eventi: 3 s
//  - distanza media ~10 s (event_start_prob_per_packet = 0.0007)
//
// Payload pulito:
//  - word0 = metadata (bessel = 0)
//  - word1.. = samples
//
// Compila:
//   g++ -O2 -std=c++17 -Wall -Wextra -pedantic rtp_producer.cpp -o rtp_producer
// Usa:
//   ./rtp_producer 127.0.0.1 5004

#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

static uint32_t to_be32(uint32_t x) { return htonl(x); }
static uint16_t to_be16(uint16_t x) { return htons(x); }

static inline double clamp(double x, double lo, double hi) {
    return (x < lo) ? lo : (x > hi) ? hi : x;
}

int main(int argc, char** argv) {
    const char* dst_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int dst_port        = (argc > 2) ? std::stoi(argv[2]) : 5004;

    // --- Protocol / RTP ---
    const uint32_t ssrc = 0xC0FFEE01;
    const uint32_t freq_div = 3000;   // CSRC[0] -> freq=6000000/(2*freq_div)=1000 Hz
    const uint32_t TS_STEP = 360;     // timestamp per packet

    // --- Streaming ---
    const int packets_per_sec = 200;
    const int samples_per_packet = 256;
    const double sample_rate = 50'000.0;   // clock logico per dt (solo simulazione)

    // --- ADC 24-bit unsigned ---
    const double full_scale = (double)((1u << 24) - 1u);
    const double mid_scale  = full_scale / 2.0;

    // Rumore bianco ±5% FS/2
    const double noise_peak = 0.005 * (full_scale / 2.0);

    // Eventi
    const double event_min_separation_s = 3.0;      // cooldown
    const double event_start_prob_per_packet = 0.0007; // distanza media ~10 s
    const double event_max_duration_s = 3.0;        // sicurezza

    // Ampiezza impulso (positiva)
    const double A0_min = 0.05 * (full_scale / 2.0);
    const double A0_max = 0.60 * (full_scale / 2.0);

    // Decadimento: tau = 1 / decay_hz
    const double decay_rate_min_hz = 10;
    const double decay_rate_max_hz = 50;

    // Payload: word0 metadata + samples
    const int payload_words = 1 + samples_per_packet;

    // RTP header (12B) + CSRC (4B) = 16B
    std::vector<uint8_t> buf(16 + payload_words * 4);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)dst_port);
    if (::inet_pton(AF_INET, dst_ip, &dst.sin_addr) != 1) {
        std::cerr << "inet_pton failed for " << dst_ip << "\n";
        return 1;
    }

    // RNG
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_real_distribution<double> uni01(0.0, 1.0);
    std::uniform_real_distribution<double> noiseU(-noise_peak, noise_peak);
    std::uniform_real_distribution<double> A0U(A0_min, A0_max);
    std::uniform_real_distribution<double> decayU(decay_rate_min_hz, decay_rate_max_hz);

    // Stato evento
    bool event_active = false;
    double event_t0_s = 0.0;
    double event_A0 = 0.0;
    double event_decay_hz = 0.0;
    double last_event_start_s = -1e9;

    uint16_t seq = 0;
    uint32_t ts = 0;
    uint64_t sample_index = 0;

    const auto period = std::chrono::microseconds(1'000'000 / packets_per_sec);
    auto next_tick = std::chrono::steady_clock::now();

    std::cout << "RTP Producer (noise + exp-decay pulses) -> "
              << dst_ip << ":" << dst_port
              << " pps=" << packets_per_sec
              << " spp=" << samples_per_packet
              << " bytes=" << (16 + payload_words * 4)
              << "\n";

    while (true) {
        // RTP header
        buf[0] = (2u << 6) | (0u << 5) | (0u << 4) | (1u); // V=2, CC=1
        buf[1] = 96;
        uint16_t seq_be = to_be16(seq);
        std::memcpy(&buf[2], &seq_be, 2);
        uint32_t ts_be = to_be32(ts);
        std::memcpy(&buf[4], &ts_be, 4);
        uint32_t ssrc_be = to_be32(ssrc);
        std::memcpy(&buf[8], &ssrc_be, 4);
        uint32_t csrc0_be = to_be32(freq_div & 0x7fffffff);
        std::memcpy(&buf[12], &csrc0_be, 4);

        double now_s = (double)sample_index / sample_rate;

        // Start evento (cooldown + probabilità)
        if (!event_active &&
            (now_s - last_event_start_s) >= event_min_separation_s &&
            (uni01(rng) < event_start_prob_per_packet)) {

            event_active = true;
            event_t0_s = now_s;
            last_event_start_s = now_s;
            event_A0 = A0U(rng);
            event_decay_hz = decayU(rng);

            std::cout << "[event START] A0=" << event_A0
                      << " decay_hz=" << event_decay_hz
                      << " tau=" << (1.0 / event_decay_hz) << " s\n";
        }

        // Payload word0 = metadata (bessel=0)
        uint32_t meta_word = 0x00000000u;
        uint32_t meta_be = to_be32(meta_word);
        std::memcpy(&buf[16], &meta_be, 4);

        // Samples
        for (int i = 0; i < samples_per_packet; i++, sample_index++) {
            double t_s = (double)sample_index / sample_rate;
            double x = mid_scale + noiseU(rng);

            if (event_active) {
                double dt = t_s - event_t0_s;
                if (dt < 0) dt = 0;
                double tau = 1.0 / event_decay_hz;
                double env = std::exp(-dt / tau);
                x += event_A0 * env;
                if (dt > event_max_duration_s || env < 1e-3) {
                    event_active = false;
                }
            }

            x = clamp(x, 0.0, full_scale);
            uint32_t val24 = ((uint32_t)std::llround(x)) & 0x00FFFFFFu;
            uint32_t word32 = (val24 << 8);
            uint32_t be = to_be32(word32);
            std::memcpy(&buf[16 + (1 + i) * 4], &be, 4);
        }

        // Send
        if (::sendto(fd, buf.data(), (int)buf.size(), 0,
                     (sockaddr*)&dst, sizeof(dst)) < 0) {
            perror("sendto");
            break;
        }

        seq++;
        ts += TS_STEP;
        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    ::close(fd);
    return 0;
}

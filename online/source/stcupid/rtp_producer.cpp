// rtp_producer.cpp
// Producer UDP: invia pacchetti con header RTP (CC=1 => 1 CSRC) + payload di word32 BE.
// Compatibile con il parsing visto nel repo:
// - bessel = (word0 >> 28)  -> qui forzato a 0
// - freq  = 6000000 / (2 * (csrc0 & 0x7fffffff))
// - values = ntohl(word) >> 8  -> qui word = (val24 << 8)

#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <unistd.h>

static uint32_t to_be32(uint32_t x) { return htonl(x); }
static uint16_t to_be16(uint16_t x) { return htons(x); }

int main(int argc, char** argv) {
    const char* dst_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int dst_port        = (argc > 2) ? std::stoi(argv[2]) : 5004;

    // --- Parametri simulazione (modifica qui se vuoi) ---
    const uint32_t ssrc = 0xC0FFEE01;   // canale (SSRC)
    const uint32_t freq_div = 3000;     // CSRC[0]; nel repo: freq=6000000/(2*freq_div)=1000 Hz
    const int packets_per_sec = 200;    // rate pacchetti
    const int samples_per_packet = 256; // word per pacchetto (payload)
    const double tone_hz = 50.0;        // sinusoide generata (solo per test)
    const double adc_max = (double)((1u << 24) - 1u);

    // Timestamp "per packet" (coerente con le assunzioni viste nel repo: gap ~360)
    const uint32_t TS_STEP = 360;

    // RTP header (12B) + 1 CSRC (4B) = 16B
    // Payload: samples_per_packet * 4 bytes
    std::vector<uint8_t> buf(16 + samples_per_packet * 4);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)dst_port);
    if (::inet_pton(AF_INET, dst_ip, &dst.sin_addr) != 1) {
        std::cerr << "inet_pton failed for " << dst_ip << "\n";
        return 1;
    }

    uint16_t seq = 0;
    uint32_t ts  = 0;

    const auto period = std::chrono::microseconds(1'000'000 / packets_per_sec);
    auto next_tick = std::chrono::steady_clock::now();

    std::cout << "RTP Producer -> " << dst_ip << ":" << dst_port
              << " ssrc=0x" << std::hex << ssrc << std::dec
              << " pps=" << packets_per_sec
              << " spp=" << samples_per_packet
              << " TS_STEP=" << TS_STEP
              << "\n";

    while (true) {
        // -------- RTP fixed header --------
        // Byte0: V(2)=2, P=0, X=0, CC=1 (1 CSRC)
        buf[0] = (2u << 6) | (0u << 5) | (0u << 4) | (1u);
        // Byte1: M=0, PT=96 (dinamico)
        buf[1] = 96;

        // seq
        uint16_t seq_be = to_be16(seq);
        std::memcpy(&buf[2], &seq_be, 2);

        // timestamp (per packet)
        uint32_t ts_be = to_be32(ts);
        std::memcpy(&buf[4], &ts_be, 4);

        // SSRC
        uint32_t ssrc_be = to_be32(ssrc);
        std::memcpy(&buf[8], &ssrc_be, 4);

        // 1 CSRC (csrc[0]) usato dal parser per derivare freq
        uint32_t csrc0_be = to_be32(freq_div & 0x7fffffff);
        std::memcpy(&buf[12], &csrc0_be, 4);

        // -------- Payload --------
        // word32 big-endian, con dato 24-bit tale che (ntohl(word) >> 8) = sample24.
        // Inoltre, per compatibilità con bessel=(word0>>28) nel repo, forziamo bessel=0:
        // - bessel = top4bits(word0) => azzeriamo i bit 31..28 del primo word.
        for (int i = 0; i < samples_per_packet; i++) {
            // tempo continuo solo per generare una sinusoide "bella" (non c'entra con ts RTP)
            // Se vuoi agganciarlo a ts, puoi usare (ts * something).
            double t = (double)(seq * samples_per_packet + (uint32_t)i) / 50'000.0;
            double s = 0.5 * (std::sin(2.0 * M_PI * tone_hz * t) + 1.0); // [0,1]

            uint32_t val24 = (uint32_t)std::llround(s * adc_max) & 0x00FFFFFFu;

            // Forza bessel=0 nel primo word: i bit 31..28 di word32 corrispondono
            // a val24 bits 23..20 (perché word32 = val24<<8).
            if (i == 0) {
                val24 &= 0x000FFFFFu; // azzera val24[23:20] => word32[31:28]=0
            }

            uint32_t word32 = (val24 << 8);
            uint32_t be = to_be32(word32);
            std::memcpy(&buf[16 + i * 4], &be, 4);
        }

        ssize_t sent = ::sendto(fd, buf.data(), (int)buf.size(), 0,
                                (sockaddr*)&dst, sizeof(dst));
        if (sent < 0) { perror("sendto"); break; }

        seq++;
        ts += TS_STEP;

        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    ::close(fd);
    return 0;
}


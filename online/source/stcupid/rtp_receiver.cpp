// rtp_receiver.cpp
// Receiver UDP: riceve pacchetti RTP (CC>=1 per leggere CSRC[0]) e decodifica
// in modo compatibile con il parsing visto nel repo:
// - bessel = (word0 >> 28)
// - freq  = 6000000 / (2 * (csrc0 & 0x7fffffff))
// - values = ntohl(word) >> 8

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

static uint32_t from_be32(uint32_t x) { return ntohl(x); }
static uint16_t from_be16(uint16_t x) { return ntohs(x); }

int main(int argc, char** argv) {
    int listen_port = (argc > 1) ? std::stoi(argv[1]) : 5004;

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }

    std::cout << "RTP Receiver listening on UDP port " << listen_port << "\n";

    std::vector<uint8_t> buf(65536);

    while (true) {
        ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
        if (n < 0) { perror("recvfrom"); break; }
        if (n < 12) { std::cerr << "packet too small: " << n << "\n"; continue; }

        // ---- Parse RTP fixed header ----
        uint8_t vpxcc = buf[0];
        uint8_t mpt   = buf[1];

        uint8_t version = (vpxcc >> 6) & 0x03;
        uint8_t padding = (vpxcc >> 5) & 0x01;
        uint8_t ext     = (vpxcc >> 4) & 0x01;
        uint8_t cc      = (vpxcc & 0x0F);

        uint8_t marker  = (mpt >> 7) & 0x01;
        uint8_t pt      = (mpt & 0x7F);

        uint16_t seq_be;
        uint32_t ts_be, ssrc_be;
        std::memcpy(&seq_be,  &buf[2], 2);
        std::memcpy(&ts_be,   &buf[4], 4);
        std::memcpy(&ssrc_be, &buf[8], 4);

        uint16_t seq  = from_be16(seq_be);
        uint32_t ts   = from_be32(ts_be);
        uint32_t ssrc = from_be32(ssrc_be);

        // ---- CSRC parsing ----
        size_t header_len = 12 + 4u * (size_t)cc;
        if ((size_t)n < header_len) {
            std::cerr << "bad packet: n=" << n << " < header_len=" << header_len << "\n";
            continue;
        }

        uint32_t csrc0 = 0;
        if (cc >= 1) {
            uint32_t csrc0_be;
            std::memcpy(&csrc0_be, &buf[12], 4);
            csrc0 = from_be32(csrc0_be);
        }

        // NOTE: Questo receiver "minimo" non implementa RTP header extension (X=1) né padding (P=1).
        // Per il producer d'esempio X=0, P=0.
        if (ext || padding) {
            std::cerr << "warning: ext=" << (int)ext << " padding=" << (int)padding
                      << " not handled in this minimal receiver\n";
        }

        // ---- Payload ----
        size_t payload_off = header_len;
        size_t payload_len = (size_t)n - payload_off;

        // Calcolo freq come nel tuo PacketParser
        double freq = 0.0;
        uint32_t denom = (csrc0 & 0x7fffffff);
        if (denom != 0) freq = 6000000.0 / (2.0 * (double)denom);

        // Estrai bessel come nel repo: dai 4 bit alti del primo word della payload
        uint32_t bessel = 0;
        if (payload_len >= 4) {
            uint32_t w0_be;
            std::memcpy(&w0_be, &buf[payload_off], 4);
            uint32_t w0 = from_be32(w0_be);
            bessel = (w0 >> 28) & 0x0Fu;
        }

        std::cout << "RTP v=" << (int)version
                  << " pt=" << (int)pt
                  << " m=" << (int)marker
                  << " cc=" << (int)cc
                  << " ssrc=0x" << std::hex << ssrc << std::dec
                  << " seq=" << seq
                  << " ts=" << ts
                  << " csrc0=" << csrc0
                  << " freq~" << freq << " Hz"
                  << " bessel=" << bessel
                  << " payload_bytes=" << payload_len
                  << "\n";

        if (payload_len < 4) continue;

        if (payload_len % 4 != 0) {
            std::cerr << "warning: payload not multiple of 4 bytes: " << payload_len << "\n";
        }

        // Come nel repo: interpretazione BE32, poi >> 8 (campione 24-bit)
        size_t words = payload_len / 4;
        size_t to_print = (words < 8) ? words : 8;

        std::cout << "  samples24 (first " << to_print << "): ";
        for (size_t i = 0; i < to_print; i++) {
            uint32_t be;
            std::memcpy(&be, &buf[payload_off + i * 4], 4);
            uint32_t w = from_be32(be);
            uint32_t v24 = (w >> 8) & 0x00FFFFFFu;
            std::cout << v24 << (i + 1 < to_print ? ", " : "");
        }
        std::cout << "\n";
    }

    ::close(fd);
    return 0;
}

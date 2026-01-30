#!/usr/bin/env python3
import socket
import struct
import time
from collections import deque

import numpy as np
import matplotlib.pyplot as plt

# -----------------------------
# Config
# -----------------------------
LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 5004

# ADC 24-bit unsigned
ADC_FULL_SCALE = (1 << 24) - 1
ADC_MID = ADC_FULL_SCALE / 2.0

# Plot settings
WINDOW_SAMPLES = 50_000          # quanti campioni tenere (es. ~1s se 50 kS/s)
PLOT_UPDATE_HZ = 20              # refresh plot
DOWNSAMPLE_FOR_PLOT = 10         # per alleggerire: plottiamo 1 campione ogni N

# Physical scaling
FULL_SCALE_VOLT = 5.0
ADC_HALF_SCALE = ADC_FULL_SCALE / 2.0

# Optional: filtra solo un SSRC (metti None per accettare il primo visto)
FILTER_SSRC = None


def parse_rtp_packet(pkt: bytes):
    if len(pkt) < 12:
        return None

    b0, b1 = pkt[0], pkt[1]
    version = (b0 >> 6) & 0x03
    cc = b0 & 0x0F
    pt = b1 & 0x7F

    seq = struct.unpack("!H", pkt[2:4])[0]
    ts = struct.unpack("!I", pkt[4:8])[0]
    ssrc = struct.unpack("!I", pkt[8:12])[0]

    header_len = 12 + 4 * cc
    if len(pkt) < header_len:
        return None

    csrc0 = 0
    if cc >= 1:
        csrc0 = struct.unpack("!I", pkt[12:16])[0]

    payload = pkt[header_len:]
    nwords = len(payload) // 4
    if nwords < 2:
        return None

    words = np.frombuffer(payload[: nwords * 4], dtype=">u4")  # BE u32

    # word0 = metadata
    meta0 = int(words[0])
    bessel = (meta0 >> 28) & 0x0F

    # word1.. = samples
    sample_words = words[1:]
    samples = ((sample_words >> 8) & 0x00FFFFFF).astype(np.uint32)

    return {
        "version": version, "cc": cc, "pt": pt, "seq": seq, "ts": ts, "ssrc": ssrc,
        "csrc0": csrc0, "bessel": bessel, "samples": samples
    }


def calc_freq_from_csrc0(csrc0: int) -> float:
    denom = csrc0 & 0x7FFFFFFF
    if denom == 0:
        return 0.0
    return 6000000.0 / (2.0 * float(denom))


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LISTEN_IP, LISTEN_PORT))
    sock.settimeout(0.2)

    print(f"Python RTP receiver listening on {LISTEN_IP}:{LISTEN_PORT}")

    # Buffer circolare di campioni (signed float)
    ring = deque(maxlen=WINDOW_SAMPLES)

    chosen_ssrc = None
    last_print = 0.0
    pkt_count = 0

    # Plot init
    plt.ion()
    fig, ax = plt.subplots()
    line, = ax.plot([], [])
    ax.set_title("RTP samples (24-bit -> signed, real-time)")
    ax.set_xlabel("sample index (window)")
    ax.set_ylabel("ADC counts (signed around mid-scale)")
    text = ax.text(0.01, 0.98, "", transform=ax.transAxes, va="top")

    next_plot = time.time()

    while True:
        # Ricezione burst di pacchetti per riempire buffer
        got_any = False
        for _ in range(200):  # leggi più pacchetti per ciclo
            try:
                pkt, addr = sock.recvfrom(65535)
            except socket.timeout:
                break

            info = parse_rtp_packet(pkt)
            if info is None:
                continue

            ssrc = info["ssrc"]
            if FILTER_SSRC is not None and ssrc != FILTER_SSRC:
                continue

            if chosen_ssrc is None:
                chosen_ssrc = ssrc

            if ssrc != chosen_ssrc:
                continue

            samples_u = info["samples"]
            if samples_u.size == 0:
                continue

            # converti a signed centrato (come per vedere rumore/burst)
            samples_signed = samples_u.astype(np.float64) - ADC_MID

            # append al ring
            ring.extend(samples_signed.tolist())

            pkt_count += 1
            got_any = True

            now = time.time()
            if now - last_print > 1.0:
                freq = calc_freq_from_csrc0(info["csrc0"])
                print(f"ssrc=0x{ssrc:08x} seq={info['seq']} ts={info['ts']} "
                      f"csrc0={info['csrc0']} freq~{freq:.1f}Hz bessel={info['bessel']} "
                      f"words={samples_u.size} pkts={pkt_count}")
                last_print = now

        # Aggiorna plot a frequenza limitata
        now = time.time()
        if now >= next_plot:
            if len(ring) > 0:
                y_adc = np.fromiter(ring, dtype=np.float64)
                y = y_adc * (FULL_SCALE_VOLT / ADC_HALF_SCALE)


                # downsample per render veloce
                if DOWNSAMPLE_FOR_PLOT > 1 and y.size > DOWNSAMPLE_FOR_PLOT:
                    y_plot = y[::DOWNSAMPLE_FOR_PLOT]
                else:
                    y_plot = y

                x_plot = np.arange(y_plot.size)

                line.set_data(x_plot, y_plot)

                # autoscale
                ax.relim()
#                ax.autoscale_view()
                ax.set_ylim(-FULL_SCALE_VOLT, FULL_SCALE_VOLT)
                ax.set_ylabel("Amplitude [V]")


                # stats
                rms = float(np.sqrt(np.mean(y**2))) if y.size else 0.0
                peak = float(np.max(np.abs(y))) if y.size else 0.0
                text.set_text(
                    f"ssrc={('0x%08x' % chosen_ssrc) if chosen_ssrc is not None else '---'}\n"
                    f"samples_in_window={len(ring)} (plot ds={DOWNSAMPLE_FOR_PLOT})\n"
                    f"rms={rms:.1f}  peak={peak:.1f}"
                )

                fig.canvas.draw()
                fig.canvas.flush_events()

            next_plot = now + (1.0 / PLOT_UPDATE_HZ)
        if now - last_print > 1.0:
            print("UDP bytes:", len(pkt), "samples:", samples_u.size)

        # se non riceve nulla, lascia respirare un attimo
        if not got_any:
            time.sleep(0.01)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import time
import argparse
from collections import deque

import numpy as np
import matplotlib.pyplot as plt

import midas
import midas.client


ADC_FULL_SCALE = (1 << 24) - 1
ADC_HALF_SCALE = ADC_FULL_SCALE / 2.0
FULL_SCALE_VOLT = 5.0

def adc24_to_volt(samples_u24: np.ndarray) -> np.ndarray:
    # samples_u24: uint32 with valid bits [23:0]
    signed = samples_u24.astype(np.float64) - ADC_HALF_SCALE
    volt = signed * (FULL_SCALE_VOLT / ADC_HALF_SCALE)
    return volt

def main():
    ap = argparse.ArgumentParser(description="Online plot MIDAS banks C000..C019")
    ap.add_argument("--host", default="localhost", help="MIDAS host (default: localhost)")
    ap.add_argument("--expt", default="WC", help="MIDAS experiment name")
    ap.add_argument("--buffer", default="SYSTEM", help="Event buffer name (default: SYSTEM)")
    ap.add_argument("--nch", type=int, default=20, help="Number of channels (default: 20)")
    ap.add_argument("--ch", type=int, default=0, help="Channel index to plot (0..nch-1), default: 0")
    ap.add_argument("--window", type=int, default=50000, help="Samples in plot window (default: 50000)")
    ap.add_argument("--update_hz", type=float, default=20.0, help="Plot refresh rate (default: 20 Hz)")
    ap.add_argument("--ds", type=int, default=10, help="Downsample for plotting (default: 10)")
    ap.add_argument("--ylim", type=float, default=5.0, help="Y limit in Volt (default: 5.0 => +/-5V)")
    args = ap.parse_args()

    if not (0 <= args.ch < args.nch):
        raise SystemExit(f"--ch must be in [0, {args.nch-1}]")

    # connect to MIDAS
    client = midas.client.MidasClient("pyPlot", args.host, args.expt)

    # open event buffer
    buf = client.open_event_buffer(args.buffer)

    bank_name = f"C{args.ch:03d}"

    ring = deque(maxlen=args.window)
    last_stat = time.time()
    ev_count = 0

    plt.ion()
    fig, ax = plt.subplots()
    line, = ax.plot([], [])
    ax.set_title(f"MIDAS online plot - bank {bank_name} (FS=±{FULL_SCALE_VOLT}V)")
    ax.set_xlabel("sample index (window)")
    ax.set_ylabel("Amplitude [V]")
    ax.set_ylim(-args.ylim, args.ylim)
    txt = ax.text(0.01, 0.98, "", transform=ax.transAxes, va="top")

    next_plot = time.time()

    print(f"Connected to MIDAS expt='{args.expt}' host='{args.host}', buffer='{args.buffer}'")
    print(f"Plotting bank {bank_name}, window={args.window}, ds={args.ds}, ylim=±{args.ylim} V")

    while True:
        # read next event (blocking a little)
        # If your MIDAS python uses different API names, tell me and I’ll adapt quickly.
        event = buf.receive_event(timeout_ms=200)
        if event is None:
            # no event within timeout
            pass
        else:
            ev_count += 1
            try:
                # event is midas.event.Event-like
                # Extract bank as numpy array of uint32
                b = event.get_bank(bank_name)
                if b is not None and len(b) > 0:
                    samples_u = np.array(b, dtype=np.uint32) & 0x00FFFFFF
                    v = adc24_to_volt(samples_u)
                    ring.extend(v.tolist())
            except Exception as e:
                # bank missing or decode issue
                # Keep running
                pass

        now = time.time()

        # periodic stats
        if now - last_stat > 1.0:
            n = len(ring)
            if n:
                y = np.fromiter(ring, dtype=np.float64)
                rms = float(np.sqrt(np.mean(y*y)))
                peak = float(np.max(np.abs(y)))
            else:
                rms = 0.0
                peak = 0.0
            print(f"events={ev_count} ring_samples={n} rms={rms:.3f}V peak={peak:.3f}V")
            last_stat = now

        # plot refresh
        if now >= next_plot:
            if len(ring) > 0:
                y = np.fromiter(ring, dtype=np.float64)
                if args.ds > 1 and y.size > args.ds:
                    y_plot = y[::args.ds]
                else:
                    y_plot = y
                x_plot = np.arange(y_plot.size)

                line.set_data(x_plot, y_plot)
                ax.set_xlim(0, max(1, x_plot.size))

                # update overlay
                rms = float(np.sqrt(np.mean(y*y))) if y.size else 0.0
                peak = float(np.max(np.abs(y))) if y.size else 0.0
                txt.set_text(
                    f"bank={bank_name}\n"
                    f"samples={len(ring)} (ds={args.ds})\n"
                    f"rms={rms:.3f} V  peak={peak:.3f} V"
                )

                fig.canvas.draw()
                fig.canvas.flush_events()

            next_plot = now + (1.0 / args.update_hz)


if __name__ == "__main__":
    main()

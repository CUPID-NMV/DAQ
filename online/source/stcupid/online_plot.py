#!/usr/bin/env python3
import os
import sys
import time
from datetime import datetime

import matplotlib as mpl

print("DISPLAY =", os.environ.get("DISPLAY"))
if os.environ.get("DISPLAY"):
    os.environ.setdefault("QT_X11_NO_MITSHM", "1")
    mpl.use("Qt5Agg", force=True)
else:
    mpl.use("Agg", force=True)

mpl.rcParams["figure.raise_window"] = False

import matplotlib.pyplot as plt
import numpy as np

import midas
import midas.client


# -------- ADC / scaling --------
ADC_FULL_SCALE = (1 << 24) - 1
ADC_HALF_SCALE = ADC_FULL_SCALE / 2.0
FULL_SCALE_VOLT = 5.0  # ±5V corresponds to ±ADC_HALF_SCALE

def adc24_to_volt(samples_u24: np.ndarray) -> np.ndarray:
    """u24 unsigned -> signed centered -> volts"""
    signed = samples_u24.astype(np.float64) - ADC_HALF_SCALE
    volt = signed * (FULL_SCALE_VOLT / ADC_HALF_SCALE)
    return volt


def setup_grid(fig, nch, grid=False):
    """Create axes+lines once, return (axes_flat, lines)."""
    import math
    ncols = 4 if nch > 4 else nch
    nrows = math.ceil(nch / ncols)

    axes = fig.subplots(nrows=nrows, ncols=ncols, squeeze=False)
    axes_flat = axes.ravel()

    lines = []
    for i, ax in enumerate(axes_flat):
        (ln,) = ax.plot([], [], linewidth=1.0)
        lines.append(ln)
        if grid:
            ax.grid(True, alpha=0.3)
        if i >= nch:
            ax.set_visible(False)

    # Avoid focus-stealing (best-effort)
    try:
        from PyQt5 import QtCore
        mgr = plt.get_current_fig_manager()
        mgr.window.setAttribute(QtCore.Qt.WA_ShowWithoutActivating, True)
    except Exception:
        pass

    return axes_flat, lines


def update_grid(axes, lines, waveforms, smin, emax, vmin, vmax, unit_label="V"):
    """
    waveforms: list of 1D numpy arrays (can have different lengths)
    smin/emax applied per channel with clamping.
    """
    nch = len(waveforms)
    for ch in range(nch):
        ax = axes[ch]
        ln = lines[ch]
        y = waveforms[ch]
        if y is None or y.size == 0:
            ln.set_data([], [])
            continue

        n = y.size
        a = max(0, int(smin))
        b = min(int(emax), n) if emax is not None else n
        if b <= a:
            a, b = 0, n

        x = np.arange(a, b, dtype=float)
        ln.set_data(x, y[a:b])

        ax.set_xlim(float(x[0]), float(x[-1]) if x.size > 1 else float(x[0]) + 1.0)

        # fixed y limits for stability
        ax.set_ylim(vmin, vmax)
        ax.set_ylabel(f"ch{ch} [{unit_label}]")

    for j in range(nch, len(axes)):
        if axes[j].get_visible():
            axes[j].set_visible(False)


def _bank_to_u32_array(bank):
    """
    Convert a MIDAS Python Bank into np.uint32 array.
    Supports different midas python implementations:
      - bank.data is bytes-like -> frombuffer
      - bank.data is tuple/list of ints -> array()
      - bank.values exists -> array()
    """
    # prefer explicit attributes if present
    if hasattr(bank, "data"):
        data = bank.data
    elif hasattr(bank, "values"):
        data = bank.values
    else:
        data = bank  # last resort

    # bytes-like
    if isinstance(data, (bytes, bytearray, memoryview)):
        return np.frombuffer(data, dtype=np.uint32).copy()

    # tuple/list of python ints
    if isinstance(data, (tuple, list)):
        return np.array(data, dtype=np.uint32)

    # numpy already?
    if isinstance(data, np.ndarray):
        return data.astype(np.uint32, copy=False)

    # fallback: try iterable
    return np.array(list(data), dtype=np.uint32)


def read_cupid_banks(event, nch, mask24=True):
    out = [None] * nch

    for ch in range(nch):
        bname = f"C{ch:03d}"
        bank = event.banks.get(bname, None)
        if bank is None:
            continue

        arr = _bank_to_u32_array(bank)

        if mask24:
            arr &= 0x00FFFFFF

        out[ch] = arr

    return out


def main(grid=False, nch=20, channel=None,
         hmin=0, hmax=2048,
         vmin=-5.0, vmax=5.0,
         update_pause_s=0.05,
         verbose=False):

    event_before = 0

    # Uses your working connection mode (no host/expt args)
    client = midas.client.MidasClient("cupid_display")
    buffer_handle = client.open_event_buffer("SYSTEM", None, 1000000000)

    request_id = client.register_event_request(
        buffer_handle,
        sampling_type=midas.GET_RECENT
    )

    # Drop old events
    try:
        midas.bm_skip_event(buffer_handle)
    except AttributeError:
        pass

    plt.ion()
    fig = plt.figure(figsize=(14, 8), facecolor="#DEDEDE")

    # avoid focus steal
    try:
        from PyQt5 import QtCore
        mgr = plt.get_current_fig_manager()
        mgr.window.setAttribute(QtCore.Qt.WA_ShowWithoutActivating, True)
    except Exception:
        pass

    if channel is None:
        # all channels grid
        axes, lines = setup_grid(fig, nch=nch, grid=grid)
    else:
        ax = fig.add_subplot(1, 1, 1)
        (ln,) = ax.plot([], [], linewidth=1.0)
        axes, lines = [ax], [ln]
        if grid:
            ax.grid(True, alpha=0.3)
        ax.set_ylim(vmin, vmax)
        ax.set_ylabel("Amplitude [V]")
        ax.set_xlabel("sample index")

    try:
        fig.show()
    except Exception:
        pass

    print("CUPID MIDAS display running..., Ctrl-C to stop")

    while True:
        try:
            event = client.receive_event(buffer_handle, async_flag=False)
            if event is None:
                continue

            if event.header.is_midas_internal_event():
                continue

            event_number = event.header.serial_number
            event_time = datetime.fromtimestamp(event.header.timestamp).strftime("%Y-%m-%d %H:%M:%S")

            if verbose:
                bank_names = ", ".join(b.name for b in event.banks.values())
                print(f"Event #{event_number} banks: {bank_names}")

            banks_u24 = read_cupid_banks(event, nch=nch, mask24=True)

            # convert to volts
            banks_v = [adc24_to_volt(b) if b is not None else None for b in banks_u24]

            if channel is None:
                # plot all
                update_grid(
                    axes=axes,
                    lines=lines,
                    waveforms=banks_v,
                    smin=hmin,
                    emax=hmax,
                    vmin=vmin,
                    vmax=vmax,
                    unit_label="V",
                )
                fig.suptitle(f"Event: {event_number:d} at {event_time:s}  (C000..C{nch-1:03d})")
                fig.canvas.draw_idle()

            else:
                # plot single channel
                ch = int(channel)
                y = banks_v[ch]
                ax = axes[0]
                ln = lines[0]

                if y is None or y.size == 0:
                    ln.set_data([], [])
                else:
                    a = max(0, int(hmin))
                    b = min(int(hmax), y.size)
                    if b <= a:
                        a, b = 0, y.size
                    x = np.arange(a, b, dtype=float)
                    ln.set_data(x, y[a:b])
                    ax.set_xlim(float(x[0]), float(x[-1]) if x.size > 1 else float(x[0]) + 1.0)

                ax.set_ylim(vmin, vmax)
                ax.set_title(f"Bank C{ch:03d}  Event {event_number}  {event_time}")
                fig.canvas.draw_idle()

            # terminal status
            print(f"Data rate: {float(event_number - event_before):.2f} Hz", end="\r", flush=True)
            event_before = event_number

            plt.pause(update_pause_s)
            client.communicate(10)

        except KeyboardInterrupt:
            client.deregister_event_request(buffer_handle, request_id)
            client.disconnect()
            print("\nBye, bye...")
            sys.exit(0)


if __name__ == "__main__":
    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog [options]")
    parser.add_option("-g", "--grid", dest="grid", action="store_true", default=False, help="show grid;")
    parser.add_option("-n", "--nch", dest="nch", action="store", type="int", default=20, help="number of channels [20]")
    parser.add_option("-c", "--channel", dest="channel", action="store", type="string", default="all",
                      help="channel index to view (0..nch-1) or 'all' [all]")
    parser.add_option("-s", "--hmin", dest="hmin", action="store", type="int", default=0, help="hmin [0]")
    parser.add_option("-e", "--hmax", dest="hmax", action="store", type="int", default=2048, help="hmax [2048]")
    parser.add_option("--vmin", dest="vmin", action="store", type="float", default=-5.0, help="vmin [-5.0]")
    parser.add_option("--vmax", dest="vmax", action="store", type="float", default=5.0, help="vmax [+5.0]")
    parser.add_option("--pause", dest="pause", action="store", type="float", default=0.05, help="plt.pause seconds [0.05]")
    parser.add_option("-v", "--verbose", dest="verbose", action="store_true", default=False, help="verbose;")

    (opt, args) = parser.parse_args()

    ch = None if opt.channel == "all" else int(opt.channel)

    main(
        grid=opt.grid,
        nch=int(opt.nch),
        channel=ch,
        hmin=int(opt.hmin),
        hmax=int(opt.hmax),
        vmin=float(opt.vmin),
        vmax=float(opt.vmax),
        update_pause_s=float(opt.pause),
        verbose=opt.verbose,
    )

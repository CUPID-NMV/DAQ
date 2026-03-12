#!/usr/bin/env python3
#
# I. Abritta and G. Mazzitelli March 2022
# Middelware online reconstruction
# Modify by ... in date ...
#
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

# ⬇️ IMPORTANTISSIMO: evita che la finestra venga "alzata" ad ogni update
mpl.rcParams["figure.raise_window"] = False

import matplotlib.pyplot as plt


import numpy as np

import midas
import midas.client

import wclib as wc


def setup_waveform_grid(fig, pmt, grid=False):
    """
    Create axes and line objects once.
    Returns: axes(list), lines(list)
    """
    # layout: 2 columns, ceil(pmt/2) rows (like your plot_waveform_h)
    import math
    nrows = math.ceil(pmt / 2)
    ncols = 2 if pmt > 1 else 1

    axes = fig.subplots(nrows=nrows, ncols=ncols, squeeze=False)
    axes_flat = axes.ravel()

    lines = []
    for i, ax in enumerate(axes_flat):
        # create a line even for unused axes to keep indexing simple
        (ln,) = ax.plot([], [], linewidth=1.0)
        lines.append(ln)

        if grid:
            ax.grid(True, alpha=0.3)

        # Hide unused axes if pmt is odd and last subplot unused
        if i >= pmt:
            ax.set_visible(False)

    # Try to avoid “focus stealing” behavior on some WMs:
    try:
       from PyQt5 import QtCore
       mgr = plt.get_current_fig_manager()
       mgr.window.setAttribute(QtCore.Qt.WA_ShowWithoutActivating, True)
    except Exception:
       pass

    return axes_flat, lines


def update_waveform_grid(axes, lines, waveform, lenw, pmt, smin, emax, vmin, vmax):
    """
    Update existing Line2D data instead of recreating subplots each time.
    """
    # x-axis
    t = np.arange(lenw, dtype=float)

    # clamp indices
    smin = max(0, int(smin))
    emax = min(int(emax), lenw)
    if emax <= smin:
        smin, emax = 0, lenw

    x = t[smin:emax]

    for ipmt in range(pmt):
        ax = axes[ipmt]
        ln = lines[ipmt]

        y = waveform[ipmt][smin:emax]
        ln.set_data(x, y)

        ax.set_xlim(float(x[0]), float(x[-1]) if len(x) > 1 else float(x[0]) + 1.0)

        # Apply y-limits only if user set something different
        if vmax != 4096 or vmin != 0:
            ax.set_ylim(vmin, vmax)
        else:
            # autoscale y for nicer view when default limits used
            ax.relim()
            ax.autoscale_view(scalex=False, scaley=True)

        ax.set_ylabel(f"ch{ipmt} [mV]")

    # If there are visible axes beyond pmt (when pmt < nrows*ncols), keep them hidden
    for j in range(pmt, len(axes)):
        if axes[j].get_visible():
            axes[j].set_visible(False)


def main(grid=False, pmt_n=8, hmin=0, hmax=1024, vmin=0, vmax=4096, verbose=False):
    event_before = 0

    client = midas.client.MidasClient("db_display")
    buffer_handle = client.open_event_buffer("SYSTEM", None, 1000000000)

    request_id = client.register_event_request(
        buffer_handle,
        sampling_type=midas.GET_RECENT
    )

    # Drop any events that were in the buffer before we started
    try:
        midas.bm_skip_event(buffer_handle)
    except AttributeError:
        pass

    plt.ion()
    fig = plt.figure(figsize=(12, 6), facecolor="#DEDEDE")
    try:
       from PyQt5 import QtCore
       mgr = plt.get_current_fig_manager()
       mgr.window.setAttribute(QtCore.Qt.WA_ShowWithoutActivating, True)
    except Exception:
       pass

    axes, lines = setup_waveform_grid(fig, pmt=pmt_n, grid=grid)

    # Show the window once (important for some backends)
    try:
        fig.show()
    except Exception:
        pass

    print("Events display running..., Ctrl-C to stop")

    while True:
        try:
            event = client.receive_event(buffer_handle, async_flag=False)
            if event is None:
                continue

            if event.header.is_midas_internal_event():
                if verbose:
                    print("Saw a special event")
                continue

            bank_names = ", ".join(b.name for b in event.banks.values())
            event_number = event.header.serial_number
            event_time = datetime.fromtimestamp(event.header.timestamp).strftime("%Y-%m-%d %H:%M:%S")

            # you read these but don't use corrected; leaving as-is
            _corrected = client.odb_get("/Configurations/DRS4Correction")
            channels_offset = client.odb_get("/Configurations/DigitizerOffset")

            if verbose:
                print(f"Event # {event_number} type ID {event.header.event_id} banks {bank_names}")
                print(f"Timestamp {event.header.timestamp} banks {bank_names}")
                print(f"{event_time}, banks {bank_names}")

            waveform = None
            lenw = None

            for bank_name, bank in event.banks.items():
                # NOTE: cy is not defined in your script; keeping this block disabled
                if bank_name == "INPT":
                    # slow = cy.daq_slow2array(bank)
                    # if slow is None:
                    #     break
                    pass

                if bank_name == "DGH0":
                    waveform_header = wc.daq_dgz_full2header(bank, verbose=False)
                    if verbose:
                        print(waveform_header)

                    waveform = wc.daq_dgz_full2array(
                        event.banks["DIG0"],
                        waveform_header,
                        verbose=False,
                        ch_offset=channels_offset,
                    )
                    lenw = waveform_header[2][0]
                    break  # we got what we need

            if waveform is not None and lenw is not None:
                update_waveform_grid(
                    axes=axes,
                    lines=lines,
                    waveform=waveform,
                    lenw=lenw,
                    pmt=pmt_n,
                    smin=hmin,
                    emax=hmax,
                    vmin=vmin,
                    vmax=vmax,
                )

                fig.suptitle(f"Event: {event_number:d} at {event_time:s}")

                # Draw/update without recreating the window/figure
                fig.canvas.draw_idle()

            # terminal status (same line)
            print(f"Data rate: {float(event_number - event_before):.2f} Hz", end="\r", flush=True)
            event_before = event_number

            # Keep GUI responsive and enforce ~1 Hz update
            plt.pause(1.0)

            client.communicate(10)

        except KeyboardInterrupt:
            client.deregister_event_request(buffer_handle, request_id)
            client.disconnect()
            print("\nBye, bye...")
            sys.exit(0)


if __name__ == "__main__":
    DEFAULT_PMT_VIEW = "8"
    DEFAULT_HMIN_VALUE = "0"
    DEFAULT_HMAX_VALUE = "1024"
    DEFAULT_VMIN_VALUE = "0"
    DEFAULT_VMAX_VALUE = "4096"

    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog\t ")
    parser.add_option("-g", "--grid", dest="grid", action="store_true", default=False, help="grid;")
    parser.add_option("-s", "--hmin", dest="hmin", action="store", type="string", default=DEFAULT_HMIN_VALUE,
                      help=f"hmin [{DEFAULT_HMIN_VALUE}]")
    parser.add_option("-e", "--hmax", dest="hmax", action="store", type="string", default=DEFAULT_HMAX_VALUE,
                      help=f"hmax [{DEFAULT_HMAX_VALUE}]")
    parser.add_option("-b", "--vmin", dest="vmin", action="store", type="string", default=DEFAULT_VMIN_VALUE,
                      help=f"vmin [{DEFAULT_VMIN_VALUE}]")
    parser.add_option("-t", "--vmax", dest="vmax", action="store", type="string", default=DEFAULT_VMAX_VALUE,
                      help=f"vmax [{DEFAULT_VMAX_VALUE}]")
    parser.add_option("-c", "--channel", dest="channel", action="store", type="string", default=DEFAULT_PMT_VIEW,
                      help=f"channel to view [{DEFAULT_PMT_VIEW}]")
    parser.add_option("-v", "--verbose", dest="verbose", action="store_true", default=False, help="verbose output;")

    (options, args) = parser.parse_args()

    main(
        grid=options.grid,
        pmt_n=int(options.channel),
        hmin=int(options.hmin),
        hmax=int(options.hmax),
        vmin=int(options.vmin),
        vmax=int(options.vmax),
        verbose=options.verbose,
    )


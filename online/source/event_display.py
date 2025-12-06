#!/usr/bin/env python3
#
# I. Abritta and G. Mazzitelli March 2022
# Middelware online recostruction 
# Modify by ... in date ...
#
from matplotlib import pyplot as plt
import numpy as np
import os
from datetime import datetime
#import pre_reconstruction as pr
import time

import midas
import midas.client
import sys

import wclib as wc




def plot_waveform(waveform, lenw, pmt, event_number, event_time):
    import numpy as np
    
    t = np.linspace(0,lenw, lenw)
    for ipmt in range(pmt):
        plt.subplot(pmt, 1, ipmt+1)
        plt.plot(t, waveform[ipmt])
        plt.ylabel('ch{:d} [mV]'.format(ipmt))

    return

def plot_waveform_h(waveform, lenw, pmt, smin, emax, bmin, tmax, event_number, event_time):
    import numpy as np
    import math
    npx = math.ceil(pmt/2)

    t = np.linspace(0,lenw, lenw)
    for ipmt in range(pmt):
        plt.subplot(npx, 2, ipmt+1)
        plt.plot(t[smin:emax], waveform[ipmt][smin:emax])
        if tmax!=4096 or bmin!=0: 
            plt.ylim(bmin, tmax)
        plt.ylabel('ch{:d} [mV]'.format(ipmt))

    return

    
def main(grid=False, pmt_n=8, hmin=0, hmax=1024, vmin=0, vmax=4096, verbose=False):
    # Create our client
    client = midas.client.MidasClient("db_display")
    
    buffer_handle = client.open_event_buffer("SYSTEM", None, 1000000000)

    request_id = client.register_event_request(
       buffer_handle,
       sampling_type = midas.GET_RECENT
    )

    # Drop any events that were in the buffer before we started
    try:
        midas.bm_skip_event(buffer_handle)
    except AttributeError:
    # If your python midas module doesn’t expose bm_skip_event, just ignore
        pass

    #request_id = client.register_event_request(buffer_handle, sampling_type = 2)
    
    plt.ion()
    fig = plt.figure(figsize=(12,6), facecolor='#DEDEDE')

    print("Events display running..., Crtl-C to stop")
    
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
            event_time = datetime.fromtimestamp(event.header.timestamp).strftime('%Y-%m-%d %H:%M:%S')
            corrected = client.odb_get("/Configurations/DRS4Correction")
            channels_offset = client.odb_get("/Configurations/DigitizerOffset")

            if verbose:
                print("Event # %s of type ID %s contains banks %s" % (event_number, event.header.event_id, bank_names))
                print("Received event with timestamp %s containing banks %s" % (event.header.timestamp, bank_names))
                print("%s, banks %s" % (event_time, bank_names))

            #plt.ion() #sovrapone i grafici nella stessa finestra
            
            for bank_name, bank in event.banks.items():
		
                if bank_name=='INPT':
                    slow = cy.daq_slow2array(bank) 
                    if slow is None:
                    	break
		
                if bank_name=='DGH0': 
                    plt.clf()
                    waveform_header = wc.daq_dgz_full2header(bank, verbose=False)
                    if verbose: print (waveform_header)
                    waveform = wc.daq_dgz_full2array(event.banks['DIG0'], waveform_header, verbose=False, ch_offset=channels_offset)
                    lenw = waveform_header[2][0]
                    

                    plot_waveform_h(waveform, lenw, pmt_n, hmin, hmax, vmin, vmax, event_number, event_time)

            fig.suptitle ("Event: {:d} at {:s}".format(event_number, event_time))
            fig.canvas.draw()
            fig.canvas.flush_events()
            time.sleep(1)
            client.communicate(10)
        except KeyboardInterrupt:
            client.deregister_event_request(buffer_handle, request_id)
            client.disconnect()
            print ("\nBye, bye...")
            sys.exit()

    
    
if __name__ == "__main__":
    DEFAULT_PMT_VIEW   = '8'
    DEFAULT_HMIN_VALUE  = '0'
    DEFAULT_HMAX_VALUE  = '1024'
    DEFAULT_VMIN_VALUE  = '0'
    DEFAULT_VMAX_VALUE  = '4096'
    from optparse import OptionParser
    parser = OptionParser(usage='usage: %prog\t ')
    parser.add_option('-g','--grid', dest='grid', action="store_true", default=False, help='grid;');
    parser.add_option('-s','--hmin', dest='hmin', action="store", type="string", default=DEFAULT_HMIN_VALUE, help='hmin [{:s}]'.format(DEFAULT_HMIN_VALUE));
    parser.add_option('-e','--hmax', dest='hmax', action="store", type="string", default=DEFAULT_HMAX_VALUE, help='hmax,[{:s}]'.format(DEFAULT_HMAX_VALUE));
    parser.add_option('-b','--vmin', dest='vmin', action="store", type="string", default=DEFAULT_VMIN_VALUE, help='vmin [{:s}]'.format(DEFAULT_VMIN_VALUE));
    parser.add_option('-t','--vmax', dest='vmax', action="store", type="string", default=DEFAULT_VMAX_VALUE, help='vmax,[{:s}]'.format(DEFAULT_VMAX_VALUE));
    parser.add_option('-c','--channel', dest='channel', action="store", type="string", default=DEFAULT_PMT_VIEW, help='channel to view [{:s}]'.format(DEFAULT_PMT_VIEW));
#    parser.add_option('-l','--pmt', dest='pmt', action="store", type="string", default=DEFAULT_PMT_FAST_VALUE, help='show X PMT, where X is the number of channel');		#a bit broken function (do not use it unless you accept risks)
#    parser.add_option('-y','--y0', dest='y0', action="store", type="string", default=DEFAULT_FRAME_VALUE, help='green frame (pixel) = ' + DEFAULT_FRAME_VALUE);
    parser.add_option('-v','--verbose', dest='verbose', action="store_true", default=False, help='verbose output;');
    (options, args) = parser.parse_args()
    main(grid=options.grid, pmt_n=int(options.channel), hmin=int(options.hmin), hmax=int(options.hmax), vmin=int(options.vmin), vmax=int(options.vmax), verbose=options.verbose)
# main()



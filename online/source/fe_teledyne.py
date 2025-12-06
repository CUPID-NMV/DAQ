#!/usr/bin/env python3
"""
Example of a basic midas frontend that has one periodic equipment.

See `examples/multi_frontend.py` for an example that uses more
features (frontend index, polled equipment, ODB settings etc). 
"""

import midas
import midas.frontend
import midas.event
import numpy as np
import TeledyneLeCroyPy
import time
import json
from datetime import datetime

verbose=False

def json_default(o):
    if isinstance(o, datetime):
        return o.isoformat()
    return str(o)  # fallback generale, se mai servisse

class MyPeriodicEquipment(midas.frontend.EquipmentBase):
    """
    We define an "equipment" for each logically distinct task that this frontend
    performs. For example, you may have one equipment for reading data from a
    device and sending it to a midas buffer, and another equipment that updates
    summary statistics every 10s.
    
    Each equipment class you define should inherit from 
    `midas.frontend.EquipmentBase`, and should define a `readout_func` function.
    If you're creating a "polled" equipment (rather than a periodic one), you
    should also define a `poll_func` function in addition to `readout_func`.
    """
    def __init__(self, client):
        # The name of our equipment. This name will be used on the midas status
        # page, and our info will appear in /Equipment/MyPeriodicEquipment in
        # the ODB.
        equip_name = "TeledyneLeCroyPy"
        
        # Define the "common" settings of a frontend. These will appear in
        # /Equipment/MyPeriodicEquipment/Common. The values you set here are
        # only used the very first time this frontend/equipment runs; after 
        # that the ODB settings are used.
        default_common = midas.frontend.InitialEquipmentCommon()
        default_common.equip_type = midas.EQ_PERIODIC
        default_common.buffer_name = "SYSTEM"
        default_common.trigger_mask = 0
        default_common.event_id = 2    # ricodasi di mettere qqiello giusto
        default_common.period_ms = 100 # WARNING va settato in base al sempling massimo che si vuole ottenere dall'osclloscopio
        default_common.read_when = midas.RO_ALWAYS
        default_common.log_history = 0
        
        # You MUST call midas.frontend.EquipmentBase.__init__ in your equipment's __init__ method!
        midas.frontend.EquipmentBase.__init__(self, client, equip_name, default_common)
        scope_ip=client.odb_get("/Equipment/{:}/Variables/{:}".format(equip_name, "scope_ip"))
        scope_timeout_sec=client.odb_get("/Equipment/{:}/Variables/{:}".format(equip_name, "scope_timeout_sec"))
        self.scope_number_of_channels=client.odb_get("/Equipment/{:}/Variables/{:}".format(equip_name, "scope_number_of_channels"))
        start = time.time()
        if verbose: 
            print('TCPIP0::{:s}::inst0::INSTR'.format(scope_ip))
            print("OK: connentcig to ", scope_ip)
        while True:
            try:
                self.o = TeledyneLeCroyPy.LeCroyWaveRunner('TCPIP0::{:s}::inst0::INSTR'.format(scope_ip)) 
                break
            except:
                pass

            end = time.time()
            if int(end-start)>=scope_timeout_sec:
                print("ERROR: connection timeout")
            else:
               time.sleep(0.5)
               if verbose: print("--> waiting for connection... "+str(int(end-start))+"/10 s", end="\r")
        if verbose: 
            print("OK: connected", self.o.idn) # Print e.g. LECROY,WAVERUNNER9254M,LCRY4751N40408,9.2.0



	# client.odb_get("/Equipment/{:}/Variables/{:}".format(equip_name, "scope_ip"))
        # client.odb_set("/Equipment/{:}/Variables/{:}".format(equip_name, "Voltage"), voltage)

    def readout_func(self):
        """
        For a periodic equipment, this function will be called periodically
        (every 100ms in this case). It should return either a `midas.event.Event`
        or None (if we shouldn't write an event).
        """
        
        # In this example, we just make a simple event with one bank.
        event = midas.event.Event()
        
        # Create a bank (called "MYBK") which in this case will store 8 ints.
        # data can be a list, a tuple or a numpy array.
        # If performance is a strong factor (and you have large bank sizes), 
        # you should use a numpy array instead of raw python lists. In
        # that case you would have `data = numpy.ndarray(8, numpy.int32)`
        # and then fill the ndarray as desired. The correct numpy data type
        # for each midas TID_xxx type is shown in the `midas.tid_np_formats`
        # dict.
        # print(self.counts)
        self.o.wait_for_single_trigger() # Halt the execution until there is a trigger.
        for channel in range(1, self.scope_number_of_channels+1):
            data = self.o.get_waveform(n_channel=channel)
            if verbose:
                print(channel, type(data['wavedesc']), data['wavedesc'])
                print("data:", channel, type(data['waveforms'][0]['Amplitude (V)']), data['waveforms'][0]['Amplitude (V)'])
                
            wavedesc = data['wavedesc']  # il tuo dict con TRIGGER_TIME ecc.
            json_str = json.dumps(wavedesc, default=json_default)
            json_bytes = json_str.encode("utf-8")

            event.create_bank("HSCO", midas.TID_BYTE, json_bytes)
            event.create_bank("DSCO", midas.TID_DOUBLE, data['waveforms'][0]['Amplitude (V)'])
        
        
        self.set_status("Running")
        return event

class MyFrontend(midas.frontend.FrontendBase):
    """
    A frontend contains a collection of equipment.
    You can access self.client to access the ODB etc (see `midas.client.MidasClient`).
    """
    def __init__(self):
        # You must call __init__ from the base class.
        midas.frontend.FrontendBase.__init__(self, "scope_signal")
        
        # You can add equipment at any time before you call `run()`, but doing
        # it in __init__() seems logical.
        self.add_equipment(MyPeriodicEquipment(self.client))
        
    def begin_of_run(self, run_number):
        """
        This function will be called at the beginning of the run.
        You don't have to define it, but you probably should.
        You can access individual equipment classes through the `self.equipment`
        dict if needed.
        """
        self.set_all_equipment_status("Running", "greenLight")
        self.client.msg("Frontend has seen start of run number %d" % run_number)
        return midas.status_codes["SUCCESS"]
        
    def end_of_run(self, run_number):
        self.set_all_equipment_status("Finished", "greenLight")
        self.client.msg("Frontend has seen end of run number %d" % run_number)
        return midas.status_codes["SUCCESS"]
    
    def frontend_exit(self):
        """
        Most people won't need to define this function, but you can use
        it for final cleanup if needed.
        """
        print("Goodbye from user code!")
        
if __name__ == "__main__":
    # The main executable is very simple - just create the frontend object,
    # and call run() on it.
    with MyFrontend() as my_fe:
        my_fe.run()

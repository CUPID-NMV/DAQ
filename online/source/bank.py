# === Import principali ===
import numpy as np
import matplotlib.pyplot as plt
import os
import midas.file_reader
from datetime import datetime
import wclib as wc
import warnings

verbose = True
DEBUG   = False
run     = int(input("run number [int] "))

outplot = True # fa i plot

path    = '/home/daq/data/'

mfile = wc.open_mid(run=run, path=path, cloud=False, tag='', verbose=verbose)

# esempio lettura informazioni dall'odb  #######
odb = wc.get_bor_odb(mfile)

try:
    Run_description   = odb.data['Experiment']['Run Parameters']['Run description']
    print('Run description:', Run_description)
except:
    print('WARNING: no run description')

###############################################
# lettura equipment nel file #######

for event in mfile:
    if event.header.is_midas_internal_event():
        print("Saw a special event")
        continue

    bank_names = ", ".join(b.name for b in event.banks.values())
    event_number = event.header.serial_number
    event_time = datetime.fromtimestamp(event.header.timestamp).strftime('%Y-%m-%d %H:%M:%S')
    if verbose or event_number % 1000==0:
        print("Event # %s of type ID %s contains banks %s" % (event_number, event.header.event_id, bank_names))
        print("Received event with timestamp %s containing banks %s" % (event.header.timestamp, bank_names))
        print("%s, banks %s" % (datetime.utcfromtimestamp(event.header.timestamp).strftime('%Y-%m-%d %H:%M:%S'), bank_names))
    for bank_name, bank in event.banks.items():
        if ('DGH0' in bank_name): # PMTs wavform 
            print('DGH0')
    if DEBUG:        
        if event.header.serial_number == 20: # si ferm dopo i primi 20 eventi
            break
            

print('DONE')

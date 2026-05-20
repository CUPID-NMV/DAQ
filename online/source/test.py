import TeledyneLeCroyPy

o = TeledyneLeCroyPy.LeCroyWaveRunner('TCPIP0::192.168.99.109::inst0::INSTR')

print(o.idn) # Prings e.g. LECROY,WAVERUNNER9254M,LCRY4751N40408,9.2.0

print('Waiting for trigger...')
o.wait_for_single_trigger() # Halt the execution until there is a trigger.

data = o.get_waveform(n_channel=1)

print(data['waveforms'])

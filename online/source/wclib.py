class dgtz_header:      # very simple class for the dgtz header
    def __init__(self, a):
        self.ntriggers           = a[0]
        self.nchannels           = a[1]
        self.nsamples            = a[2]
        self.vertical_resulution = a[3]
        self.sampling_rate       = a[4]
        self.offsets             = a[5]
        self.TTT                 = a[6]
        self.SIC                 = a[7]
        if len(a)>8:
            self.nBoards         = a[8]
            self.boardNames      = a[9]
        else:
            self.nBoards        = 1
            self.boardNames     = [1742]
            print('WARNING: You are using an older version of the data bank. The analysis of the digitizers might be incomplete.')

        self.itemDict = {}
        self.itemDict["0"] = self.ntriggers
        self.itemDict["1"] = self.nchannels
        self.itemDict["2"] = self.nsamples
        self.itemDict["3"] = self.vertical_resulution
        self.itemDict["4"] = self.sampling_rate
        self.itemDict["5"] = self.offsets
        self.itemDict["6"] = self.TTT
        self.itemDict["7"] = self.SIC
        self.itemDict["8"] = self.nBoards
        self.itemDict["9"] = self.boardNames
    
    def __getitem__(self, index):
        return self.itemDict[str(int(index))]


def daq_dgz_full2header(bank, verbose=False):
    # v0.1 full PMT recostruction
    import numpy as np
    nboard              = bank.data[0]
    full_buffer_size    = len(bank.data)
    name_board          = np.empty([nboard], dtype=int)
    number_samples      = np.empty([nboard], dtype=int)
    number_channels     = np.empty([nboard], dtype=int)
    number_events       = np.empty([nboard], dtype=int)
    vertical_resulution = np.empty([nboard], dtype=int)
    sampling_rate       = np.empty([nboard], dtype=int)
    channels_offset     = []
    channels_ttt        = []
    channels_SIC        = []
    if verbose: print("Number of board: {:d}".format(nboard))
    ich=0
    for iboard in range(nboard): ######### cicle over the boards
        ich+=1
        name_board[iboard]          = bank.data[ich]
        ich+=1  
        number_samples[iboard]      = bank.data[ich]
        ich+=1  
        number_channels[iboard]     = bank.data[ich]
        ich+=1  
        number_events[iboard]       = bank.data[ich]
        ich+=1  
        vertical_resulution[iboard] = bank.data[ich]
        ich+=1  
        sampling_rate[iboard]       = bank.data[ich]

        if verbose:
            print ("board: {:d}, name_board: {:d}, number_samples: {:d}, number_channels: {:d}, number_events: {:d}, vertical_resulution: {:d}, sampling_rate: {:d}".format( 
                   iboard, name_board[iboard], number_samples[iboard], number_channels[iboard], number_events[iboard], vertical_resulution[iboard], sampling_rate[iboard]))
        
        ######### Channels offset reading:
        channels_offset_tmp = np.empty([number_channels[iboard]], dtype=int)
        for ichannels in range(number_channels[iboard]):
            ich+=1
            channels_offset_tmp[ichannels] = bank.data[ich]
        if verbose:
            print ("channels_offset: ", channels_offset_tmp, flush=True)
        channels_offset.append(channels_offset_tmp)
        
        ######### TTT reading:
        channels_ttt_tmp = np.empty(number_events[iboard], dtype=int)
        for ttt in range(number_events[iboard]):
            ich+=1
            channels_ttt_tmp[ttt] = bank.data[ich]
        if verbose:
            print ("channels_ttt: ", channels_ttt)
        channels_ttt.append(channels_ttt_tmp)
        
        ######### Start Index Cell reading:  
        if name_board[iboard] == 1742:   
            channels_SIC_tmp = np.empty(number_events[iboard], dtype=int)
            for sic in range(number_events[iboard]):
                ich+=1
                channels_SIC_tmp[sic] = bank.data[ich]
            channels_SIC.append(channels_SIC_tmp)
                
    full_header = dgtz_header([number_events, number_channels, number_samples, vertical_resulution, 
                              sampling_rate, channels_offset, channels_ttt, channels_SIC, nboard, name_board])
    return full_header


def daq_dgz_full2array(bank, header, verbose=False, ch_offset=[]):

    if verbose: print("There are {} boards, and they are {}".format(header.nBoards, header.boardNames))

    data_offset = 0
    channels_to_correct = 8 # FOR NOW WE CORRECT ONLY THE FIRST 8 CHANNELS

    waveform = []

    for idigi,digitizer in enumerate(header.boardNames):

        ## Acquiring the "fast digitizer" data
        if str(digitizer) == '1742':  

            number_events   = header[0][idigi]
            number_channels = header[1][idigi]
            number_samples  = header[2][idigi]
            SIC = header.SIC
            #to_correct=[]
            
            #if not corrected:
            #    for ch in range(channels_to_correct):
            #        if ch_offset[ch]<-0.25 and ch_offset[ch]>-0.35:
            #            to_correct.append(ch)
            
            #    if number_events!=len(SIC[0]):       ## Check if the start index cell passed are right
            #        raise myError("Number of events does not match")
            
            for ievent in range(number_events):       
                for ichannels in range(number_channels):
                    if verbose:
                        print ("data_offset, data_offset+number_samples",
                            data_offset, data_offset+number_samples)
                        print(bank.data[data_offset:data_offset+number_samples])

                    waveform.append(bank.data[data_offset:data_offset+number_samples])
                    data_offset += number_samples

            #if not corrected:              ## Correcting the wavefoms (only the ones with offset at -0.3 of first 8 channels)
            #    waveform = correct_waveforms(waveform, SIC[0], number_channels, to_correct=to_correct, tag=tag)
            
            if verbose:
                print(number_channels, number_events, number_channels)

        else:
            raise myError("You seem to be trying to use a new digitizer model. You need to update the cygno libs for that.")

    return waveform

def mid_file(run, tag='WC', cloud=False, verbose=False):
    if cloud:
        BASE_URL  = BUCKET_REST_PATH+'cygno-data/'
        f = BASE_URL+(tag+'/run%05d.mid.gz' % run)
    else:
        f = ('/run%05d.mid.gz' % run)
    print(f)
    return f

def open_mid(run, path='/tmp/',  cloud=True, tag='WC', verbose=False):
    import midas.file_reader
    import os
    fname = mid_file(run, tag=tag, cloud=cloud, verbose=verbose)
    if verbose: print(fname)
    if not cloud:
        if os.path.exists(path+tag+fname):
            f = midas.file_reader.MidasFile(path+tag+fname)
        else:
            raise myError("openFileError: "+path+tag+fname+" do not exist") 
    else:
        filetmp = cmd.cache_file(fname, cachedir=path, verbose=verbose)
        f = midas.file_reader.MidasFile(filetmp)  
    return f

def get_bor_odb(mfile): # function to acquire the begin of run ODB entries from the midas file
    try:
        odb = mfile.get_bor_odb_dump()
    except:
        myError("No begin-of-run ODB dump found")
    
    mfile.jump_to_start()
    return odb


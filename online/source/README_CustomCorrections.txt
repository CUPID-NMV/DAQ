# Update of the CUPID-WC DAQ system with an improved wavedumping method

The `cupid_fe_soloV1742_v2.cxx` file represents the core source code of the **CUPID-WC DAQ** system. This version is a direct update of `cupid_fe_soloV1742_v2_NoCustomCorr.cxx`.
This document describes the specific modifications implemented in the new version, outlining their technical nature and the motivations behind them.


## Analysis of Default Corrections and Optimization

In `cupid_fe_soloV1742_v2_NoCustomCorr.cxx`, the waveforms are acquired by means of the default wavedump method provided by CAEN that, provided the flag `/Configurations/DRS4Correction` is set to `Yes`, consists in applying 3 different corrections to the waveforms (implemented as in the `ApplyDataCorrection` method defined in <https://github.com/cjpl/caen-suite/blob/master/WaveDump/src/X742CorrectionRoutines.c>):

* **`cellCorrection`**:    
    An offset value is subtracted from each cell (1024 per channel). This value is not uniform across different waveforms for a given cell, as it depends on the starting cell of the signal, which varies with each event. Additionally, `cellCorrection` includes a **`PeakCorrection`** (detailed below).

* **`nsampleCorrection`**:  
    It represents an offset value subtracted from every cell. Unlike the previous correction, cells of the same channel group sharing the same index across different waveforms receive an identical correction.

* **`timeCorrection`**:  
    Since the time interval is not constant from cell to cell, this correction is applied to eliminate these discrepancies and prevent distortions along the time axis.

As mentioned above, `cellCorrection` includes a `PeakCorrection` method: it searches for simultaneous spikes across all channels in a group and it removes these spurious signals.
The default `PeakCorrection` method is defined in <https://github.com/cjpl/caen-suite/blob/master/WaveDump/src/X742CorrectionRoutines.c>, and it identifies a spike in a cell if there is a signal difference of at least 30 units relative to that of the adjacent cells. If this occurs for all 8 channels (at the same cell index), the correction is applied (only to the affected channels) by removing the spike.
After applying these corrections, about the 3-5% of the acquired events still present anomalous simultaneous spikes in all channels.
Therefore, I updated the `PeakCorrection` algorithm by changing the identification criteria of simultaneous spikes: the signal difference with respect to the adjacent cells must be of at least 20 units (instead of 30), and a spike must be found in the same cell for at least 5 channels (instead of 8).
The application of this looser conditions resulted in the total absence of residual simultaneous peaks in the acquired events.

## Implementation Details
The custom correction described above is implemented in the `cupid_fe_soloV1742_v2.cxx` code as detailed below.

### Definition of custom methods
Since the custom corrections differ from the default ones, the `ApplyDataCorrection` and `PeakCorrection` methods must be redefined in the code:

```cpp
    void CustomPeakCorrection(CAEN_DGTZ_X742_GROUP_t* dataout);

    void ApplyCustomDataCorrection(CAEN_DGTZ_DRS4Correction_t* CTable, CAEN_DGTZ_DRS4Frequency_t frequency, int CorrectionLevelMask,    CAEN_DGTZ_X742_GROUP_t* data);
```
In particular, the `ApplyCustomDataCorrection` method is identical to `ApplyDataCorrection`, and `CustomPeakCorrection` only differs from `PeakCorrection` in the identification criteria of simultaneous spikes, as explained above.

### Enabling custom corrections
The application of corrections (default or custom ones) is set by the following boolean variables:
* **`enable_corrections`**:    
    It's value depends on the setting of the `/Configurations/DRS4Correction` flag (wether `Yes` or `No`):
    ```cpp
        bool enable_corrections;
        size = 4 * sizeof(bool);
        db_get_value(hDB, 0, "/Configurations/DRS4Correction", &enable_corrections, &size, TID_BOOL, TRUE);
    ```
    It enables default corrections.

* **`custom_corrections`**:    
    It's value depends on the setting of the `/Configurations/CustomCorrection` flag (wether `Yes` or `No`):
    ```cpp
        bool custom_corrections;
        ...
        size = 4 * sizeof(bool);
        db_get_value(hDB, 0, "/Configurations/CustomCorrection", &custom_corrections, &size, TID_BOOL, TRUE);
    ```
    It enables custom corrections.

In the `ConfigDgts` method, according to the values of `enable_corrections` and `custom_corrections`, the corrections are enabled and/or disabled as follows:
```cpp
        if (custom_corrections) { //*custom corrections
            if ((ret = CAEN_DGTZ_GetCorrectionTables(gDGTZ[i], DRS4Frequency, (void*)gTables)) != CAEN_DGTZ_Success){
                cerr << "Error in GetCorrectionTables" << endl;
                exit(EXIT_FAILURE);
            }
            if ((ret2 = CAEN_DGTZ_DisableDRS4Correction(gDGTZ[i])) != CAEN_DGTZ_Success) {
                cerr << "Error in DisableDRS4Correction" << endl;
            }
            cout << "Correction tables for custom corrections loaded successfully." << endl;
        }
        else {
            if (!enable_corrections) { //*no corrections (neither default nor custom ones)
                ret2 = CAEN_DGTZ_DisableDRS4Correction(gDGTZ[i]);
                if (ret2 != CAEN_DGTZ_Success) {
                    cerr << "Error in DisableDRS4Correction" << endl;
                }
            }
            else { //*default corrections

                if ((ret2 = CAEN_DGTZ_LoadDRS4CorrectionData(gDGTZ[i], DRS4Frequency)) != CAEN_DGTZ_Success)
                {
                    cerr << "Error in LoadDRS4Correction" << endl;
                    exit(EXIT_FAILURE);
                }
                if ((ret2 = CAEN_DGTZ_EnableDRS4Correction(gDGTZ[i])) != CAEN_DGTZ_Success)
                {
                    cerr << "Error in EnableDRS4Correction" << endl;
                    exit(EXIT_FAILURE);
                }
            }
        }
```
Note that if custom corrections are enabled the default ones get disabled regardless of the `enable_corrections` setting, in order to prevent an incorrect double correction.
If custom corrections are enabled, the correction tables are manually extracted (conversely to the case of default corrections).
The following table illustrates how the two flags interact to determine the final behavior of the code. As implemented, `custom_corrections` takes precedence over `enable_corrections`.

| `custom_corrections` | `enable_corrections` | Applied Correction |
| :---: | :---: | :---: |
| **Yes** | **Yes** | **Custom** |
| **Yes** | **No** | **Custom** |
| **No** | **Yes** | **Default** |
| **No** | **No** | **None** |

### Application of custom corrections
If the `custom_corrections` flag is enabled, custom corrections are applied to each event in the `read_dgtz` method.
This process is performed on an event-by-event basis. Within each event, the correction is applied group by group.  
In fact, the 32 channels of the digitizer are organized into 4 groups of 8 channels each, and every group requires its own specific correction parameters.
The code iterates through the groups using a dedicated loop:
```cpp
    if (custom_corrections) {
        // Iterate through each of the 4 groups (8 channels per group)
        for (int gr = 0; gr < MAX_X742_GROUP_SIZE; gr++)
        {
            // Apply corrections using the specific tables for the current group
            ApplyCustomDataCorrection(&(gTables[gr]), DRS4Frequency, 0x7, &(Evt->DataGroup[gr]));
        }
    }
```
The `0x7` parameter indicates that all the above detailed corrections (`cellCorrection`, `nsampleCorrection` and `timeCorrection`) are applied.
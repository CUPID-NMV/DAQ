#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include "midas.h"
#include "CAEN_PLULib.h"

using namespace std;

/*-- Globals -------------------------------------------------------*/
char* frontend_name = "PLU_v2495";
char* frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 3000;
INT max_event_size = 10000;
INT max_event_size_frag = 5 * 1024 * 1024;
INT event_buffer_size = 10 * 1024 * 1024;
BOOL equipment_common_overwrite = TRUE;

int gHandle = -1;
extern HNDLE hDB;

// Variabili locali per i parametri letti dall'ODB
DWORD gBaseAddress = 0;
char  gBridgeIP[32];

INT read_event(char* pevent, INT off);

/*-- Equipment Definition ------------------------------------------*/
EQUIPMENT equipment[] = {
  {"PLU_v2495",
   { 2,
     0,
     "SYSTEM",
     EQ_POLLED,
     0,
     "MIDAS",
     TRUE,
     RO_RUNNING,
     100
   },
   read_event,
  },
  { "" }
};

/*-- Frontend Init -------------------------------------------------*/
INT frontend_init() {
    INT status;
    INT size;

    // 1. Connessione al database MIDAS
    cm_get_experiment_database(&hDB, NULL);

    // 2. Lettura IP Bridge dall'ODB (Sola Lettura)
    size = sizeof(gBridgeIP);
    status = db_get_value(hDB, 0, "/Configurations/Bridge_IPAddr", gBridgeIP, &size, TID_STRING, TRUE);
    if (status != DB_SUCCESS) {
        cm_msg(MERROR, "frontend_init", "Impossibile leggere Bridge IP dall'ODB");
        return FE_ERR_HW;
    }

    // 3. Lettura Base Address dall'ODB (Sola Lettura)
    size = sizeof(gBaseAddress);
    status = db_get_value(hDB, 0, "/Equipment/PLU_v2495/Settings/BaseAddress", &gBaseAddress, &size, TID_INT, TRUE);
    if (status != DB_SUCCESS) {
        // Se non trova la chiave, usa un default di sicurezza
        gBaseAddress = 0xBB000000;
        cm_msg(MINFO, "frontend_init", "BaseAddress non trovato, uso default 0xBB000000");
    }

    // 4. Connessione Hardware tramite libreria standard CAEN_PLU
    // Nota: Passiamo l'indirizzo base come stringa esadecimale per la libreria
    char addr_str[20];
    sprintf(addr_str, "0x%08X", gBaseAddress);

    CAEN_PLU_ERROR_CODE ret = CAEN_PLU_OpenDevice2(
        CAEN_PLU_CONNECT_VME_V4718_ETH,
        gBridgeIP,
        0,
        addr_str,
        &gHandle
    );

    if (ret != CAEN_PLU_OK) {
        cm_msg(MERROR, "frontend_init", "Connessione hardware fallita! Errore: %d", ret);
        return FE_ERR_HW;
    }

    printf("Brigde IP letto: %s\n", gBridgeIP);
    printf("BaseAddress letto: 0x%08X\n", gBaseAddress);
    printf("Stringa inviata a OpenDevice: %s\n", addr_str);

    uint32_t fw;
    CAEN_PLU_ReadReg(gHandle, 0x812C, &fw);
    printf("Firmware Revision: 0x%08X\n", fw);

    return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/
INT begin_of_run(INT run_number, char* error) {
    // In modalità Logic Unit, qui di solito si resettano le FIFO o si abilita la logica
    // Esempio: Reset della scheda (se previsto dal tuo firmware specifico)
    // CAEN_PLU_WriteRegister(gHandle, 0xXXXX, 0x1); 

    cm_msg(MINFO, "begin_of_run", "Logic Unit pronta per il Run %d", run_number);
    return SUCCESS;
}

/*-- Event Readout -------------------------------------------------*/
INT read_event(char* pevent, INT off) {
    // In modalità Logic Unit "pura", qui potresti leggere registri di stato
    // o campionare scaler interni se implementati. 
    // Per ora creiamo un bank vuoto o leggiamo un registro di test.

    uint32_t* midas_data;
    uint32_t dummy_val;

    // Esempio: Leggi un registro di stato generico
    CAEN_PLU_ReadReg(gHandle, 0x812C, &dummy_val); // Revisione FW

    bk_init32(pevent);
    bk_create(pevent, "LOGI", TID_DWORD, (void**)&midas_data);

    *midas_data++ = dummy_val; // Salviamo la revisione firmware come dato di test

    bk_close(pevent, midas_data);
    return bk_size(pevent);
}

/*-- End of Run ----------------------------------------------------*/
INT end_of_run(INT run_number, char* error) {
    return SUCCESS;
}

/*-- Callbacks Obbligatorie ----------------------------------------*/
INT frontend_exit() {
    if (gHandle >= 0) CAEN_PLU_CloseDevice(gHandle);
    return SUCCESS;
}

INT poll_event(INT source, INT count, BOOL test) { return TRUE; }
INT pause_run(INT run_number, char* error) { return SUCCESS; }
INT resume_run(INT run_number, char* error) { return SUCCESS; }
INT frontend_loop() { return SUCCESS; }
INT interrupt_configure(INT cmd, INT source, POINTER_T adr) { return SUCCESS; }
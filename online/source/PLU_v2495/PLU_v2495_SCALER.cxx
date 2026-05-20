#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "midas.h"
#include "CAEN_PLULib.h"
#include <cstring>
extern "C" {
    #include "PLUscalerLib.h"
}

using namespace std;

/*-- Globals -------------------------------------------------------*/
char* frontend_name = "PLU_v2495";
char* frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 3000;
INT max_event_size = 100000; 
INT max_event_size_frag = 5 * 1024 * 1024;
INT event_buffer_size = 100000000;
BOOL equipment_common_overwrite = TRUE;

int gHandle = -1;
PLUscalerDescr gPluDescr;

INT read_event(char* pevent, INT off);

/*-- Struttura ODB -------------------------------------------------*/
typedef struct {
    INT base_address;
    char  bridge_ip[20];
} V2495_SETTINGS;

V2495_SETTINGS v2495_settings;
extern HNDLE hDB;
HNDLE hKeySettings;

#define V2495_SETTINGS_STR "\
BaseAddress = INT : 0XBB000000\n\
BridgeIP = STRING : 192.168.99.105\n\
"

EQUIPMENT equipment[] = {
  {"PLU_v2495",                // Nome dell'attrezzatura
   { 2,                        // Tipo di evento (ID)
     0,                        // Maschera dei bit
     "SYSTEM",                 // Sorgente dei dati
     EQ_POLLED,                // Modalità di lettura (Polled = interpellato ciclicamente)
     0,                        // Evento sorgente
     "MIDAS",                  // Formato del dato
     TRUE,                     // Abilitato al boot
     RO_RUNNING,               // Leggi i dati solo quando il run è "Running"
     100                       // Frequenza di polling (in millisecondi)
   },
   read_event,                 // La funzione da chiamare per leggere i dati
  },
  { "" }                       // Terminatore dell'array
};

/*-- Frontend Init -------------------------------------------------*/
INT frontend_init() {
    
    cm_get_experiment_database(&hDB, NULL); // get the global handle 
    //db_create_record(hDB, 0, "/Equipment/PLU_v2495/Settings", V2495_SETTINGS_STR);
    //db_find_key(hDB, 0, "/Equipment/PLU_v2495/Settings", &hKeySettings); // assegna il puntatore di /Equipment/V2495_PLU/Settings a hKeySettings
    //INT settings_size = sizeof(v2495_settings);
    //db_get_record(hDB, hKeySettings, &v2495_settings, &settings_size, 0); // copia i valori presenti nell'ODB

    INT addr_size = sizeof(v2495_settings.base_address);
    printf("Addr_size:%d\n",addr_size);
    db_get_value(hDB, 0, "/Equipment/PLU_v2495/Settings/BaseAddress", &v2495_settings.base_address, &addr_size, TID_INT, TRUE);

    INT ip_size = sizeof(v2495_settings.bridge_ip);
    printf("Ip_size:%d\n",ip_size);
    db_get_value(hDB, 0, "/Configurations/Bridge_IPAddr", &v2495_settings.bridge_ip, &ip_size, TID_STRING, TRUE);

    // Connessione con l'hardware
    CAEN_PLU_ERROR_CODE ret = CAEN_PLU_OpenDevice2(
        CAEN_PLU_CONNECT_VME_V4718_ETH, // connessione di tipo Ethernet
        v2495_settings.bridge_ip,       // indirizzo IP del Bridge
        0,
        "0xBB00000",                     //TODO: Indirizzo base VME 
        &gHandle                        // D'ora in poi punta alla scheda appena letta
    );

    if (ret != CAEN_PLU_OK) return FE_ERR_HW;

    // Inizializzazione Descrittore
    memset(&gPluDescr, 0, sizeof(gPluDescr)); // svuota gPluDescr
    gPluDescr.handle = gHandle;

    // ABILITAZIONE CANALI
    gPluDescr.ChEnable[0] = 0x0;        // Porta A (0-31) DISABILITATA
    gPluDescr.ChEnable[1] = 0x0;        // Porta B (32-63) DISABILITATA
    gPluDescr.ChEnable[2] = 0xFFFFFFFF; // Porta D (64-95) ABILITATA (tutti i 32 canali)
    gPluDescr.ChEnable[3] = 0x0;        // Porta E (96-127) DISABILITATA
    gPluDescr.ChEnable[4] = 0x0;        // Porta F (128-159) DISABILITATA

    // Chiamiamo db_check per VALIDARE la configurazione rispetto all'hardware reale
    // Questa funzione stamperà a video cosa trova e correggerà gPluDescr.ChEnable se necessario
    db_check(&gPluDescr);

    //uint32_t fw_rev;
    //CAEN_PLU_ReadReg(gHandle, 0x812C, &fw_rev);
    //std::cout << "Firmware Revision: 0x" << std::hex << fw_rev << std::dec << std::endl;

    return SUCCESS;
}


/*-- Begin of Run --------------------------------------------------*/
INT begin_of_run(INT run_number, char* error) {
    // 1. SINCRONIZZAZIONE: Legge i parametri aggiornati dall'ODB
    // Senza questa riga, eventuali modifiche fatte a mano su MIDAS verrebbero ignorate
    int size = sizeof(v2495_settings);
    db_get_record(hDB, hKeySettings, &v2495_settings, &size, 0);

    // 2. CONFIGURAZIONE LOGICA: Imposta come vogliamo i dati (32-bit)
    gPluDescr.TimeTag = 1;          // Abilita Timestamp
    gPluDescr.PLU_pl = 1;           // Modo esteso (consente di leggere i singoli canali)
    gPluDescr.time64_en = 0;        // 32 bit per il tempo
    gPluDescr.cnt64_en = 0;         // 32 bit per i dati canali

    // 3. HARDWARE SETUP: Applica le impostazioni alla scheda fisica
    PLUProgram(&gPluDescr);         // Scrive nei registri via VME
    PLUResetCounters(gPluDescr);    // Azzera i contatori
    PLUClearData(gPluDescr);        // Pulisce la memoria FIFO (First-in, First-out)

    // 4. START: Invia il segnale di inizio acquisizione
    // PLUSCALER_CTRL_BITSET permette di mettere a 1 singoli bit senza cambiare gli altri
    PLUWriteRegister(gPluDescr, PLUSCALER_CTRL_BITSET, 0x1);

    return SUCCESS;
}

/*-- Event Readout -------------------------------------------------*/
INT read_event(char* pevent, INT off) {
    uint32_t raw_buffer[8192];  //Buffer --> Area di memoria temporanea
    uint32_t* midas_data;       //Puntatore per scrivere i dati nel formato MIDAS
    int nw = 0;                 //Size degli eventi letti

    uint32_t event_size = CountEventSize(&gPluDescr);   //Calcola quanto spazio serve per ogni evento in base alla configurazione effettuata

    // Lettura dalla FIFO del modulo
    if (ReadEvents(gPluDescr, raw_buffer, event_size, &nw) != CAEN_PLU_OK || nw == 0) //ReadEvents preleva i dati dalla memoria FIFO
        return 0;

    bk_init32(pevent);
    bk_create(pevent, "PLU2", TID_DWORD, (void**)&midas_data);

    int pnt = 0; //pointer per la lettura --> traccia la posizione corrente all'interno del buffer
    while (pnt < nw) {
        // 1. Header (parola 0)
        uint32_t header = raw_buffer[pnt++]; //Contiene le info di sincronizzazione

        // 2. Info Word (parola 1)
        uint32_t info_word = raw_buffer[pnt++];
        uint32_t port_mask = (info_word >> 4) & 0x1F; // dice quali porte hanno inviato dati    
        // Bit 4..8 rappresentano A, B, D, E, F (>>4 fa scorrere tutti i bit a dx di 4 posti  per escludere i bit in 0,1,2,3 che non sono di interesse)
        // & 0x1F per mantenere solo i 5 bit di interesse (quelli che corrispondono alle porte)

        // 3. Timestamp (parola 2)
        uint32_t timestamp = raw_buffer[pnt++];

        // Scrittura metadati nel Bank
        *midas_data++ = port_mask;
        *midas_data++ = timestamp;

        // 4. Lettura canali: per ogni porta attiva, leggiamo i dati dei canali abilitati
        for (int p = 0; p < 5; p++) {
            if ((port_mask >> p) & 0x1) { // >>p sposta il bit della porta nell posizione 0 e 0x1 isola quel singolo bit
                // Se la porta è attiva (i.e. (port_mask >> p) & 0x1 è 1), allora leggiamo i canali abilitati su quella porta
                for (int ch = 0; ch < 32; ch++) {
                    if ((gPluDescr.ChEnable[p] >> ch) & 0x1) {
                        // Salviamo il valore del canale (tempo/conteggio)
                        *midas_data++ = raw_buffer[pnt++];
                    }
                }
            }
        }
    }

    bk_close(pevent, midas_data);
    return bk_size(pevent);
}

/*-- End of Run ----------------------------------------------------*/
INT end_of_run(INT run_number, char* error) {
    PLUWriteRegister(gPluDescr, PLUSCALER_CTRL_BITCLEAR, 0x1);
    return SUCCESS;
}

/*-- Callbacks Obbligatorie (Vuote) --------------------------------*/
INT frontend_exit() { if (gHandle >= 0) CAEN_PLU_CloseDevice(gHandle); return SUCCESS; }
INT poll_event(INT source, INT count, BOOL test) { return TRUE; }
INT pause_run(INT run_number, char* error) { return SUCCESS; }
INT resume_run(INT run_number, char* error) { return SUCCESS; }
INT frontend_loop() { return SUCCESS; }
INT interrupt_configure(INT cmd, INT source, POINTER_T adr) { return SUCCESS; }
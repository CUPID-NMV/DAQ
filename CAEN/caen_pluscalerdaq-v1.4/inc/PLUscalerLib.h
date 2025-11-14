/******************************************************************************
*
* CAEN SpA - Front End Division
* Via Vetraia, 11 - 55049 - Viareggio ITALY
* +390594388398 - www.caen.it
*
***************************************************************************//**
* \note TERMS OF USE:
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU General Public License as published by the Free Software
* Foundation. This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The user relies on the
* software, documentation and results solely at his own risk.
*
* \file     PLUscalerLib.h
* \brief    CAEN Front End - PLUscaler Library Header File
* \author   Carlo Tintori (c.tintori@caen.it)
******************************************************************************/
#ifndef __PLUSCALER_H
#define __PLUSCALER_H

#include <stdio.h>

#include <CAEN_PLULib.h>

#include "gnuplot_i.h"

/*###########################################################################*/
/*
** OS portability
*/
/*###########################################################################*/

#ifdef WIN32

    #ifndef _CRT_SECURE_NO_DEPRECATE
        #define _CRT_SECURE_NO_DEPRECATE
    #endif
	#include <conio.h>
	#include <stdio.h>
	#include <stdlib.h>
    #include <time.h>
    #include <sys/timeb.h>
    #include <process.h>
    #include <windows.h>
    #include <winsock.h>
    #include "gnuplot_i.h"
	#include <stdint.h>
    #define PATH_SEPARATOR '\\'

    // redefine the 'deprecated' functions
    #define popen  _popen
    #define pclose _pclose

    #define msleep  Sleep

#else  // LINUX

    #include <unistd.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <stdint.h>

    #define PATH_SEPARATOR '/'

    #define msleep(t)  usleep((t)*1000)
#endif

typedef int bool;
#define true  1
#define false 0

// Some Definitions
#define BUFFER_SIZE     (2048*128)    // Size of the local buffer (in bytes)
#define MAX_BLT_SIZE    (2048*128)    // Max. Size for the Block Transfer (in bytes)
#define IRQ_LEVEL       1             // Interrupt Level (1-7)

/* ###########################################################################
*  PLU MAIN Register Map (this is not complete)
*  ########################################################################### */
#define PLU_MAIN_STATUS_REG                 0x8204
#define PLU_MAIN_STATUS_REG_DATA_AVAILABLE  0x1   

/* ###########################################################################
*  PLUSCALER Register Map (this is not complete)
*  ########################################################################### */
#define PLUSCALER_CTRL              0x1000
#define PLUSCALER_CTRL_BITSET       0x1004
#define PLUSCALER_CTRL_BITCLEAR     0x1008
#define PLUSCALER_USER_FW_REV       0x100C
#define PLUSCALER_STATUS_REG        0x1010
#define PLUSCALER_SHOT              0x1014
#define PLUSCALER_CHENABLE_A        0x1020
#define PLUSCALER_CHENABLE_B        0x1024
#define PLUSCALER_CHENABLE_D        0x1028
#define PLUSCALER_CHENABLE_E        0x102C
#define PLUSCALER_CHENABLE_F        0x1034
#define PLUSCALER_CHENABLE(i)       (0x1020 + (i)*4 + (i/4)*4) //if (i<4) else (0x1034)
#define PLUSCALER_DWELL_TIME        0x1030
#define PLUSCALER_DB_ID             0x1038

#define MAIN_FIRMWARE_REVISION        0x8200
#define MAIN_FIRMWARE_MEB_USEDW       0x8208 
#define MAIN_SOFT_RESET               0x8218              

#define PLUSCALER_SHOT_TRIGGER      0x00000001
#define PLUSCALER_SHOT_CNTRES       0x00000002
#define PLUSCALER_SHOT_CLEARDATA    0x00000004
#define PLUSCALER_SHOT_RESET        0x00000008

#define PLU_IN_PORT_A    0
#define PLU_IN_PORT_B    1
#define PLU_IN_PORT_D    2
#define PLU_IN_PORT_E    3


/**************************************************************************//**
* \struct  PLUscalerDescr
* \brief   Descriptor of the PLU
******************************************************************************/
typedef struct {

	union ConnAddr {
		uint32_t  BaseAddress; char *serial_number; char *ip_addr;
	};
	int handle;
    int SerialNumber;
	int LinkType;

    // Acquisition options
    int TriggerMode;                ///< 0 = No trigger, 1 = External Only, 2 = Internal Only, 3 = Both
    int AutoReset;                  ///< 0 = Disabled, 1 = Enabled
    int TimeTag;                    ///< 0 = Disabled, 1 = Enabled
    int G1Mode;                     ///< 0 = Inhibit, 1 = Counters Reset, 2 = Test Input
    int GPortType;                  ///< 0 = NIM, 1 = TTL
	int DBInputType;                ///< 0 = NIM, 1 = TTL
	uint32_t DwellTime;            ///< Dwell time (period of the internal trigger)
	uint32_t ChEnable[5];      ///< Channel Enable Mask (Port A, B, D, E, F)
    bool SaveToFile;                 ///< 0 = No Save, 1 = Save
	bool TestClock;
	bool OfflineMode;
    int OpenPlot;                   ///< 0 = No Plot, 1 = Open gnuplot to trace one channel
    int PlotPoints;             ///< Trace time window
	int PlotChan;
	int SWTrigTime;
	int ReadingTime;
	int ports;
	bool PrintCounters;
	bool PLU_pl;
	bool mask_en;
	bool cnt64_en;
	bool time64_en;
	
} PLUscalerDescr;



/* ---------------------------------------------------------------------------
   Read one 32bit register of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUReadRegister(PLUscalerDescr des, unsigned short RegAddress, uint32_t *data);

/* ---------------------------------------------------------------------------
   Write one 32bit register of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUWriteRegister(PLUscalerDescr des, unsigned short RegAddress, uint32_t data);

/* ---------------------------------------------------------------------------
   Reset the PLUscaler
   ------------------------------------------------------------------------- */
int PLUReset(PLUscalerDescr des);

/* ---------------------------------------------------------------------------
   Clear the PLUscaler 
   ------------------------------------------------------------------------- */
int PLUClear(PLUscalerDescr des);

/* ---------------------------------------------------------------------------
   Send a SW trigger to the PLUscaler
   ------------------------------------------------------------------------- */
int PLUSoftTrigger(PLUscalerDescr des);

/* ---------------------------------------------------------------------------
   Reset the counters of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUResetCounters(PLUscalerDescr des);

int PLUClearData(PLUscalerDescr des);

/* ---------------------------------------------------------------------------
   Read one block of data
   ------------------------------------------------------------------------- */
int ReadBlock(PLUscalerDescr des, uint32_t *buffer, int MaxSize, int *nw);

/* ---------------------------------------------------------------------------
   Read one or more events from the PLUscaler
   ------------------------------------------------------------------------- */
int ReadEvents(PLUscalerDescr des, uint32_t* buffer, uint32_t event_size, int* nw);
int ReadEvents_old(PLUscalerDescr des, uint32_t *buffer, int *nw);

long get_time();
void Manual(PLUscalerDescr des);
bool chan_enabled(int i, PLUscalerDescr *des);
void PLUProgram(PLUscalerDescr *des);
void PrintCntOnScreen(PLUscalerDescr des, uint32_t *Counters, uint32_t *Counters64);
void GPlotInit(gnuplot_ctrl **plt, char *style, char *xlabel, char *ylabel);
void clearScreen();
uint32_t CountEventSize(PLUscalerDescr* des);
void db_check(PLUscalerDescr *des);

#endif
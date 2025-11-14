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
* \file     PLUscalerLib.c
* \brief    CAEN Front End - PLUscaler Library
* \author   Carlo Tintori (c.tintori@caen.it)
******************************************************************************/

#include "PLUscalerLib.h"

#define CONFIG_FILENAME  "PLUscaler_Config.txt"
/* ###########################################################################
*  Functions
*  ########################################################################### */

// channel enabled check 
bool chan_enabled(int i, PLUscalerDescr* des) {
	if (i > 160) {
		printf("index exceeded the available channels\n");
		return false;
	}
	else {
		return (des->ChEnable[i / 32] >> (i % 32) & 0x1) ? true : false;
	}
}


/* ---------------------------------------------------------------------------
   Read one 32bit register of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUReadRegister(PLUscalerDescr des, unsigned short RegAddress, uint32_t* data)
{

	return CAEN_PLU_ReadReg(des.handle, RegAddress, data);
}

/* ---------------------------------------------------------------------------
   Write one 32bit register of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUWriteRegister(PLUscalerDescr des, unsigned short RegAddress, uint32_t data)
{
	return CAEN_PLU_WriteReg(des.handle, RegAddress, data);
}

/* ---------------------------------------------------------------------------
   Reset the PLUscaler
   ------------------------------------------------------------------------- */
int PLUReset(PLUscalerDescr des)
{
	uint32_t data = PLUSCALER_SHOT_RESET;

	return CAEN_PLU_WriteReg(des.handle, PLUSCALER_SHOT, data);
}

/* ---------------------------------------------------------------------------
   Clear the PLUscaler
   ------------------------------------------------------------------------- */
int PLUClearData(PLUscalerDescr des)
{
	uint32_t data = PLUSCALER_SHOT_CLEARDATA;

	return CAEN_PLU_WriteReg(des.handle, PLUSCALER_SHOT, data);
}

/* ---------------------------------------------------------------------------
   Send a SW trigger to the PLUscaler
   ------------------------------------------------------------------------- */
int PLUSoftTrigger(PLUscalerDescr des)
{
	uint32_t data = PLUSCALER_SHOT_TRIGGER;

	return CAEN_PLU_WriteReg(des.handle, PLUSCALER_SHOT, data);

}

/* ---------------------------------------------------------------------------
   Reset the counters of the PLUscaler
   ------------------------------------------------------------------------- */
int PLUResetCounters(PLUscalerDescr des)
{
	uint32_t data = PLUSCALER_SHOT_CNTRES;

	return CAEN_PLU_WriteReg(des.handle, PLUSCALER_SHOT, data);

}

/* ---------------------------------------------------------------------------
   Read one block of data
   ------------------------------------------------------------------------- */

int ReadBlock(PLUscalerDescr des, uint32_t* buffer, int MaxSize, int* nw)
{
	int ret = 0, pnt = 0, bltnw;
	uint32_t  nw_read = 0;

	*nw = 0;

	/* make a loop of MBLT cycles until the board asserts the Bus Error to signal the end of event(s)
	(or until the buffer size has been exceeded) */
	while (pnt < MaxSize) {
		bltnw = (MAX_BLT_SIZE / 4);
		if ((pnt + bltnw) > MaxSize)  /* buffer size exceeded */
			bltnw = MaxSize - pnt;

		ret = CAEN_PLU_ReadFIFO32(des.handle, 0, bltnw, (uint32_t*)(buffer + pnt), &nw_read);

		pnt += nw_read;
		if (ret == CAEN_PLU_TERMINATED) break;
		else if (ret != CAEN_PLU_OK) return -1;
	}

	*nw = pnt;
	return 0;
}

/* ---------------------------------------------------------------------------
   Read one or more events from the PLUscaler
   ------------------------------------------------------------------------- */
int ReadEvents_old(PLUscalerDescr des, uint32_t* buffer, int* nw)
{
	int ret = 0;
	uint32_t StatusReg, header;
	uint32_t  EventSize;
	*nw = 0;

	/*
	** Wait until there are data available in the queue in case of socket connection (ethernet)
	** PLUReadRegister(des, 0, &header) will otherwise return wrong data if no events have arrived yet.
	** In case of other links,  PLUReadRegister(des, 0, &header) itself will return error in case of no data available.
	*/
	if (des.LinkType == 1) { // Ethernet link
		ret = PLUReadRegister(des, PLU_MAIN_STATUS_REG, &StatusReg);
		/* Check if event FIFO has data */
		if (!(StatusReg & PLU_MAIN_STATUS_REG_DATA_AVAILABLE)) {
			*nw = 0;
			return 0;
		}
	}

	/* read the event header from output FIFO using a single read cycle */
	ret = PLUReadRegister(des, 0, &header);

	if (ret != 0) {
		*nw = 0;
		return 0;
	}
	if (((header >> 30) & 0x01) == 0) {
		//printf("V1495 data format : %08X\n", header);
		EventSize = header & 0xFF;
	}
	else {
		//printf("PLU data format: %08X\n", header);
		EventSize = header & 0xFFF;
	}
	//printf("event size: %d\n", EventSize);
	buffer[0] = header;

	/* read the rest of the event with an exact size Block Transfer */
	ret = ReadBlock(des, buffer + 1, EventSize - 1, nw);
	//for(i=0;i<(*nw);i++) printf("Word %d: %d\n",i, buffer[i]);
	(*nw)++; // the function must return the event words, but the header is not counted yet 
	return ret;
}

/* ---------------------------------------------------------------------------
   Read one or more events from the PLUscaler
   ------------------------------------------------------------------------- */
int ReadEvents(PLUscalerDescr des, uint32_t* buffer, uint32_t event_size, int* nw)
{
	int ret = 0;
	uint32_t words_in_meb;
	uint32_t evts_in_meb = 0;
	uint32_t StatusReg;
	static int data_carry_size = 0;
	static int data_carry_start = 0;
	*nw = 0;

	// Wait until there are data available in the queue in case of socket connection (ethernet)
	// PLUReadRegister(des, 0, &header) will otherwise return wrong data if no events have arrived yet.
	// In case of other links,  PLUReadRegister(des, 0, &header) itself will return error in case of no data available.
	// 04/12/2020: This does not seem to be the case at least with the USB-bridge connection
	// The function PLUReadRegister returns 0 (no error) even when there are no data
	// as a consequence it cannot be used to probe whether the FIFO is full.
	// The readout of the MEB used words was added and used to decide whether to exit 
	// or to read a specific number of events on the basis of the (known) event size

	if (des.LinkType == 1) { // Ethernet link
		ret = PLUReadRegister(des, PLU_MAIN_STATUS_REG, &StatusReg);
		// Check if event FIFO has data 
		if (!(StatusReg & PLU_MAIN_STATUS_REG_DATA_AVAILABLE)) {
			*nw = 0;
			return 0;
		}
	}
	else {
		ret = PLUReadRegister(des, MAIN_FIRMWARE_MEB_USEDW, &words_in_meb);
		evts_in_meb = (uint32_t)(words_in_meb / event_size);
		if (evts_in_meb == 0) return 0;
	}

	/* read the full events in the MEB with an exact-size Block Transfer */
	if (data_carry_size != 0) memcpy(buffer, buffer + data_carry_start, data_carry_size);
	ret = ReadBlock(des, buffer + data_carry_size, (event_size * evts_in_meb) - (data_carry_size), nw);
	data_carry_size = (*nw + data_carry_size) % event_size; // carried data are the eccess of a single event
	data_carry_start = *nw - data_carry_size;
	*nw = *nw - data_carry_size;
	return ret;
}

/* ---------------------------------------------------------------------------
get time in milliseconds
/* ------------------------------------------------------------------------- */

long get_time() {
	uint32_t time_ms;
#ifdef WIN32
	struct _timeb timebuffer;
	_ftime(&timebuffer);
	time_ms = (uint32_t)timebuffer.time * 1000 + (uint32_t)timebuffer.millitm;
#else
	struct timeval t1;
	struct timezone tz;
	gettimeofday(&t1, &tz);
	time_ms = (t1.tv_sec) * 1000 + t1.tv_usec / 1000;
#endif
	return time_ms;
}

void PLUProgram(PLUscalerDescr* des) {
	uint32_t CtrlReg;
	int i;
	des->ports = 0;
	CtrlReg = des->GPortType << 2 | des->AutoReset << 3 | des->G1Mode << 4 | des->TimeTag << 6 |
		(des->PLU_pl && des->mask_en) << 7 | (des->PLU_pl && des->cnt64_en) << 8 | (des->PLU_pl && des->time64_en) << 9 |
		des->PLU_pl << 10 | des->TestClock << 11 | des->DBInputType << 12 |
		des->DBInputType << 13 | des->DBInputType << 14;
	PLUWriteRegister(*des, PLUSCALER_CTRL, CtrlReg);                         /* Enable Time Tag */
	PLUWriteRegister(*des, PLUSCALER_DWELL_TIME, des->DwellTime);             /* DWELL time */
	for (i = 0; i < 5; i++) {
		PLUWriteRegister(*des, PLUSCALER_CHENABLE(i), des->ChEnable[i]);      /* Enable Channels */
		if (des->ChEnable[i] != 0) des->ports = des->ports + (0x1 << i);
	}
	PLUReadRegister(*des, PLUSCALER_CTRL, &CtrlReg);
	PLUReadRegister(*des, 0x1020, &CtrlReg);
}



/* ---------------------------------------------------------------------------
Manual Controler
/* ------------------------------------------------------------------------- */

void Manual(PLUscalerDescr des)
{
	char c;
	uint32_t addr, data;
	printf("\n");
	//while (1) {
	printf("[w] Write [r] Read\n");
	c = _getch();
	if (c == 'w') {
		printf("Address = ");
		scanf("%x", &addr);
		printf("Data = ");
		scanf("%x", &data);
		PLUWriteRegister(des, (unsigned short)addr, data);
	}
	else if (c == 'r') {
		printf("Address = ");
		scanf("%x", &addr);
		PLUReadRegister(des, (unsigned short)addr, &data);
		printf("Data = %08X\n", data);
	}
	else {
		printf("invalid option\n");
	}
	printf("\n");
}

void PrintCntOnScreen(PLUscalerDescr des, uint32_t* Counters, uint32_t* Counters64) {
	int i, j;
	printf("               port A         port B         port D         port E         port F\n\n");
	for (i = 0; i < 32; i++) {
		if (des.cnt64_en && des.PLU_pl) {
			for (j = 0; j < 5; j++) {
				if (chan_enabled((j * 32) + i, &des)) printf("%15u", *(Counters64 + (i + (32 * j))));
				else printf("               ");
			}
			printf("\n");
		}
		printf("CH%4d", i);
		for (j = 0; j < 5; j++) {
			if (chan_enabled((j * 32) + i, &des)) printf("%15u", *(Counters + (i + (32 * j))));
			else printf("              *");
		}
		printf("\n");
	}
}

void GPlotInit(gnuplot_ctrl** plt, char* style, char* xlabel, char* ylabel) {
	*plt = gnuplot_init();
	gnuplot_setstyle(*plt, style);
	gnuplot_set_xlabel(*plt, xlabel);
	gnuplot_set_ylabel(*plt, ylabel);

}

void db_check(PLUscalerDescr* des) {
	int k;
	uint32_t db_id;
	char ports[5] = { "ABDEF" };
	PLUReadRegister(*des, PLUSCALER_DB_ID, &db_id);
	for (k = 0; k < 3; k++) {
		switch ((db_id >> (4 * k)) & 0x7) {
		case 0:
			printf("Daughterboard mod. A395A detected in slot %c\n", ports[k + 2]);
			break;
		case 3:
			printf("Daughterboard mod. A395D detected in slot %c. Only the first 8 channels can be used, the others will be masked\n", ports[k + 2]);
			des->ChEnable[k + 2] = des->ChEnable[k + 2] & 0x000000ff;
			break;
		default:
			printf("No daughterboard compatible with the scaler firmware detected in slot %c\n", ports[k + 2]);
			des->ChEnable[k + 2] = 0x00000000;
			break;
		}
	}
}

uint32_t CountEventSize(PLUscalerDescr* des) {
	uint32_t eventsize;
	int i, j;
	if (des->PLU_pl == 0) {
		eventsize = 1; // V1495 legacy
		if (des->TimeTag) eventsize++;
		for (i = 0; i < 5; i++) {
			for (j = 0; j < 32; j++) {
				if ((des->ChEnable[i] >> j) & 0x1) eventsize++; //1/2 words for each active counter
			}
		}
	}
	else {
		eventsize = 2; // base header size;
		if (des->TimeTag) eventsize++;
		if (des->time64_en) eventsize++;
		for (i = 0; i < 5; i++) {
			if (des->ChEnable[i]) eventsize++; // header size
			for (j = 0; j < 32; j++) {
				if ((des->ChEnable[i] >> j) & 0x1) eventsize += (1 + des->cnt64_en); //1/2 words for each active counter
			}
		}
	}
	return eventsize;
}
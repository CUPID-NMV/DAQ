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
* \file     V2495scalerLib.c
* \brief    CAEN Front End - V2495scaler Library
* \author   Carlo Tintori (c.tintori@caen.it)
******************************************************************************/

#include "config.h"

#define CONFIG_FILENAME  "V2495scaler_Config.txt"

void DefaultConfig(PLUscalerDescr *des)
{
	/* Assign default values */
	des->TriggerMode = 2;
	des->AutoReset = 0;
	des->TimeTag = 1;
	des->time64_en = false;
	des->cnt64_en = false;
	des->mask_en = false;
	des->G1Mode = 2;
	des->GPortType = 0;
	des->ChEnable[0] = 0x0;
	des->ChEnable[1] = 0x0;
	des->ChEnable[2] = 0x0;
	des->ChEnable[3] = 0x0;
	des->ChEnable[4] = 0x0;
	des->DwellTime = 1000000;
	des->SaveToFile = false;
	des->TestClock = false;
	des->OfflineMode = false;
	des->OpenPlot = 0;
	des->PlotPoints = 1000;
	des->SWTrigTime = 100;
	des->ReadingTime = 1;
	des->PlotChan = 0;
	des->PrintCounters = false;
}

int ReadConfigFile(PLUscalerDescr *des, char *cfg_file)
{
	char param[1000];
	FILE *cfg;
	cfg = fopen(cfg_file, "r");
	if (cfg == NULL) {
		printf("Config file does not exist\n");
		return -1;
	} else {
		printf("Config file opened\n");
		while (!feof(cfg)) {
			fscanf(cfg, "%s", param);
			if (param[0] == '#') {
				fgets(param, 1000, cfg); /* It's a comment => skip the line */
				continue;
			}
			else if (strcmp(param, "AutoReset") == 0) fscanf(cfg, "%d", &des->AutoReset);
			else if (strcmp(param, "TriggerMode") == 0) fscanf(cfg, "%d", &des->TriggerMode);
			else if (strcmp(param, "TimeTag") == 0) fscanf(cfg, "%d", &des->TimeTag);
			else if (strcmp(param, "Ch_Mask") == 0) fscanf(cfg, "%d", &des->mask_en);
			else if (strcmp(param, "TimeTag_64") == 0) fscanf(cfg, "%d", &des->time64_en);
			else if (strcmp(param, "Counter_64") == 0) fscanf(cfg, "%d", &des->cnt64_en);
			else if (strcmp(param, "V2495_Payload") == 0) fscanf(cfg, "%d", &des->PLU_pl);
			else if (strcmp(param, "G1Mode") == 0) fscanf(cfg, "%d", &des->G1Mode);
			else if (strcmp(param, "GPortType") == 0) fscanf(cfg, "%d", &des->GPortType);
			else if (strcmp(param, "DBInputType") == 0) fscanf(cfg, "%d", &des->DBInputType);
			else if (strcmp(param, "ChEnableA") == 0) fscanf(cfg, "%x", &des->ChEnable[0]);
			else if (strcmp(param, "ChEnableB") == 0) fscanf(cfg, "%x", &des->ChEnable[1]);
			else if (strcmp(param, "ChEnableD") == 0) fscanf(cfg, "%x", &des->ChEnable[2]);
			else if (strcmp(param, "ChEnableE") == 0) fscanf(cfg, "%x", &des->ChEnable[3]);
			else if (strcmp(param, "ChEnableF") == 0) fscanf(cfg, "%x", &des->ChEnable[4]);
			else if (strcmp(param, "DwellTime") == 0) fscanf(cfg, "%d", &des->DwellTime);
			else if (strcmp(param, "SaveToFile") == 0) fscanf(cfg, "%d", &des->SaveToFile);
			else if (strcmp(param, "TestClock") == 0) fscanf(cfg, "%d", &des->TestClock);
			else if (strcmp(param, "OpenPlot") == 0)fscanf(cfg, "%d", &des->OpenPlot);
			else if (strcmp(param, "SWTrigTime") == 0) fscanf(cfg, "%d", &des->SWTrigTime);
			else if (strcmp(param, "ReadingTime") == 0) {
				fscanf(cfg, "%d", &des->ReadingTime);
				if (des->ReadingTime < 1) des->ReadingTime = 1;
			} else if (strcmp(param, "PlotPoints") == 0) fscanf(cfg, "%d", &des->PlotPoints);		
			else if (strcmp(param, "PlotChan") == 0) fscanf(cfg, "%d", &des->PlotChan);
			else if (strcmp(param, "PrintCounters") == 0) fscanf(cfg, "%d", &des->PrintCounters);
			else printf("\a%s: invalid parameter\n", param);
		}

		fclose(cfg);
		return 0;
	}
}


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
* \file     PLUscalerDAQ.c
* \brief    CAEN Front End - PLUscaler Data Acquisition Demo
* \author   Carlo Tintori(c.tintori@caen.it); Stefano Venditti (s.venditti@caen.it);
*           Luca Colombini (l.colombini@caen.it)
******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>

#include <CAEN_PLULib.h>

#include "config.h"
#include "gnuplot_i.h"
#include "keyb.h"

#ifdef WIN32 // Windows
#include "PLUscalerLib.h"
#include <windows.h>
#include <conio.h>
#include <io.h>
#else // Linux
#include "PLUscalerLib.h"
#define _popen popen
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

char _getch() {
	char buf = 0;
	struct termios old = { 0 };
	fflush(stdout);
	if (tcgetattr(0, &old) < 0)
		perror("tcsetattr()");
	old.c_lflag &= ~ICANON;
	old.c_lflag &= ~ECHO;
	old.c_cc[VMIN] = 1;
	old.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSANOW, &old) < 0)
		perror("tcsetattr ICANON");
	if (read(0, &buf, 1) < 0)
		perror("read()");
	old.c_lflag |= ICANON;
	old.c_lflag |= ECHO;
	if (tcsetattr(0, TCSADRAIN, &old) < 0)
		perror("tcsetattr ~ICANON");
	printf("%c\n", buf);
	return buf;
}
#endif

void usage() {
	fprintf(stderr, "Usage: PLUscaler_daq [config file path](default: ./PLUScaler_Config.txt) [-h] [-c usb-direct|eth-direct|usb-V1718|usb-V3718|opt-V2718|opt-V3718|opt-V4718][eth-V4718][usb-V4718][usb-A4818] [-b vme base address] [-ip ipaddress] [-sn serial number]\n");
}

/* ##########################################################################
*  MAIN
*  ########################################################################## */
int main(int argc, char* argv[]) {
	// miscellanea
	int i, k = 0, nw = 0, ntrg, pnt, pnt_old, quit = 0;
	clock_t before, beforeTrig;
	int file_mode = 0;
	uint32_t size;
	int VMELink = 0;
	int ConetNode = 0;
	uint32_t event_size = 0;
	uint8_t ports[6] = { "ABDEF" };
	uint32_t lost_triggers = 0;
	int ContTrigger = 0;
	// i/o files
	FILE* WbinFile;
	FILE* RbinFile;
	char input_file[100], output_file[100];
	// scaler struct initialization	
	PLUscalerDescr des;

	CAEN_PLU_ERROR_CODE ret = CAEN_PLU_GENERIC;
	int ConnType;
	char* IpAddr;
	char* VmeBaseAddress;
	int SerialNumber;
	char* ConfigFileName;
	int optarg1 = 0;
	int optarg2 = 0;
	//ConnType = 3; // V2718
	ConnType = 0; // USB
	IpAddr = "192.168.0.90";
	VmeBaseAddress = "32100000";
	SerialNumber = 28;
	ConfigFileName = "PLUscaler_Config.txt";

	for (i = 1; i < argc; i++) { // Skip argv[0] (program name)
		if (strcmp(argv[i], "-c") == 0) { // Process optional arguments
		// The last argument is argv[argc-1].  Make sure there are enough arguments.
			if (i + 1 <= argc - 1) { // There are enough arguments in argv.
				 // Increment 'i' again so that you don't check these arguments the next time through the loop.
				i++;
				if (strcmp(argv[i], "usb-direct") == 0) ConnType = 0;
				else if (strcmp(argv[i], "eth-direct") == 0) ConnType = 1;
				else if (strcmp(argv[i], "usb-V1718") == 0) ConnType = 2;
				else if (strcmp(argv[i], "usb-V3718") == 0) ConnType = 2;
				else if (strcmp(argv[i], "opt-V2718") == 0) ConnType = 3;
				else if (strcmp(argv[i], "opt-V3718") == 0) ConnType = 3;
				else if (strcmp(argv[i], "opt-V4718") == 0) ConnType = 3;
				else if (strcmp(argv[i], "eth-V4718") == 0) ConnType = 4;
				else if (strcmp(argv[i], "usb-V4718") == 0) ConnType = 5;
				else if (strcmp(argv[i], "usb-A4818") == 0) ConnType = 6;
				else {
					// Print usage statement and exit (see below)
					usage();
					exit(-1);
				}
			}
		}
		else if (strcmp(argv[i], "-b") == 0) {
			if (i + 1 <= argc - 1) {
				i++;
				VmeBaseAddress = argv[i];
			}
			else {
				usage();
				exit(-1);
			}
		}
		else if (strcmp(argv[i], "-sn") == 0) {
			if (i + 1 <= argc - 1) {
				i++;
				SerialNumber = atoi(argv[i]);
			}
			else {
				usage();
				exit(-1);
			}
		}
		else if (strcmp(argv[i], "-ip") == 0) {
			if (i + 1 <= argc - 1) {
				i++;
				IpAddr = argv[i];
			}
			else {
				usage();
				exit(-1);
			}
		}
		else if (strcmp(argv[i], "-h") == 0) {
			usage();
			exit(-1);
		}
		else { // Process non-optional arguments here
			ConfigFileName = argv[i];
		}
	}



	DefaultConfig(&des);
	ret = ReadConfigFile(&des, ConfigFileName);
	if (ret) {
		printf("Configuration file %s not found", ConfigFileName);
		exit(-1);
	}


	// gnuplot initialization
	double* Yplot, * Xplot, * Timeplot, * Chplot;
	gnuplot_ctrl* plt, * plt2;
	if (des.OpenPlot) {
		GPlotInit(&plt, "boxes", "Channel", "Counts");
		GPlotInit(&plt2, "points", "Trigger Time", "Counts");
		Yplot = malloc(160 * sizeof(double));
		Xplot = malloc(160 * sizeof(double));
		Timeplot = malloc(des.PlotPoints * sizeof(double));
		Chplot = malloc(des.PlotPoints * sizeof(double));
		for (i = 0; i < 160; i++) {
			Xplot[i] = Yplot[i] = 0.;
		}
		for (i = 0; i < des.PlotPoints; i++) {
			Timeplot[i] = Chplot[i] = 0.;
		}
	}
	int printout = 0;
	uint32_t EventCnt = 0;
	char c = ' ';
	char cbef = ' ';
	uint32_t* buffer, data;
	uint32_t Counters[160], Counters64[160];
	for (i = 0; i < 160; i++) { Counters[i] = 0; Counters64[i] = 0; }
	uint64_t TimeTag = 0;




	/* allocate the memory buffer for the readout */
	if (BUFFER_SIZE < (130 * 4)) {
		printf("Warning: local buffer size too small to contain one event\n");
		getch();
		return -1;
	}
	buffer = malloc(4 * 4096); // 4096 is the size of the FIFO where the output data is stored. A single readout cannot exceed its size.
	/* open the output file */
	if (des.SaveToFile) {
		printf("please enter the binary output file name\n");
		scanf("%s", output_file);
		printf("output will be copied in file %s\n", output_file);
		WbinFile = fopen(output_file, "wb");
	}

	ntrg = 0;

	/* Connect to the module */
	switch (ConnType) {
	case 0:
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_DIRECT_USB, &SerialNumber, ConetNode, 0, &des.handle);
		des.LinkType = 0;
		break;
	case 1:
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_DIRECT_ETH, IpAddr, VMELink, 0, &des.handle);
		des.LinkType = 1;
		break;
	case 2:
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V1718, &VMELink, ConetNode, VmeBaseAddress, &des.handle);
		des.LinkType = 2;
		break;
	case 3:
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V2718, &VMELink, ConetNode, VmeBaseAddress, &des.handle);
		des.LinkType = 3;
		break;
	case 4: // ETH V4718
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V4718_ETH, IpAddr, 0, VmeBaseAddress, &des.handle);
		des.LinkType = 4;
		break;
	case 5: // USB V4718
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V4718_USB, &SerialNumber, 0, VmeBaseAddress, &des.handle);
		des.LinkType = 5;
		break;
	case 6: // USB_A4818
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_A4818, &SerialNumber, 0, VmeBaseAddress, &des.handle);
		des.LinkType = 6;
		break;
	default:
		break;
	}

	if (ret != CAEN_PLU_OK) {
		printf("The module cannot be accessed, the program will be run in offline mode\n");
		des.OfflineMode = true;
	}
	else {
		printf("A connection with the module/bridge has been established\n");
		if (PLUReadRegister(des, MAIN_FIRMWARE_REVISION, &data) < 0) {
			printf("ERROR: main FPGA cannot be read\n");
			return -1;
		}
		else
			printf("Main   firmware revision: %d.%d\n", (data >> 8) & 0xFF, data & 0xFF);
		/* read the firmware revision of the FPGA 'USER' (scaler) */
		if (PLUReadRegister(des, PLUSCALER_USER_FW_REV, &data) < 0) {
			printf("ERROR: the user FPGA of the scaler cannot be read\n");
			return -1;
		}
		else {
			if (((data >> 8) & 0xFF) != 0x80) {
				printf("ERROR! No scaler firmware found on user FPGA installed\n\n");
				return -1;
			}
			printf("Scaler firmware revision: %d.%d\n\n", (data >> 8) & 0xFF, data & 0xFF);
		}

		// check which daughterboards are present and possibly mask unused channels
		db_check(&des);

		// compute the event size (function of config parameters only)
		event_size = CountEventSize(&des);

		// disable triggers
		PLUWriteRegister(des, PLUSCALER_CTRL_BITSET, des.TriggerMode & 0x0);

		// empty the FIFO in the main FPGA 
		PLUWriteRegister(des, MAIN_SOFT_RESET, 0x1);

		/* Reset the board */
		PLUReset(des);

		/* program the scaler */
		PLUProgram(&des);
		PLUClearData(des);
	}

	/* start the acquisition */
	printf("\n Options:\n [h] print instructions\n [m] manual controller (read/write registers)\n [p] Print Counters of active channels (no VME readout)\n [t] Software trigger\n [T] Software trigger (periodic)\n [r] Reset Counters\n [f] Analyze binary file (previously saved)\n [q] quit\n");
	printf("Press ENTER\n");
	getchar();
	// enable triggers
	PLUWriteRegister(des, PLUSCALER_CTRL_BITSET, des.TriggerMode & 0x3);
	before = clock();
	beforeTrig = clock();
	while (!quit) {
		if ((int)((double)(clock() - before) / (double)CLOCKS_PER_SEC) >= des.ReadingTime) {
			printout = 1;
			before = clock();
		}
		if (ContTrigger) {
			if ((int)((double)(clock() - beforeTrig) / (double)CLOCKS_PER_SEC) >= des.SWTrigTime) {
				PLUSoftTrigger(des);
				beforeTrig = clock();
			}
		}
		if (kbhit()) {
			c = getch();
			switch (c) {
			case 'q':
				quit = 1;
				PLUWriteRegister(des, PLUSCALER_CTRL_BITSET, des.TriggerMode & 0x0); // stop triggers
				break;
			case 'h':
				printf("Options:\n [h] print instructions\n [m] manual controller (read/write registers)\n [p] Print Counters of active channels (no VME readout)\n [t] Software trigger\n [T] Software trigger (periodic)\n [r] Reset Counters\n [f] Analyze binary file (previously saved)\n [q] quit\n");
				break;
			case 'p':
				PrintCntOnScreen(des, Counters, Counters64);
				break;
			case 'm':
				if (!des.OfflineMode) Manual(des);
				else printf("offline mode! Press 'h' to choose another option\n");
				break;
			case 't':
				if (!des.OfflineMode) { "SW trigger sent\n"; PLUSoftTrigger(des); }
				else printf("offline mode! Press 'h' to choose another option\n");
				break;
			case 'T':
				if (!des.OfflineMode) {
					if (ContTrigger) { printf("Periodic SW trigger deactivated"); ContTrigger = 0; }
					else { printf("Periodic SW trigger activated"); ContTrigger = 1; }
				}
				else printf("offline mode! Press 'h' to choose another option\n");
				break;
			case 'r':
				if (!des.OfflineMode) PLUResetCounters(des);
				else printf("offline mode! Press 'h' to choose another option\n");
				break;
			case 'f':
				printf("enter the binary file name\n");
				scanf("%s", input_file);
				if (access(input_file, 0) == -1) {
					printf("the chosen input binary file does not exist\n");
					break;
				}
				else {
					printf("binary file %s will be read\n", input_file);
					file_mode = 1;
				}
				break;
			default: break;
			}
		}
		if (des.OfflineMode) {
			if (file_mode) {
				size_t result;
				RbinFile = fopen(input_file, "rb");
				// obtain file size:
				fseek(RbinFile, 0, SEEK_END);
				nw = ftell(RbinFile) / 4;
				rewind(RbinFile);
				result = fread(buffer, 1, 4 * nw, RbinFile);
				if (result != 4 * nw) printf("error\n");
			}
		}
		else
			ret = ReadEvents(des, buffer, event_size, &nw);
		if (ret != CAEN_PLU_OK) {
			printf("Error after event read! Exiting....\n");
			return(-1);
		}


		if (nw > 0) {
			if (des.SaveToFile) fwrite(buffer, 1, nw * 4, WbinFile);
		}
		/* analyze and save event data */
		pnt = pnt_old = 0;
		while (pnt < nw) {
#ifdef WIN32
			//system("cls");
#else
			//system("clear");
#endif
			des.PLU_pl = (buffer[pnt] >> 30) & 0x1;

			if (des.OfflineMode && file_mode) {
				printf("press 'q' to exit or any other key to analyze the next event\n");
				c = getch();
				if (c == 'q') return -1;
			}
			while (pnt < nw) {
				if (!des.PLU_pl) {
					if (des.OfflineMode) {
						printf("in V1495 mode port masks are not part of the payload and therefore it is not possible to associate counters to channels when offline\n");
						return -1;
					}
					des.TimeTag = (buffer[pnt] >> 31 & 0x1);
					EventCnt = (buffer[pnt++] >> 8) & 0xFFFF;
				}
				else {
					if (((buffer[pnt] >> 12) & 0x3FFFF) > EventCnt) lost_triggers += ((buffer[pnt] >> 12) & 0x3FFFF) - EventCnt - 1;
					EventCnt = (buffer[pnt++] >> 12) & 0x3FFFF;
					des.TimeTag = (buffer[pnt] & 0x1);
					des.mask_en = ((buffer[pnt] >> 1) & 0x1);
					des.cnt64_en = ((buffer[pnt] >> 2) & 0x1);
					des.time64_en = ((buffer[pnt] >> 3) & 0x1);
					des.ports = ((buffer[pnt++] >> 4) & 0x1F);
				}
				if (des.TimeTag) {
					TimeTag = buffer[pnt++];
					if (des.PLU_pl && des.time64_en) {
						TimeTag = TimeTag + ((uint64_t)buffer[pnt++] << 32);
					}
				}

				if ((des.PLU_pl == 1) && (des.mask_en == 0)) {
					printf("No masks activated, active channels are unknown. Exiting.");
					return -1;
				}
				// fill counters only if they have to be either printed or plotted
				if (des.PrintCounters || des.OpenPlot) {
					for (i = 0; i < 160; i++) {
						if ((des.ports >> i / 32) & 0x1) {
							if (i % 32 == 0 && des.PLU_pl) des.ChEnable[i / 32] = buffer[pnt++]; //read from packet only in PLU mode
							if (chan_enabled(i, &des)) {
								Counters[i] = buffer[pnt++];
								if (des.PLU_pl && des.cnt64_en) Counters64[i] = buffer[pnt++];
							}
						}
					}
				}
				else pnt = pnt_old + event_size;
				pnt_old = pnt;
			}
			// fill plot axes and plot
			if (printout) {
				printf("\n**********************************\n");
				printf("Last event collected in the last BLT:\n");
				printf("Event number: %"PRIu32"\n", EventCnt);
				printf("Time Tag    : %"PRIu64" c.c.\n", TimeTag);
				for (i = 0; i < 5; i++) {
					if (printout) if ((des.ports >> i) & 0x1) printf("port %c has active channels\n", ports[i]);
				}
				if (des.PrintCounters) PrintCntOnScreen(des, Counters, Counters64);
				if (des.OpenPlot) {
					for (i = 0; i < 160; i++) {
						if (chan_enabled(i, &des)) {
							Xplot[i] = i;
							Yplot[i] = Counters[i];
							if (i == des.PlotChan && k < des.PlotPoints) {
								Timeplot[k] = (double)TimeTag;
								Chplot[k] = Counters[i];
								k++;
								if (k == des.PlotPoints) printf("Maximum number of points reserved for the counts vs time plot reached, it will not be updated anymore\n");
							}
						}
						else {
							Xplot[i] = i;
							Yplot[i] = 0;
						}
					}

					gnuplot_resetplot(plt);
					gnuplot_resetplot(plt2);
					gnuplot_plot_xy(plt, Xplot, Yplot, 160, "Counts/Channel");
					if (k < des.PlotPoints) gnuplot_plot_xy(plt2, Timeplot, Chplot, k, "Counts/Time");
				}
				if (printout) printf("Lost Triggers in the interval: %d\n", lost_triggers);
			}
			if (printout) lost_triggers = 0;
			printout = 0;
			ntrg = 0;
		}

		if (des.OfflineMode && file_mode) {
			printf("file decoded succesfully, press 'h' to choose other options\n");
			file_mode = 0;
			nw = 0;
			fclose(RbinFile);
		}
	}

	/* stop the acquisition */
	if (!des.OfflineMode) PLUWriteRegister(des, PLUSCALER_CTRL_BITCLEAR, 3);

	if (des.OpenPlot) {
		free(Yplot);
		free(Xplot);
		free(Timeplot);
		free(Chplot);
		gnuplot_close(plt);
		gnuplot_close(plt2);
	}

	if (!des.OfflineMode) CAEN_PLU_CloseDevice(des.handle);
	free(buffer);

	if (des.SaveToFile) {
		fseek(WbinFile, 0, SEEK_END);
		size = ftell(WbinFile);
		fclose(WbinFile);
		printf("output file of size %d bytes written\n", size);
	}
	return 0;
}

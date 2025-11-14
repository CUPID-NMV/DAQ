/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2024 CAEN SpA
*
*	This file is part of the CAEN PLU Library.
*
*	Permission is hereby granted, free of charge, to any person obtaining a
*	copy of this software and associated documentation files (the "Software"),
*	to deal in the Software without restriction, including without limitation
*	the rights to use, copy, modify, merge, publish, distribute, sublicense,
*	and/or sell copies of the Software, and to permit persons to whom the
*	Software is furnished to do so.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
*	THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*	DEALINGS IN THE SOFTWARE.
*
*	SPDX-License-Identifier: MIT-0
*
***************************************************************************//*!
*
*	\file		CAEN_PLULibTEST.cpp
*	\brief		CAEN PLU Library demo
*	\author
*
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32 // Windows
#include <windows.h>
#include <conio.h>
#else // Linux
#define _popen popen
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#endif

#include <CAEN_PLULib.h>

#ifndef _WIN32
static inline char _getch() {
	char buf = 0;
	struct termios old = { 0 };
	fflush(stdout);
	if (tcgetattr(0, &old)<0)
		perror("tcsetattr()");
	old.c_lflag &= ~ICANON;
	old.c_lflag &= ~ECHO;
	old.c_cc[VMIN] = 1;
	old.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSANOW, &old)<0)
		perror("tcsetattr ICANON");
	if (read(0, &buf, 1)<0)
		perror("read()");
	old.c_lflag |= ICANON;
	old.c_lflag |= ECHO;
	if (tcsetattr(0, TCSADRAIN, &old)<0)
		perror("tcsetattr ~ICANON");
	printf("%c\n", buf);
	return buf;
}
#endif

#define USB_TEST
//#define VME_TEST
//#define ETH_TEST

int help() {
#ifdef _WIN32
	printf("\nUsage: CAEN_PLULib_Test [-c connection_type (0=USB, 1=ETH, 2=V1718, 3 = V2718] -sn [device_serial_number] -ip [IP address if ethernet] -b [vme_base_address]\n");
#else
	printf("\nUsage: ./CAEN_PLULibTEST [-c connection_type (0=USB, 1=ETH, 2=V1718, 3 = V2718] -sn [device_serial_number] -ip [IP address if ethernet] -b [vme_base_address]\n");
#endif
	return 1;
}

int main(int argc, char* argv[])
{
	int	       i, handle;
	tUSBDevice list_usb[1024];
	uint32_t     num_usb;
	int        connection_type = -1;
	uint32_t	SerialNumber;
	char	ip[20];
	CAEN_PLU_ERROR_CODE ret;
	char       sn[100];
	uint32_t   val, length;
	uint32_t  arr[256];
	int status;
	const char *vme_base_address = "32100000";
	const int vme_link = 0;

	/* iterate over all arguments */
	SerialNumber = 0;
	ip[0] = 0;

	for (i = 1; i < (argc - 1); i++) {
		if (strcmp("-c", argv[i]) == 0) {
			connection_type = atoi(argv[++i]);
			continue;
		}
		if (strcmp("-b", argv[i]) == 0) {
			vme_base_address = argv[++i];
			continue;
		}
		if (strcmp("-sn", argv[i]) == 0) {
			SerialNumber = atoi(argv[++i]);
			continue;
		}
		if (strcmp("-ip", argv[i]) == 0) {
			strcpy(ip, argv[++i]);
			continue;
		}
		return help();
	}

	if (connection_type == -1) {
		printf("Please specify the connection type (-c)\n");
		return help();
	}

	if ((SerialNumber == 0) && (connection_type == 0) ) {
		printf("Please specify the Serial Number of the board (-sn)\n");
		return help();
	}

	if ((ip[0] == 0) && (connection_type == 1)) {
		printf("Please specify the IP address of the board (-sn)\n");
		return help();
	}

	switch (connection_type) {
	case 0:

		/* Connect to first enumerate board, if it exists */
		//! [DiscoverUSB]
		ret = CAEN_PLU_USBEnumerate(list_usb, &num_usb);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		ret = CAEN_PLU_USBEnumerateSerialNumber(&num_usb, sn, 100);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_DIRECT_USB, &SerialNumber, 0, 0, &handle);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		break;

	case 1:
		//! [OpenDeviceETHirect]
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_DIRECT_ETH, ip, 0, 0, &handle);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		//! [OpenDeviceETHirect]
		break;

	case 2:
		//! [OpenDeviceV1718]
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V1718, &vme_link, 0, vme_base_address, &handle);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		//! [OpenDeviceV1718]
		break;

	case 3:
		//! [OpenDeviceV2718]
		ret = CAEN_PLU_OpenDevice2(CAEN_PLU_CONNECT_VME_V2718, &vme_link, 0, vme_base_address, &handle);
		if (ret != CAEN_PLU_OK) {
			printf("Error %d\n", ret);
			exit(0);
		}
		//! [OpenDeviceV2718]
		break;

	default:
		help();
		exit(-1);
		break;
	}

	printf("Device connected\n");

	ret = CAEN_PLU_GetSerialNumber(handle, sn, 100);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	printf("Device Serial Number: %s\n", sn);
	//! [Registers]
	ret = CAEN_PLU_ReadReg(handle, 0x8020, &val);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_WriteReg(handle, 0x8020, 0x1234);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_ReadReg(handle, 0x8020, &val);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	//! [Registers]


	//! [Data area access (memory mode)]
	memset(arr, 0, 10 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteData32(handle, 0x8020, 4, arr);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_ReadData32(handle, 0x8020, 1, arr, &length);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	//! [Data area access (memory mode)]

	//! [Data area access (FIFO mode)]
	memset(arr, 0, 10 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFIFO32(handle, 0x8020, 4, arr);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_ReadFIFO32(handle, 0x8020, 4, arr, &length);
	//! [Data area access (FIFO mode)]

	ret = CAEN_PLU_ConnectionStatus(handle, &status);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}

	//! [Flash]

	// Sector delete test
	ret = CAEN_PLU_EnableFlashAccess(handle, FPGA_MAIN);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_MAIN, 510);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}

	memset(arr, 0, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_MAIN, 510);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	memset(arr, 0xAAAAAAAA, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DisableFlashAccess(handle, FPGA_MAIN);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_MAIN, 510);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_MAIN, 510 * 64 * 1024, arr, 256);

	ret = CAEN_PLU_EnableFlashAccess(handle, FPGA_USER);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_USER, 510);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	memset(arr, 0, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_USER, 510);
	memset(arr, 0xFFFFFFFF, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_USER, 510);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_USER, 510 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DisableFlashAccess(handle, FPGA_USER);

	ret = CAEN_PLU_EnableFlashAccess(handle, FPGA_DELAY);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_DELAY, 127);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	memset(arr, 0, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_DELAY, 127);
	memset(arr, 0xFFFFFFFF, 256 * sizeof(arr[0]));
	ret = CAEN_PLU_WriteFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DeleteFlashSector(handle, FPGA_DELAY, 127);
	ret = CAEN_PLU_ReadFlashData(handle, FPGA_DELAY, 127 * 64 * 1024, arr, 256);
	ret = CAEN_PLU_DisableFlashAccess(handle, FPGA_DELAY);
	//! [Flash]


	CAEN_PLU_WriteReg(handle, 0x1800, 1); // Set delay ID
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	CAEN_PLU_WriteReg(handle, 0x1804, 200); // Set clock rate low enough..
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	//! [GateAndDelay]
	uint32_t gate, delay, scale_factor;
	ret = CAEN_PLU_InitGateAndDelayGenerators(handle);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 0, 0, 255);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 1, 0, 255);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 4, 2, 255);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 5, 3, 255);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 6, 4, 255);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 7, 5, 255);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 0, 1, 100, 6, 255);
	ret = CAEN_PLU_GetGateAndDelayGenerator(handle, 0, &gate, &delay, &scale_factor);


	CAEN_PLU_WriteReg(handle, 0x1800, 0x11); // Select delay 1

	ret = CAEN_PLU_GetGateAndDelayGenerator(handle, 1, &gate, &delay, &scale_factor);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 20, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 40, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 60, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 80, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 100, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 200, 1);
	ret = CAEN_PLU_SetGateAndDelayGenerator(handle, 1, 1, 10, 300, 1);
	ret = CAEN_PLU_GetGateAndDelayGenerator(handle, 1, &gate, &delay, &scale_factor);
	//! [GateAndDelay]

	tBOARDInfo HWOPTIONS;
	ret = CAEN_PLU_GetInfo(handle, &HWOPTIONS);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	//! [CloseDevice]
	ret = CAEN_PLU_CloseDevice(handle);
	if (ret != CAEN_PLU_OK) {
		printf("Error %d\n", ret);
		exit(0);
	}
	printf("Program closed with success\n");
	//! [CloseDevice]

	return 0;
}

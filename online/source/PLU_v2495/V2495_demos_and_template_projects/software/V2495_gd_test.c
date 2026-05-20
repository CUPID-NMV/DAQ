// V2495_gd_test.c
//

#include <stdio.h>
#include "CAENComm.h"

UINT16 const DATA_WR_REG = 0x7f00;
UINT16 const COMMAND_REG = 0x7f04;
UINT16 const CONTROL_REG = 0x7f08;
UINT16 const DATA_RD_REG = 0x7f0C;

void write_cmd(int *handle) {
	CAENComm_Write32(*handle, CONTROL_REG, 0x1);
	CAENComm_Write32(*handle, CONTROL_REG, 0x0);
}

void read_cmd(int *handle) {
	CAENComm_Write32(*handle, CONTROL_REG, 0x2);
	CAENComm_Write32(*handle, CONTROL_REG, 0x0);
}

void set_delay(int *handle, UINT32 delay) {
	CAENComm_Write32(*handle, DATA_WR_REG, delay);
	CAENComm_Write32(*handle, COMMAND_REG, 0x2000);
	write_cmd(handle);
}

void set_gate(int *handle, UINT32 gate) {
	CAENComm_Write32(*handle, DATA_WR_REG, gate);
	CAENComm_Write32(*handle, COMMAND_REG, 0x3000);
	write_cmd(handle);
}

void write_gatedelay(int *handle, UINT32 channel) {
	CAENComm_Write32(*handle, COMMAND_REG, 0x100 + (0x0000001F & channel));
	write_cmd(handle);
}

void read_gate(int *handle, UINT32 *gate, int channel) {
	CAENComm_Write32(*handle, COMMAND_REG, 0x2000 + (0x0000001F & channel));
	read_cmd(handle);
	CAENComm_Read32(*handle, DATA_RD_REG, gate);
}

void read_delay(int *handle, UINT32 *delay, int channel) {
	CAENComm_Write32(*handle, COMMAND_REG, 0x3000 + (0x0000001F & channel));
	read_cmd(handle);
	CAENComm_Read32(*handle, DATA_RD_REG, delay);
}

int main(int argc, char* argv[])
{
	
	int handle;
	UINT32 gate, delay;
	UINT32 chan;
	CAENComm_OpenDevice(CAENComm_USB,0,0,0x32100000,&handle);
  //CAENComm_OpenDevice(CAENComm_OpticalLink,0,0,0x32100000,&handle);
	printf("please choose the gate/delay channel index (0-31): ");
	scanf_s("%d", &chan);
	if (chan < 0 || chan>31) {
		printf("Choose a channel ID between 0 and 31\n");
		return -1;
	}
	printf("Please insert the delay value: ");
	scanf_s("%d",&delay);
	set_delay(&handle, delay);	
	printf("Please insert the gate value: ");
	scanf_s("%d", &gate);
	set_gate(&handle, gate);
	write_gatedelay(&handle,chan);
	read_delay(&handle, &delay, chan);
	read_gate(&handle, &gate, chan);
	printf("set values: DELAY=%X, GATE=%X\n", delay, gate);	
	CAENComm_CloseDevice(handle);
	return 0;
}


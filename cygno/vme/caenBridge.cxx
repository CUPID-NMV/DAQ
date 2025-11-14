/********************************************************************

  $Id: caenBridge.c 269 2013-10-31 17:22:20Z tassielli $
  $Author:  $
  $Revision:  $

  Created by:   Giovanni F. Tassielli
  Based on v1718.c created by Jimmy Ngai

  Contents:     Midas VME standard (MVMESTD) layer for CAEN VME Bridge VX718
                using CAENVMElib Linux library

\********************************************************************/

#ifdef __linux__
#ifndef OS_LINUX
#define OS_LINUX
#endif
#endif

#ifdef OS_LINUX

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#endif // OS_LINUX

#include "caenBridge.h"

static CaenBridgePar myCaenBridgePar = {cvV1718, 0};

void CaenBrdgResetDefaultPar() {
    myCaenBridgePar.model = cvV1718;
    myCaenBridgePar.Link= 0;
}

void CaenBrdgSetPar( CVBoardTypes model, short Link) {
    myCaenBridgePar.model = model;
    myCaenBridgePar.Link= Link;
}

/*------------------------------------------------------------------*/

/********************************************************************\

  MIDAS VME standard (MVMESTD) functions

\********************************************************************/

//        cvV1718 = 0,                    /* CAEN V1718 USB-VME bridge                    */
//        cvV2718 = 1,                    /* V2718 PCI-VME bridge with optical link       */
//        Link      : The link number (don't care for V1718).

int mvme_open(MVME_INTERFACE **vme, int idx)
{
   *vme = (MVME_INTERFACE *) malloc(sizeof(MVME_INTERFACE));
   if (*vme == NULL)
      return MVME_NO_MEM;

   memset(*vme, 0, sizeof(MVME_INTERFACE));

   if (myCaenBridgePar.model<0 || myCaenBridgePar.model>1) {
       printf("request of opening connection with unknown CAEN VME Bridge model!\n");
       return MVME_NO_INTERFACE;
   }
   printf("request of opening connection with CAEN VME Bridge : ");
   if (myCaenBridgePar.model==0) {printf("V1718");  }
   else if (myCaenBridgePar.model==1) {printf("V2718");  }
   printf(" at Link: %i\n",myCaenBridgePar.Link);
   char swrel[100]; CAENVME_SWRelease(swrel);
   printf("FW release: %s\n",swrel);
   /* open VME */
   if (CAENVME_Init(myCaenBridgePar.model, myCaenBridgePar.Link, idx, &(*vme)->handle) != cvSuccess) {
     printf("%d\n",CAENVME_Init(myCaenBridgePar.model, myCaenBridgePar.Link, idx, &(*vme)->handle));
       CaenBrdgResetDefaultPar();
       return MVME_NO_INTERFACE;
   }
   CaenBrdgResetDefaultPar();

   /* default values */
   (*vme)->am        = MVME_AM_DEFAULT;
   (*vme)->dmode     = MVME_DMODE_D32;
   (*vme)->blt_mode  = MVME_BLT_NONE;
   (*vme)->table     = NULL; // not used

   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_close(MVME_INTERFACE *vme)
{
   CAENVME_End(vme->handle);

   free(vme);

   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_sysreset(MVME_INTERFACE *vme)
{
   CAENVME_SystemReset(vme->handle);

   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_write(MVME_INTERFACE *vme, mvme_addr_t vme_addr, void *src, mvme_size_t n_bytes)
{
   mvme_size_t i;
   int status=0, n;
   int hvme;
   hvme = vme->handle;

   n = 0;

   /* D8 */
   if (vme->dmode == MVME_DMODE_D8) {
      for (i=0 ; i<n_bytes ; i++)
         status = CAENVME_WriteCycle(hvme, vme_addr, (src+i), (CVAddressModifier)vme->am, cvD8);
      n = n_bytes;
   /* D16 */
   } else if (vme->dmode == MVME_DMODE_D16) {
      /* normal I/O */
      if (vme->blt_mode == MVME_BLT_NONE) {
         for (i=0 ; i<(n_bytes>>1) ; i++)
            status = CAENVME_WriteCycle(hvme, vme_addr, (src+(i<<1)), (CVAddressModifier)vme->am, cvD16);
         n = n_bytes;
      /* FIFO BLT */
      } else if ((vme->blt_mode == MVME_BLT_BLT32FIFO) || (vme->blt_mode == MVME_BLT_MBLT64FIFO))
         status = CAENVME_FIFOBLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, cvD16, &n);
      /* BLT */
      else
         status = CAENVME_BLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, cvD16, &n);
   /* D32 */
   } else if (vme->dmode == MVME_DMODE_D32) {
      /* normal I/O */
      if (vme->blt_mode == MVME_BLT_NONE) {
         for (i=0 ; i<(n_bytes>>2) ; i++)
            status = CAENVME_WriteCycle(hvme, vme_addr, (src+(i<<2)), (CVAddressModifier)vme->am, cvD32);
         n = n_bytes;
      /* FIFO BLT */
      } else if (vme->blt_mode == MVME_BLT_BLT32FIFO)
         status = CAENVME_FIFOBLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, cvD32, &n);
      /* BLT */
      else
         status = CAENVME_BLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, cvD32, &n);
   /* D64 */
   } else if (vme->dmode == MVME_DMODE_D64) {
      /* FIFO MBLT */
      if (vme->blt_mode == MVME_BLT_MBLT64FIFO) 
         status = CAENVME_FIFOMBLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, &n);
      /* MBLT */
      else
         status = CAENVME_MBLTWriteCycle(hvme, vme_addr, src, n_bytes, (CVAddressModifier)vme->am, &n);
   }

   if (status != cvSuccess)
      n = 0;

   return n;
}

/*------------------------------------------------------------------*/

int mvme_write_value(MVME_INTERFACE *vme, mvme_addr_t vme_addr, unsigned int value)
{
   int status=0, n;
   int hvme;
   hvme = vme->handle;

   if (vme->dmode == MVME_DMODE_D8)
      n = 1;
   else if (vme->dmode == MVME_DMODE_D16)
      n = 2;
   else
      n = 4;

   /* D8 */
   if (vme->dmode == MVME_DMODE_D8)
      status = CAENVME_WriteCycle(hvme, vme_addr, &value, (CVAddressModifier)vme->am, cvD8);
   /* D16 */
   else if (vme->dmode == MVME_DMODE_D16)
      status = CAENVME_WriteCycle(hvme, vme_addr, &value, (CVAddressModifier)vme->am, cvD16);
   /* D32 */
   else if (vme->dmode == MVME_DMODE_D32)
      status = CAENVME_WriteCycle(hvme, vme_addr, &value, (CVAddressModifier)vme->am, cvD32);

   if (status != cvSuccess)
      n = 0;

   return n;
}

/*------------------------------------------------------------------*/

int mvme_read(MVME_INTERFACE *vme, void *dst, mvme_addr_t vme_addr, mvme_size_t n_bytes)
{
   mvme_size_t i;
   int status=0, n;
   int hvme;
   hvme = vme->handle;

   n = 0;

   /* D8 */
   if ((vme->dmode == MVME_DMODE_D8) || (vme->blt_mode == MVME_BLT_NONE)) {
      for (i=0 ; i<n_bytes ; i++)
         status = CAENVME_ReadCycle(hvme, vme_addr, (dst+i), (CVAddressModifier)vme->am, cvD8);
      n = n_bytes;
   /* D16 */
   } else if (vme->dmode == MVME_DMODE_D16) {
      /* normal I/O */
      if (vme->blt_mode == MVME_BLT_NONE) {
         for (i=0 ; i<(n_bytes>>1) ; i++)
            status = CAENVME_ReadCycle(hvme, vme_addr, (dst+(i<<1)), (CVAddressModifier)vme->am, cvD16);
         n = n_bytes;
      /* FIFO BLT */
      } else if ((vme->blt_mode == MVME_BLT_BLT32FIFO) || (vme->blt_mode == MVME_BLT_MBLT64FIFO))
         status = CAENVME_FIFOBLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, cvD16, &n);
      /* BLT */
      else
         status = CAENVME_BLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, cvD16, &n);
   /* D32 */
   } else if (vme->dmode == MVME_DMODE_D32) {
      /* normal I/O */
      if (vme->blt_mode == MVME_BLT_NONE) {
         for (i=0 ; i<(n_bytes>>2) ; i++)
            status = CAENVME_ReadCycle(hvme, vme_addr, (dst+(i<<2)), (CVAddressModifier)vme->am, cvD32);
         n = n_bytes;
      /* FIFO BLT */
      } else if (vme->blt_mode == MVME_BLT_BLT32FIFO)
         status = CAENVME_FIFOBLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, cvD32, &n);
      /* BLT */
      else
         status = CAENVME_BLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, cvD32, &n);
   /* D64 */
   } else if (vme->dmode == MVME_DMODE_D64) {
      /* FIFO MBLT */
      if (vme->blt_mode == MVME_BLT_MBLT64FIFO)
         status = CAENVME_FIFOMBLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, &n);
      /* MBLT */
      else
         status = CAENVME_MBLTReadCycle(hvme, vme_addr, dst, n_bytes, (CVAddressModifier)vme->am, &n);
   }

   if ((status != cvSuccess) && (status != cvBusError))
      n = 0;

   return n;
}

/*------------------------------------------------------------------*/

unsigned int mvme_read_value(MVME_INTERFACE *vme, mvme_addr_t vme_addr)
{
   unsigned int data;
   int status=0;
   int hvme;
   hvme = vme->handle;

   data = 0;

   /* D8 */
   if (vme->dmode == MVME_DMODE_D8)
      status = CAENVME_ReadCycle(hvme, vme_addr, &data, (CVAddressModifier)vme->am, cvD8);
   /* D16 */
   else if (vme->dmode == MVME_DMODE_D16)
      status = CAENVME_ReadCycle(hvme, vme_addr, &data, (CVAddressModifier)vme->am, cvD16);
   /* D32 */
   else if (vme->dmode == MVME_DMODE_D32)
      status = CAENVME_ReadCycle(hvme, vme_addr, &data, (CVAddressModifier)vme->am, cvD32);

   return data;
}

/*------------------------------------------------------------------*/

int mvme_set_am(MVME_INTERFACE *vme, int am)
{
   vme->am = am;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_get_am(MVME_INTERFACE *vme, int *am)
{
   *am = vme->am;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_set_dmode(MVME_INTERFACE *vme, int dmode)
{
   vme->dmode = dmode;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_get_dmode(MVME_INTERFACE *vme, int *dmode)
{
   *dmode = vme->dmode;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_set_blt(MVME_INTERFACE *vme, int mode)
{
   vme->blt_mode = mode;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

int mvme_get_blt(MVME_INTERFACE *vme, int *mode)
{
   *mode = vme->blt_mode;
   return MVME_SUCCESS;
}

/*------------------------------------------------------------------*/

/********************************************************************\

  Board specific functions

\********************************************************************/

WORD caenBridge_Read16(MVME_INTERFACE *mvme, DWORD base, int offset)
{
  int cmode;
  WORD data;

  mvme_get_dmode(mvme, &cmode);
  mvme_set_dmode(mvme, MVME_DMODE_D16);
  data = mvme_read_value(mvme, base+offset);
  mvme_set_dmode(mvme, cmode);
  return data;
}

void caenBridge_Write16(MVME_INTERFACE *mvme, DWORD base, int offset, WORD value)
{
  int cmode;
  mvme_get_dmode(mvme, &cmode);
  mvme_set_dmode(mvme, MVME_DMODE_D16);
  mvme_write_value(mvme, base+offset, value);
  mvme_set_dmode(mvme, cmode);
}

DWORD caenBridge_Read32(MVME_INTERFACE *mvme, DWORD base, int offset)
{
  int cmode;
  DWORD data;

  mvme_get_dmode(mvme, &cmode);
  mvme_set_dmode(mvme, MVME_DMODE_D32);
  data = mvme_read_value(mvme, base+offset);
  mvme_set_dmode(mvme, cmode);
  return data;
}

void caenBridge_Write32(MVME_INTERFACE *mvme, DWORD base, int offset, DWORD value)
{
  int cmode;
  mvme_get_dmode(mvme, &cmode);
  mvme_set_dmode(mvme, MVME_DMODE_D32);
  mvme_write_value(mvme, base+offset, value);
  mvme_set_dmode(mvme, cmode);
}

/*------------------------------------------------------------------*/

void caenBridge_MultiRead(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle, int *am, int *dmode)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      cvAMs[i] = (CVAddressModifier)am[i];

      if (dmode[i] == MVME_DMODE_D8)
         cvDWs[i] = cvD8;
      else if (dmode[i] == MVME_DMODE_D16)
         cvDWs[i] = cvD16;
      else if (dmode[i] == MVME_DMODE_D32)
         cvDWs[i] = cvD32;
      else
         cvDWs[i] = cvD32;

      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiRead(hvme, addrs, value, ncycle, cvAMs, cvDWs, cvECs);

   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

void caenBridge_MultiWrite(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle, int *am, int *dmode)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      cvAMs[i] = (CVAddressModifier)am[i];

      if (dmode[i] == MVME_DMODE_D8)
         cvDWs[i] = cvD8;
      else if (dmode[i] == MVME_DMODE_D16)
         cvDWs[i] = cvD16;
      else if (dmode[i] == MVME_DMODE_D32)
         cvDWs[i] = cvD32;
      else
         cvDWs[i] = cvD32;

      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiWrite(hvme, addrs, value, ncycle, cvAMs, cvDWs, cvECs);

   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

void caenBridge_MultiRead16(MVME_INTERFACE *mvme, DWORD *addrs, WORD *value, int ncycle)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   DWORD *buffer;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   buffer = (DWORD *) malloc(ncycle*sizeof(DWORD));
   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      buffer[i] = 0;
      cvAMs[i] = (CVAddressModifier)mvme->am;
      cvDWs[i] = cvD16;
      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiRead(hvme, addrs, buffer, ncycle, cvAMs, cvDWs, cvECs);

   for (i = 0; i < (mvme_size_t)ncycle; i++)
      value[i] = buffer[i];

   free(buffer);
   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

void caenBridge_MultiWrite16(MVME_INTERFACE *mvme, DWORD *addrs, WORD *value, int ncycle)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   DWORD *buffer;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   buffer = (DWORD *) malloc(ncycle*sizeof(DWORD));
   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      buffer[i] = value[i];
      cvAMs[i] = (CVAddressModifier)mvme->am;
      cvDWs[i] = cvD16;
      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiWrite(hvme, addrs, buffer, ncycle, cvAMs, cvDWs, cvECs);

   free(buffer);
   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

void caenBridge_MultiRead32(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      cvAMs[i] = (CVAddressModifier)mvme->am;
      cvDWs[i] = cvD32;
      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiRead(hvme, addrs, value, ncycle, cvAMs, cvDWs, cvECs);

   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

void caenBridge_MultiWrite32(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle)
{
   if (ncycle<0) { printf("Error request a negative number of cycle to read!\n"); return; }
   mvme_size_t i;
   CVAddressModifier *cvAMs;
   CVDataWidth *cvDWs;
   CVErrorCodes *cvECs;
   int hvme;
   hvme = mvme->handle;

   cvAMs = (CVAddressModifier *) malloc(ncycle*sizeof(CVAddressModifier));
   cvDWs = (CVDataWidth *) malloc(ncycle*sizeof(CVDataWidth));
   cvECs = (CVErrorCodes *) malloc(ncycle*sizeof(CVErrorCodes));

   for (i = 0; i < (mvme_size_t)ncycle; i++) {
      cvAMs[i] = (CVAddressModifier)mvme->am;
      cvDWs[i] = cvD32;
      cvECs[i] = cvSuccess;
   }

   CAENVME_MultiWrite(hvme, addrs, value, ncycle, cvAMs, cvDWs, cvECs);

   free(cvAMs);
   free(cvDWs);
   free(cvECs);
}

/*------------------------------------------------------------------*/

/*****************************************************************/
/**
Simple configuration of the pulsers.
@param *mvme   VME structure
@param pulser  0=PulserA, 1=PulserB
@param period  period in ns
@param width   pulse width in ns
@param pulseNo number of pulses, 0=infinite
@param start   start source (0=SW, 1=input channel)
@param reset   reset source (0=SW, 1=input channel)
*/
void caenBridge_PulserConfSet(MVME_INTERFACE *mvme, WORD pulser, DWORD period, DWORD width, WORD pulseNo, WORD start=0, WORD reset=0)
{
   CVPulserSelect pulSel = cvPulserA;
   CVOutputSelect outSel = cvOutput0;
   CVIOSources startSel = cvManualSW;
   CVIOSources resetSel = cvManualSW;
   CVTimeUnits unit = cvUnit25ns;
   int hvme = mvme->handle;

   switch (pulser) {
   case caenBridge_pulserA:
      pulSel = cvPulserA;
      outSel = cvOutput0;
      if(start != 0) startSel = cvInputSrc1;
      if(reset != 0) resetSel = cvInputSrc0;
      break;
   case caenBridge_pulserB:
      pulSel = cvPulserB;
      outSel = cvOutput2;
      startSel = cvInputSrc0;
      if(start != 0) startSel = cvInputSrc0;
      if(reset != 0) resetSel = cvInputSrc1;
      break;
   }
   if (period < 25*256) {
      period /= 25;
      width /= 25;
      unit = cvUnit25ns;
   } else if (period < 1600*256) {
      period /= 1600;
      width /= 1600;
      unit = cvUnit1600ns;
   } else if (period < 410000*256) {
      period /= 410000;
      width /= 410000;
      unit = cvUnit410us;
   } else {
      period /= 104000000;
      width /= 104000000;
      unit = cvUnit104ms;
   }
   if (width == 0)
      width = 1;
   else if (width > 255)
      width = 255;
   CAENVME_SetOutputConf(hvme, outSel, cvDirect, cvActiveHigh, cvMiscSignals);
   CAENVME_SetOutputConf(hvme, (CVOutputSelect)((int)outSel+1), cvDirect, cvActiveHigh, cvMiscSignals);
   CAENVME_SetPulserConf(hvme, pulSel, period, width, unit, pulseNo, startSel, resetSel);
}

/*------------------------------------------------------------------*/

void caenBridge_PulserStart(MVME_INTERFACE *mvme, WORD pulser)
{
   CVPulserSelect pulSel = cvPulserA;
   int hvme = mvme->handle;

   switch (pulser) {
   case caenBridge_pulserA:
      pulSel = cvPulserA;
      break;
   case caenBridge_pulserB:
      pulSel = cvPulserB;
      break;
   }
   CAENVME_StartPulser(hvme, pulSel);
}

/*------------------------------------------------------------------*/

void caenBridge_PulserStop(MVME_INTERFACE *mvme, WORD pulser)
{
   CVPulserSelect pulSel = cvPulserA;
   int hvme = mvme->handle;

   switch (pulser) {
   case caenBridge_pulserA:
      pulSel = cvPulserA;
      break;
   case caenBridge_pulserB:
      pulSel = cvPulserB;
      break;
   }
   CAENVME_StopPulser(hvme, pulSel);
}

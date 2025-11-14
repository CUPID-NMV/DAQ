/*********************************************************************

  $Id: caenBridge.h 269 2013-10-31 17:22:20Z tassielli $
  $Author:  $
  $Revision:  $

  Created by:   Giovanni F. Tassielli
  Based on v1718.h created by Jimmy Ngai

  Contents:     CAEN VME bridge  VX718 include

*********************************************************************/
#ifndef  CAENBRIDGE_INCLUDE_H
#define  CAENBRIDGE_INCLUDE_H

#include <stdio.h>
#include <string.h>
#include "mvmestd.h"

#include "CAENVMElib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define  CAENBRIDGE_STATUS_RO          (DWORD) (0x0000)
#define  CAENBRIDGE_VME_CTRL_RW        (DWORD) (0x0001)
#define  CAENBRIDGE_FW_REV_RO          (DWORD) (0x0002)
#define  CAENBRIDGE_FW_DWNLD_RW        (DWORD) (0x0003)
#define  CAENBRIDGE_FL_ENA_RW          (DWORD) (0x0004)
#define  CAENBRIDGE_IRQ_STAT_RO        (DWORD) (0x0005)
#define  CAENBRIDGE_IN_REG_RW          (DWORD) (0x0008)
#define  CAENBRIDGE_OUT_REG_S_RW       (DWORD) (0x000A)
#define  CAENBRIDGE_IN_MUX_S_RW        (DWORD) (0x000B)
#define  CAENBRIDGE_OUT_MUX_S_RW       (DWORD) (0x000C)
#define  CAENBRIDGE_LED_POL_S_RW       (DWORD) (0x000D)
#define  CAENBRIDGE_OUT_REG_C_WO       (DWORD) (0x0010)
#define  CAENBRIDGE_IN_MUX_C_WO        (DWORD) (0x0011)
#define  CAENBRIDGE_OUT_MAX_C_WO       (DWORD) (0x0012)
#define  CAENBRIDGE_LED_POL_C_WO       (DWORD) (0x0013)
#define  CAENBRIDGE_PULSEA_0_RW        (DWORD) (0x0016)
#define  CAENBRIDGE_PULSEA_1_RW        (DWORD) (0x0017)
#define  CAENBRIDGE_PULSEB_0_RW        (DWORD) (0x0019)
#define  CAENBRIDGE_PULSEB_1_RW        (DWORD) (0x001A)
#define  CAENBRIDGE_SCALER0_RW         (DWORD) (0x001C)
#define  CAENBRIDGE_SCALER1_RO         (DWORD) (0x001D)
#define  CAENBRIDGE_DISP_ADL_RO        (DWORD) (0x0020)
#define  CAENBRIDGE_DISP_ADH_RO        (DWORD) (0x0021)
#define  CAENBRIDGE_DISP_DTL_RO        (DWORD) (0x0022)
#define  CAENBRIDGE_DISP_DTH_RO        (DWORD) (0x0023)
#define  CAENBRIDGE_DISP_PC1_RO        (DWORD) (0x0024)
#define  CAENBRIDGE_DISP_PC2_RO        (DWORD) (0x0025)
#define  CAENBRIDGE_LM_ADL_RW          (DWORD) (0x0028)
#define  CAENBRIDGE_LM_ADH_RW          (DWORD) (0x0029)
#define  CAENBRIDGE_LM_C_RW            (DWORD) (0x002C)

WORD caenBridge_Read16(MVME_INTERFACE *mvme, DWORD base, int offset);
void caenBridge_Write16(MVME_INTERFACE *mvme, DWORD base, int offset, WORD value);
DWORD caenBridge_Read32(MVME_INTERFACE *mvme, DWORD base, int offset);
void caenBridge_Write32(MVME_INTERFACE *mvme, DWORD base, int offset, DWORD value);

void caenBridge_MultiRead(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle, int *am, int *dmode);
void caenBridge_MultiWrite(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle, int *am, int *dmode);
void caenBridge_MultiRead16(MVME_INTERFACE *mvme, DWORD *addrs, WORD *value, int ncycle);
void caenBridge_MultiWrite16(MVME_INTERFACE *mvme, DWORD *addrs, WORD *value, int ncycle);
void caenBridge_MultiRead32(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle);
void caenBridge_MultiWrite32(MVME_INTERFACE *mvme, DWORD *addrs, DWORD *value, int ncycle);

void caenBridge_PulserConfSet(MVME_INTERFACE *mvme, WORD pulser, DWORD period, DWORD width, WORD pulseNo);
void caenBridge_PulserStart(MVME_INTERFACE *mvme, WORD pulser);
void caenBridge_PulserStop(MVME_INTERFACE *mvme, WORD pulser);

  enum caenBridge_PulserSelect {
    caenBridge_pulserA=0x0,
    caenBridge_pulserB=0x1,
  };

  typedef struct CAENBRIDGEPAR {
      CVBoardTypes model;
      short Link;
  } CaenBridgePar;

  void CaenBrdgResetDefaultPar();

  void CaenBrdgSetPar( CVBoardTypes model, short Link);


#ifdef __cplusplus
}
#endif

#endif // CAENBRIDGE_INCLUDE_H

/* emacs
 * Local Variables:
 * mode:C
 * mode:font-lock
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */

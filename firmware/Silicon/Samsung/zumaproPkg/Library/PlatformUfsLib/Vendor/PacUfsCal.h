/**
  Scalar bridge to the vendor UFS calibration.

  The vendor headers and this tree's UfsCalAdapterLib.h both define an
  anonymous enum containing GEAR_1..GEAR_4, so they cannot be included in the
  same translation unit. This header carries nothing but plain scalars, so
  both sides can include it and neither has to see the other's definitions.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _PAC_UFS_CAL_H_
#define _PAC_UFS_CAL_H_

#include <Base.h>

//
// Everything the vendor calibration needs about this host, as scalars.
//
typedef struct {
  VOID    *Std;       // standard UFSHCI registers
  VOID    *Hci;       // vendor-specific HCI registers
  VOID    *UniPro;
  VOID    *Pma;       // PHY
  VOID    *Cport;
  UINT32   MclkRate;
  UINT8    Lanes;
  UINT8    Gear;
  UINT8    Mode;      // FAST_MODE etc, same numbering both sides
  UINT8    HsSeries;
  UINT8    Board;
  UINT8    EvtVer;
} PAC_UFS_CAL_CTX;

//
// All return the vendor's enum ufs_cal_errno as a plain UINT32.
// Zero is UFS_CAL_NO_ERROR.
//
UINT32 PacUfsCalInit     (PAC_UFS_CAL_CTX *Ctx);
UINT32 PacUfsCalPreLink  (VOID);
UINT32 PacUfsCalPostLink (VOID);
UINT32 PacUfsCalPrePmc   (VOID);
UINT32 PacUfsCalPostPmc  (VOID);

#endif

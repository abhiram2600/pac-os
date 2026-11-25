/**
  Adapter between this firmware and the vendor UFS calibration.

  This is the only file that includes the vendor headers. It keeps the
  persistent parameter block the calibration expects, supplies the delay
  function it calls through its handle, and wraps each entry point in the
  MPHY APB clock enable the stock driver performs around the same calls.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include "PacUfsCal.h"

//
// Order matters, and the vendor source says so:
//   ufs-vs-mmio.h > ufs-cal-if.h > ufs-cal.h
//
// __UFS_CAL_FW__ is deliberately NOT defined, so the kernel code path is
// taken. See Vendor/README.md: the vendor's own firmware path uses floating
// point, drops the PLL lock timeout and breaks two retry loops.
//
// The shim first: ufs-vs-mmio.h uses readl/writel and the kernel integer
// types without including anything for them, relying on its includer. In the
// kernel that is ufs-cal-if.c pulling in linux/io.h and friends beforehand.
#include <linux/types.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "ufs-vs-mmio.h"
#include "ufs-cal-if.h"

#define PAC_UFS_HOST_INDEX  0

//
// HCI_FORCE_HCS, in the vendor-specific HCI block (ufs-vs-regs.h).
//
#define PAC_HCI_FORCE_HCS            0xB4
#define PAC_UNIPRO_MCLK_STOP_EN      (1u << 5)
#define PAC_MPHY_APBCLK_STOP_EN      (1u << 10)

STATIC struct ufs_cal_param   mParam;
STATIC struct ufs_vs_handle   mHandle;
STATIC struct uic_pwr_mode    mPmd;

STATIC
VOID
PacUfsUdelay (
  u32  Microseconds
  )
{
  MicroSecondDelay (Microseconds);
}

/**
  Enable the MPHY APB clock around a calibration call, exactly as
  ufs_call_cal() does in the stock driver: clear both stop-enable bits
  beforehand, restore the MPHY one afterwards.
**/
STATIC
VOID
PacUfsMphyApbEnable (
  BOOLEAN  Enable
  )
{
  UINTN   Reg = (UINTN)mHandle.hci + PAC_HCI_FORCE_HCS;
  UINT32  Value;

  if (mHandle.hci == NULL) {
    return;
  }

  Value = MmioRead32 (Reg);

  if (Enable) {
    Value &= ~(PAC_UNIPRO_MCLK_STOP_EN | PAC_MPHY_APBCLK_STOP_EN);
  } else {
    Value |= PAC_MPHY_APBCLK_STOP_EN;
  }

  MmioWrite32 (Reg, Value);
}

UINT32
PacUfsCalInit (
  PAC_UFS_CAL_CTX  *Ctx
  )
{
  enum ufs_cal_errno  Result;

  ZeroMem (&mParam,  sizeof (mParam));
  ZeroMem (&mHandle, sizeof (mHandle));
  ZeroMem (&mPmd,    sizeof (mPmd));

  mHandle.std    = Ctx->Std;
  mHandle.hci    = Ctx->Hci;
  mHandle.unipro = Ctx->UniPro;
  mHandle.pma    = Ctx->Pma;
  mHandle.cport  = Ctx->Cport;
  mHandle.udelay = PacUfsUdelay;

  mPmd.lane      = Ctx->Lanes;
  mPmd.gear      = Ctx->Gear;
  mPmd.mode      = Ctx->Mode;
  mPmd.hs_series = Ctx->HsSeries;

  mParam.handle            = &mHandle;
  mParam.pmd               = &mPmd;
  mParam.available_lane    = Ctx->Lanes;
  mParam.connected_tx_lane = Ctx->Lanes;
  mParam.connected_rx_lane = Ctx->Lanes;
  mParam.active_tx_lane    = Ctx->Lanes;
  mParam.active_rx_lane    = Ctx->Lanes;
  mParam.mclk_rate         = Ctx->MclkRate;
  mParam.max_gear          = Ctx->Gear;
  mParam.board             = Ctx->Board;
  mParam.evt_ver           = Ctx->EvtVer;
  mParam.tbl               = HOST_EMBD;

  PacUfsMphyApbEnable (TRUE);
  Result = ufs_cal_init (&mParam, PAC_UFS_HOST_INDEX);
  PacUfsMphyApbEnable (FALSE);

  return (UINT32)Result;
}

#define PAC_UFS_CAL_STAGE(Name, VendorFn)       \
  UINT32                                        \
  Name (                                        \
    VOID                                        \
    )                                           \
  {                                             \
    enum ufs_cal_errno  Result;                 \
                                                \
    if (mParam.handle == NULL) {                \
      return (UINT32)UFS_CAL_ERROR;             \
    }                                           \
                                                \
    PacUfsMphyApbEnable (TRUE);                 \
    Result = VendorFn (&mParam);                \
    PacUfsMphyApbEnable (FALSE);                \
                                                \
    return (UINT32)Result;                      \
  }

PAC_UFS_CAL_STAGE (PacUfsCalPreLink,  ufs_cal_pre_link)
PAC_UFS_CAL_STAGE (PacUfsCalPostLink, ufs_cal_post_link)
PAC_UFS_CAL_STAGE (PacUfsCalPrePmc,   ufs_cal_pre_pmc)
PAC_UFS_CAL_STAGE (PacUfsCalPostPmc,  ufs_cal_post_pmc)

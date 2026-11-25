/**
  The PlatformUfsLib calibration entry points for the Tensor G4 (zumapro).

  These only translate: UfsDxe's struct UfsCalParam goes in, the vendor
  calibration's scalars come out the other side. The work itself is in
  Vendor/, and this file never includes the vendor headers, because both
  define GEAR_1..GEAR_4 and they cannot meet in one translation unit.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UfsHostBridge.h>
#include <Library/UfsCalAdapterLib.h>

#include "Vendor/PacUfsCal.h"

//
// brd-for-cal in the ufs@13200000 device tree node, and confirmed by the
// stock kernel's own probe output: "evt version : 0, board: 1".
//
#define PAC_UFS_BOARD  1

//
// Vendor HCI registers, offsets from the stock driver's ufs-vs-regs.h.
//
#define PAC_HCI_CLKSTOP_CTRL     0xB0
#define PAC_REFCLKOUT_STOP       (1u << 4)
#define PAC_MPHY_APBCLK_STOP     (1u << 3)
#define PAC_REFCLK_STOP          (1u << 2)
#define PAC_UNIPRO_MCLK_STOP     (1u << 1)
#define PAC_UNIPRO_PCLK_STOP     (1u << 0)
#define PAC_CLK_STOP_ALL         (PAC_REFCLKOUT_STOP | PAC_REFCLK_STOP | \
                                  PAC_UNIPRO_MCLK_STOP | PAC_UNIPRO_PCLK_STOP)

//
// HCI_FORCE_HCS holds the *enables* for automatic clock stopping. Leaving
// them set lets the hardware gate the reference clock while the link is
// down, which is exactly when the device needs it to initialise its PHY.
//
#define PAC_HCI_FORCE_HCS        0xB4
#define PAC_REFCLKOUT_STOP_EN    (1u << 11)
#define PAC_MPHY_APBCLK_STOP_EN  (1u << 10)
#define PAC_UFSP_DRCG_EN         (1u << 8)
#define PAC_REFCLK_STOP_EN       (1u << 7)
#define PAC_UNIPRO_PCLK_STOP_EN  (1u << 6)
#define PAC_UNIPRO_MCLK_STOP_EN  (1u << 5)
#define PAC_HCI_CORECLK_STOP_EN  (1u << 4)
#define PAC_CLK_STOP_CTRL_EN_ALL (PAC_UFSP_DRCG_EN | PAC_MPHY_APBCLK_STOP_EN | \
                                  PAC_REFCLKOUT_STOP_EN | PAC_REFCLK_STOP_EN | \
                                  PAC_UNIPRO_PCLK_STOP_EN | PAC_UNIPRO_MCLK_STOP_EN)

//
// UniPro debug option suites. The stock driver writes these immediately
// before link startup, commented "keep phy context even if link startup
// fails and for unipro v1.8".
//
#define PAC_UNIP_PA_DBG_OPTION_SUITE_1  0x39A8
#define PAC_UNIP_PA_DBG_OPTION_SUITE_2  0x39B4
#define PAC_DBG_SUITE1_ENABLE           0x90913C1C
#define PAC_DBG_SUITE2_ENABLE           0xE01C115F

UINT8
UfsCalGetTargetBoard (
  VOID
  )
{
  return PAC_UFS_BOARD;
}

UfsCalError
UfsCalInit (
  struct UfsCalParam  *Param
  )
{
  struct UfsHost   *Ufs;
  PAC_UFS_CAL_CTX   Ctx;
  UINT32            Result;

  if (Param == NULL || Param->Host == NULL || Param->Pmd == NULL) {
    return UFS_CAL_INV_ARG;
  }

  Ufs = (struct UfsHost *)Param->Host;

  Ctx.Std      = Ufs->IoAddr;
  Ctx.Hci      = Ufs->VsAddr;
  Ctx.UniPro   = Ufs->UniProAddr;
  Ctx.Pma      = Ufs->PhyPma;
  // The calibration never touches cport; it only writes the vendor HCI,
  // UniPro and PHY blocks through its handle. struct UfsHost has no
  // cport pointer, so leave it null rather than invent one.
  Ctx.Cport    = NULL;
  Ctx.MclkRate = Param->MclkRate;
  Ctx.Lanes    = Param->ActiveTxLane;
  Ctx.Gear     = Param->MaxGear;
  Ctx.Mode     = Param->Pmd->Mode;
  Ctx.HsSeries = Param->Pmd->HsSeries;
  Ctx.Board    = Param->Board;
  Ctx.EvtVer   = Param->EvtVer;

  DEBUG ((
    EFI_D_WARN,
    "UFS cal: mclk %u, %u lane(s), gear %u, hs-series %u, board %u, evt %u\n",
    Ctx.MclkRate,
    Ctx.Lanes,
    Ctx.Gear,
    Ctx.HsSeries,
    Ctx.Board,
    Ctx.EvtVer
    ));

  Result = PacUfsCalInit (&Ctx);
  if (Result != UFS_CAL_NO_ERROR) {
    DEBUG ((EFI_D_ERROR, "UFS cal: init failed, %u\n", Result));
  }

  return (UfsCalError)Result;
}

UfsCalError
UfsCalPreLink (
  struct UfsCalParam  *Param
  )
{
  struct UfsHost  *Ufs;
  UINT32           Value;

  if (Param == NULL || Param->Host == NULL) {
    return UFS_CAL_INV_ARG;
  }

  Ufs = (struct UfsHost *)Param->Host;

  //
  // Two things the stock driver does at this exact point, in
  // exynos_ufs_link_startup_notify()'s PRE_CHANGE case, and that UfsDxe
  // does not do for itself.
  //
  // First, ungate the clocks. CLK_STOP_ALL in HCI_CLKSTOP_CTRL covers the
  // reference clock handed out to the UFS device as well as the UniPro
  // clocks, and the device cannot answer link startup without its reference
  // clock running. The driver's pre-setup issues a software reset just
  // before here, so this has to be done now rather than in board init.
  //
  Value  = MmioRead32 ((UINTN)Ufs->VsAddr + PAC_HCI_CLKSTOP_CTRL);
  Value &= ~(UINT32)PAC_CLK_STOP_ALL;
  MmioWrite32 ((UINTN)Ufs->VsAddr + PAC_HCI_CLKSTOP_CTRL, Value);

  //
  // And disable the automatic gating itself, not just the current stop
  // state. The stock driver re-enables these once the link is up; while it
  // is coming up they only risk pulling the reference clock away from a
  // device that is trying to initialise.
  //
  Value  = MmioRead32 ((UINTN)Ufs->VsAddr + PAC_HCI_FORCE_HCS);
  Value &= ~(UINT32)PAC_CLK_STOP_CTRL_EN_ALL;
  MmioWrite32 ((UINTN)Ufs->VsAddr + PAC_HCI_FORCE_HCS, Value);

  //
  // Second, the UniPro debug option suites. The driver's own comment says
  // these keep the PHY context across a failed link startup and are needed
  // for UniPro 1.8, which this controller is.
  //
  MmioWrite32 (
    (UINTN)Ufs->UniProAddr + PAC_UNIP_PA_DBG_OPTION_SUITE_1,
    PAC_DBG_SUITE1_ENABLE
    );
  MmioWrite32 (
    (UINTN)Ufs->UniProAddr + PAC_UNIP_PA_DBG_OPTION_SUITE_2,
    PAC_DBG_SUITE2_ENABLE
    );

  DEBUG ((
    EFI_D_WARN,
    "UFS cal: clocks forced on (CLKSTOP=0x%08x FORCE_HCS=0x%08x), suites set\n",
    MmioRead32 ((UINTN)Ufs->VsAddr + PAC_HCI_CLKSTOP_CTRL),
    MmioRead32 ((UINTN)Ufs->VsAddr + PAC_HCI_FORCE_HCS)
    ));

  return (UfsCalError)PacUfsCalPreLink ();
}

UfsCalError
UfsCalPostLink (
  struct UfsCalParam  *Param
  )
{
  return (UfsCalError)PacUfsCalPostLink ();
}

UfsCalError
UfsCalPrePmc (
  struct UfsCalParam  *Param
  )
{
  return (UfsCalError)PacUfsCalPrePmc ();
}

UfsCalError
UfsCalPostPmc (
  struct UfsCalParam  *Param
  )
{
  return (UfsCalError)PacUfsCalPostPmc ();
}

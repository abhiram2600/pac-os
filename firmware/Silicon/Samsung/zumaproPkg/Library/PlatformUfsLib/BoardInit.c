/**
  UFS board bring-up for the Google Tensor G4 (zumapro).

  Every address and value here was read off the device itself, from the
  ufs@13200000 node of its device tree and from the stock kernel's own probe
  output. See pac-os/UFS-FACTS.md for the provenance of each one.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/TimerLib.h>
#include <Library/UfsHostBridge.h>
#include <Library/UfsCalAdapterLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFIGpio.h>

#include "Vendor/PacUfsCal.h"

//
// Offsets within ufs@13200000's first reg range. The node lists six ranges;
// these are the four we need, expressed relative to the first so a single
// mapped region covers them:
//
//   0x13200000 len 0x200    standard UFSHCI
//   0x13201100 len 0x2000   vendor-specific HCI
//   0x13204000 len 0x4000   PHY (PMA)
//   0x13208000 len 0x804    cport
//
#define PAC_UFS_VS_OFFSET      0x1100
#define PAC_UFS_PMA_OFFSET     0x4000
#define PAC_UFS_CPORT_OFFSET   0x8000

//
// Vendor-specific HCI registers, offsets from ufs-vs-regs.h in the stock
// driver.
//
#define PAC_HCI_1US_TO_CNT_VAL     0x0C
#define PAC_HCI_UFSHCI_V2P1_CTRL   0x8C
#define PAC_IA_TICK_SEL            (1u << 16)
#define PAC_CNT_VAL_1US_MASK       0x3FF

//
// ufs-iocc in the device tree: offset 0x710 in sysreg_ufs, mask 0x3, value
// 0x3. The stock kernel echoes it back at probe:
//   "ufs-iocc: offset 0x710, mask 0x3, value 0x3"
//
#define PAC_SYSREG_UFS_IOCC        0x710
#define PAC_SYSREG_UFS_IOCC_MASK   0x3
#define PAC_SYSREG_UFS_IOCC_VAL    0x3

//
// ufs-phy-iso in the device tree: offset 0x3EC0 in the PMU, mask 0x1,
// value 0x1. Releasing PHY isolation is what lets the PHY be driven at all.
//
#define PAC_PMU_UFS_PHY_ISO        0x3EC0
#define PAC_PMU_UFS_PHY_ISO_BIT    0

//
// PMU writes must go through EL3.
//
// PMU_ALIVE is write-protected against Non-secure access on this SoC: a
// plain store raises an uncontainable external abort, and because SErrors
// are asynchronous it surfaces somewhere else entirely. Existing UEFI
// board-init code for other Exynos parts uses MmioWrite32 here and would
// fault on this chip.
//
// The stock kernel never writes it directly either. Writes below offset
// 0x4000 go through an atomic bit-set alias serviced by the EL3 monitor,
// per drivers/soc/google/exynos-pmu-if.c, and set_priv_reg() is
// SMC 0x82000504 option 1, per drivers/soc/google/exynos-pd_el3.c. The
// argument is the bit index, not a mask.
//
#define PAC_PMU_BASE               0x15460000ULL
#define PAC_PMU_ATOMIC_SET_BIT     0xC000
#define SMC_CMD_PRIV_REG           0x82000504
#define PRIV_REG_OPTION_WRITE      1

//
// Link parameters, from the device tree and confirmed by the stock kernel:
//   mclk: 178000000
//   ufs,pmd-attr-lane = 2, ufs,pmd-attr-gear = 4
//   Power mode change(0): M(1)G(4)L(2)HS-series(2)
//
#define PAC_UFS_MCLK_RATE          178000000
#define PAC_UFS_LANES              2
#define PAC_UFS_MAX_GEAR           4
#define PAC_UFS_PWR_MODE           1   // FAST_MODE
#define PAC_UFS_HS_SERIES          2   // HS-series B

//
// The two pins the UFS link needs, from pinctrl@13060000 in the device tree:
//
//   ufs-refclk-out  gph5-0  function 2  pull none  drive 2
//   ufs-rst-n       gph5-1  function 2  pull none
//
// The reference clock matters most: without it the device is detected but
// its PHY cannot initialise, which shows up as link startup failing with
// HCS.DP set and UECPA reporting a generic PHY adapter error.
//
// gph5 is the only bank in that controller, entry
// { .CtrlNum = 4, .Id = BANK_ID_H, .Number = 5, .Offset = 0x0 } in
// zumaproPkg's GpioBankLib.
//
#define PAC_UFS_GPIO_BANK          5
#define PAC_UFS_REFCLK_PIN         0
#define PAC_UFS_RESET_PIN          1

//
// The UFS device's own supply.
//
// It is not on the PMIC: the device tree declares
//
//   fixedregulator@0 {
//           regulator-name = "ufs-vcc";
//           gpio = <&gpp0 1 0>;
//           enable-active-high;
//   };
//
// so it is a plain GPIO, gpp0 pin 1, driven high to enable. gpp0 is the
// first bank of pinctrl@10840000, entry
// { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 0, .Offset = 0x0 } in
// zumaproPkg's GpioBankLib.
//
// This matters because the bootloader powers the device down before handing
// over, its log ending with "UFS shutdown complete". Without turning it back
// on, the controller comes up fine and the PHY calibrates, but HCS reports
// DEVICE_PRESENT clear and link startup fails with a generic PHY adapter
// error, because there is nothing on the other end.
//
#define PAC_UFS_VCC_BANK           0
#define PAC_UFS_VCC_PIN            1

STATIC EFI_GPIO_PROTOCOL  *mGpioProtocol;

STATIC
VOID
PacPmuSetBit (
  IN UINT32  Offset,
  IN UINT32  BitIndex
  )
{
  ARM_SMC_ARGS  Args;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_PRIV_REG;
  Args.Arg1 = (UINTN)(PAC_PMU_BASE + (Offset | PAC_PMU_ATOMIC_SET_BIT));
  Args.Arg2 = PRIV_REG_OPTION_WRITE;
  Args.Arg3 = BitIndex;
  ArmCallSmc (&Args);
}

EFI_STATUS
UfsBoardInit (
  struct UfsHost  *Ufs
  )
{
  EFI_STATUS                    Status;
  EFI_MEMORY_REGION_DESCRIPTOR  UfsRegion;
  EFI_MEMORY_REGION_DESCRIPTOR  UniProRegion;
  EFI_MEMORY_REGION_DESCRIPTOR  ProtectorRegion;
  EFI_MEMORY_REGION_DESCRIPTOR  SysRegRegion;
  UINT32                        Value;

  if (Ufs == NULL || Ufs->CalParam == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&UfsRegion,       sizeof (UfsRegion));
  ZeroMem (&UniProRegion,    sizeof (UniProRegion));
  ZeroMem (&ProtectorRegion, sizeof (ProtectorRegion));
  ZeroMem (&SysRegRegion,    sizeof (SysRegRegion));

  Status = LocateMemoryRegionByName ("UFS", &UfsRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UFS: no \"UFS\" memory region!\n"));
    return Status;
  }

  Status = LocateMemoryRegionByName ("UFS UniPro", &UniProRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UFS: no \"UFS UniPro\" memory region!\n"));
    return Status;
  }

  Status = LocateMemoryRegionByName ("UFS Protector", &ProtectorRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UFS: no \"UFS Protector\" memory region!\n"));
    return Status;
  }

  Status = LocateMemoryRegionByName ("SYSREG UFS", &SysRegRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UFS: no \"SYSREG UFS\" memory region!\n"));
    return Status;
  }

  Ufs->IoAddr     = (VOID *)(UINTN)UfsRegion.Address;
  Ufs->VsAddr     = (VOID *)(UINTN)(UfsRegion.Address + PAC_UFS_VS_OFFSET);
  Ufs->PhyPma     = (VOID *)(UINTN)(UfsRegion.Address + PAC_UFS_PMA_OFFSET);
  Ufs->UniProAddr = (VOID *)(UINTN)UniProRegion.Address;
  Ufs->UfsPaddr   = (VOID *)(UINTN)ProtectorRegion.Address;
  Ufs->PhyIsoAddr = (VOID *)(UINTN)(PAC_PMU_BASE + PAC_PMU_UFS_PHY_ISO);
  Ufs->DevPwrAddr = NULL;

  DEBUG ((
    EFI_D_WARN,
    "UFS: hci 0x%lx vs 0x%lx unipro 0x%lx pma 0x%lx ufsp 0x%lx\n",
    (UINTN)Ufs->IoAddr,
    (UINTN)Ufs->VsAddr,
    (UINTN)Ufs->UniProAddr,
    (UINTN)Ufs->PhyPma,
    (UINTN)Ufs->UfsPaddr
    ));

  //
  // Mux the UFS pins. Nothing else in this firmware applies pin
  // configuration, and the bootloader shut UFS down before handing over, so
  // without this the reference clock never reaches the device.
  //
  Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UFS: no GPIO protocol! %r\n", Status));
    return Status;
  }

  //
  // Power the device first, then give it a moment before touching the link.
  //
  //
  // Check every one of these. Silently ignoring them hid, for several
  // attempts, whether the pins were being configured at all.
  //
  {
    BOOLEAN     Enable = TRUE;
    EFI_STATUS  S1, S2, S3, S4, S5, S6, S7;

    S1 = mGpioProtocol->SetPull (BANK_ID_P, PAC_UFS_VCC_BANK, PAC_UFS_VCC_PIN, PULL_NONE);
    S2 = mGpioProtocol->SetFunction (BANK_ID_P, PAC_UFS_VCC_BANK, PAC_UFS_VCC_PIN, FUNCTION_OUTPUT);
    S3 = mGpioProtocol->SetState (BANK_ID_P, PAC_UFS_VCC_BANK, PAC_UFS_VCC_PIN, &Enable);

    DEBUG ((
      EFI_D_WARN,
      "UFS: ufs-vcc gpp0-1: pull=%r func=%r state=%r\n",
      S1, S2, S3
      ));

    // UFS devices want their supply stable well before initialisation.
    MicroSecondDelay (10000);

    S4 = mGpioProtocol->SetPull (BANK_ID_H, PAC_UFS_GPIO_BANK, PAC_UFS_REFCLK_PIN, PULL_NONE);
    S5 = mGpioProtocol->SetPull (BANK_ID_H, PAC_UFS_GPIO_BANK, PAC_UFS_RESET_PIN,  PULL_NONE);
    S6 = mGpioProtocol->SetFunction (BANK_ID_H, PAC_UFS_GPIO_BANK, PAC_UFS_REFCLK_PIN, FUNCTION_2);
    S7 = mGpioProtocol->SetFunction (BANK_ID_H, PAC_UFS_GPIO_BANK, PAC_UFS_RESET_PIN,  FUNCTION_2);

    DEBUG ((
      EFI_D_WARN,
      "UFS: gph5 pins: pull0=%r pull1=%r func0=%r func1=%r\n",
      S4, S5, S6, S7
      ));

    // Read the controller registers straight back, so we see the hardware
    // state rather than trusting the calls.
    DEBUG ((
      EFI_D_WARN,
      "UFS: gpp0 CON=0x%08x DAT=0x%08x   gph5 CON=0x%08x DAT=0x%08x\n",
      MmioRead32 (0x10840000),
      MmioRead32 (0x10840004),
      MmioRead32 (0x13060000),
      MmioRead32 (0x13060004)
      ));
  }

  // Release PHY isolation, through EL3.
  PacPmuSetBit (PAC_PMU_UFS_PHY_ISO, PAC_PMU_UFS_PHY_ISO_BIT);

  // IO coherency. sysreg_ufs is not the PMU, so a direct write is correct
  // here.
  Value  = MmioRead32 ((UINTN)SysRegRegion.Address + PAC_SYSREG_UFS_IOCC);
  Value &= ~(UINT32)PAC_SYSREG_UFS_IOCC_MASK;
  Value |= PAC_SYSREG_UFS_IOCC_VAL;
  MmioWrite32 ((UINTN)SysRegRegion.Address + PAC_SYSREG_UFS_IOCC, Value);

  // Count a microsecond in mclk ticks, and select that as the tick source,
  // the same two writes exynos_ufs_fit_aggr_timeout() makes.
  Value  = MmioRead32 ((UINTN)Ufs->VsAddr + PAC_HCI_UFSHCI_V2P1_CTRL);
  Value |= PAC_IA_TICK_SEL;
  MmioWrite32 ((UINTN)Ufs->VsAddr + PAC_HCI_UFSHCI_V2P1_CTRL, Value);

  MmioWrite32 (
    (UINTN)Ufs->VsAddr + PAC_HCI_1US_TO_CNT_VAL,
    (PAC_UFS_MCLK_RATE / 1000000) & PAC_CNT_VAL_1US_MASK
    );

  // Hand UfsDxe the link parameters. It fills in Host, Board and EvtVer
  // itself before calling UfsCalInit.
  //
  // GearMode caps what UfsUpdateMaxGear will accept. It takes the smaller of
  // the device's PA_MaxRxHSGear and this field, so leaving it zero makes the
  // computed max gear zero and the high-speed power mode change then asks
  // for gear 0 and fails. This device reports PA_MaxRxHSGear(4).
  //
  Ufs->GearMode = PAC_UFS_MAX_GEAR;

  // No register gates this device's supply; it is the gpp0-1 GPIO above.
  Ufs->DevPwrShift = 0;

  Ufs->PmdCxt.Lane     = PAC_UFS_LANES;
  Ufs->PmdCxt.Gear     = PAC_UFS_MAX_GEAR;
  Ufs->PmdCxt.Mode     = PAC_UFS_PWR_MODE;
  Ufs->PmdCxt.HsSeries = PAC_UFS_HS_SERIES;

  Ufs->CalParam->Pmd             = &Ufs->PmdCxt;
  Ufs->CalParam->MclkRate        = PAC_UFS_MCLK_RATE;
  Ufs->CalParam->AvailableLane   = PAC_UFS_LANES;
  Ufs->CalParam->ConnectedTxLane = PAC_UFS_LANES;
  Ufs->CalParam->ConnectedRxLane = PAC_UFS_LANES;
  Ufs->CalParam->ActiveTxLane    = PAC_UFS_LANES;
  Ufs->CalParam->ActiveRxLane    = PAC_UFS_LANES;
  Ufs->CalParam->MaxGear         = PAC_UFS_MAX_GEAR;

  return EFI_SUCCESS;
}

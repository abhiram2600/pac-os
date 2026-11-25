/**
  Warm reset via the cluster 0 watchdog, for the Google Tensor G4 (zumapro).

  This exists for one reason: on this device a clean reset makes the
  bootloader re-initialise DRAM, which erases the debug log we keep in the
  ramoops pmsg zone. A watchdog reset is warm and DRAM survives it, which is
  the only way anything this firmware prints can be read back afterwards.

  The default ArmPsciResetSystemLib issues PSCI SYSTEM_RESET, a clean reset.
  That is correct behaviour, but while this port is being brought up it
  destroys the evidence: a run that got far enough for the boot manager to
  give up and reset came back with a completely empty log, telling us
  nothing about how far it had actually got.

  Every reset type is therefore a warm watchdog reset for now, including
  shutdown, which simply reboots instead. Revisit once the port boots.

  The watchdog register sequence and the PMU unlock mirror SEC. See
  SiliciumPkg/Sec/Sec.c for why the PMU half has to go through EL3: writing
  PMU_ALIVE directly raises an uncontainable external abort.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/ResetSystemLib.h>

#define PAC_PMU_BASE               0x15460000ULL
#define PAC_PMU_CL0_NONCPU_OUT     0x1220
#define PAC_PMU_CL0_NONCPU_INT_EN  0x1244
#define PAC_PMU_CNT_EN_BIT         4
#define PAC_PMU_NWDTRESET_BIT      2
#define PAC_PMU_ATOMIC_SET_BIT     0xC000

#define SMC_CMD_PRIV_REG           0x82000504
#define PRIV_REG_OPTION_WRITE      1

#define PAC_WDT_CL0_BASE           0x10060000ULL
#define PAC_WTCON                  0x00
#define PAC_WTDAT                  0x04
#define PAC_WTCNT                  0x08

#define PAC_WTCON_RSTEN            (1 << 0)
#define PAC_WTCON_ENABLE           (1 << 5)
#define PAC_WTCON_DIV128           (3 << 3)
#define PAC_WTCON_PRESCALE(x)      ((x) << 8)

// Roughly a second at this prescaler and divider.
#define PAC_WDT_COUNT              750
#define PAC_WDT_PRESCALE           0xFF

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

STATIC
VOID
PacWarmReset (VOID)
{
  volatile UINT32  *Wdt = (volatile UINT32 *)(UINTN)PAC_WDT_CL0_BASE;

  DEBUG ((EFI_D_WARN, "PAC-OS: warm reset via watchdog\n"));

  PacPmuSetBit (PAC_PMU_CL0_NONCPU_OUT,    PAC_PMU_CNT_EN_BIT);
  PacPmuSetBit (PAC_PMU_CL0_NONCPU_INT_EN, PAC_PMU_NWDTRESET_BIT);

  Wdt[PAC_WTCON / 4] = 0;
  Wdt[PAC_WTDAT / 4] = PAC_WDT_COUNT;
  Wdt[PAC_WTCNT / 4] = PAC_WDT_COUNT;
  Wdt[PAC_WTCON / 4] = PAC_WTCON_ENABLE | PAC_WTCON_RSTEN | PAC_WTCON_DIV128 |
                       PAC_WTCON_PRESCALE (PAC_WDT_PRESCALE);

  while (TRUE) {
  }
}

VOID
EFIAPI
ResetCold (VOID)
{
  PacWarmReset ();
}

VOID
EFIAPI
ResetWarm (VOID)
{
  PacWarmReset ();
}

VOID
EFIAPI
ResetShutdown (VOID)
{
  // No shutdown while bringing the port up; reboot so the log can be read.
  PacWarmReset ();
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID  *ResetData
  )
{
  PacWarmReset ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID           *ResetData OPTIONAL
  )
{
  PacWarmReset ();
}

/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>

#include "PlatformRegisters.h"

VOID
DisableWatchdogTimer ()
{
  //
  // Deliberately does nothing while bringing this port up.
  //
  // Nothing resets the Tensor G4 once our firmware has control: the ~30 s
  // "boot failure" that used to rescue us was the bootloader's own timeout
  // while it failed to load the image. SEC now arms cluster 0's watchdog as
  // a hang deadline instead, so disabling it here would wedge the phone on
  // any lock-up and require the battery to be pulled.
  //
  // Restore the disable (both clusters: cl0 at 0x10060000 and cl1 at
  // 0x10070000, WTCON bits 5 and 0) once the port reaches DXE and can pet
  // the watchdog itself.
  //
}

VOID
EnableFrameBufferWrites ()
{
  //
  // Deliberately does nothing on this SoC.
  //
  // The reference Exynos devices poke HW_SW_TRIG_CONTROL in the DECON
  // display controller here so the firmware can draw into the framebuffer
  // the bootloader left running. The Pixel 10a never gets that far: its
  // device tree reserves no splash framebuffer, and the bootloader's own log
  // shows the display coming up short:
  //
  //   [E] [DSIM] dsim fail to get panel operation
  //   [E] [DSIM] dsim_probe -: dsim1 probe failed
  //
  // So DECON is unpowered when we run, and writing to it raised an
  // uncontainable external abort. Being asynchronous, the SError sat pending
  // until ArmCpuDxe unmasked aborts, and surfaced there as
  //
  //   SError Exception at 0x...  ESR 0xBE000011  (EC 0x2F, DFSC 0x11)
  //
  // which made the CPU driver look like the broken thing when the real bad
  // access was back here in SEC.
  //
  // There is nothing to draw into on this device anyway; debug output goes
  // to the ramoops pmsg zone via RamoopsSerialPortLib instead.
  //
}

VOID
PlatformInitialize ()
{
  // Disable Watchdog Timer
  DisableWatchdogTimer ();

  // Enable Frame Buffer Writes
  EnableFrameBufferWrites ();
}

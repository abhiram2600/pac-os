/**
  A deliberately tiny UEFI application, used to prove the payload path.

  The point of this is not what it does but where it comes from: if this
  runs, the firmware has located a filesystem, read an executable off it and
  handed control over. That is the whole of step 5, and until it works there
  is no point building a real loader, because a failure could be in either.

  It prints to every console the firmware has, so it shows up both on the
  panel and in the ramoops log, then holds the screen for a while so a human
  can read it before the watchdog takes the phone back.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>

EFI_STATUS
EFIAPI
PacPayloadTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Index;

  //
  // DEBUG goes to the ramoops log, so this is readable over adb afterwards
  // even if nothing is visible on the panel.
  //
  DEBUG ((EFI_D_ERROR, "\n"));
  DEBUG ((EFI_D_ERROR, "========================================\n"));
  DEBUG ((EFI_D_ERROR, " PAC-OS payload running\n"));
  DEBUG ((EFI_D_ERROR, " Loaded and started from the payload volume\n"));
  DEBUG ((EFI_D_ERROR, "========================================\n"));

  if (SystemTable->ConOut != NULL) {
    SystemTable->ConOut->ClearScreen (SystemTable->ConOut);

    Print (L"\n");
    Print (L"  ====================================\n");
    Print (L"       P A C - O S   I S   A L I V E\n");
    Print (L"  ====================================\n");
    Print (L"\n");
    Print (L"  This is not Android and not the\n");
    Print (L"  Google bootloader.\n");
    Print (L"\n");
    Print (L"  Our own firmware is running on your\n");
    Print (L"  Pixel 10a. It brought up the SoC, the\n");
    Print (L"  MMU and the timer, lit this panel at\n");
    Print (L"  1080x2424, started the UFS storage\n");
    Print (L"  controller, read the disk, mounted a\n");
    Print (L"  filesystem, found this program on it\n");
    Print (L"  and started it.\n");
    Print (L"\n");
    Print (L"  Next: a real kernel loader goes here\n");
    Print (L"  instead of this message.\n");
    Print (L"\n");
    Print (L"  ------------------------------------\n");
    Print (L"\n");
  }

  //
  // Count down out loud, so a frozen screen is distinguishable from a
  // working one that has simply finished. Kept inside the firmware's
  // watchdog deadline, which takes the phone back to the bootloader on its
  // own, so nothing here can leave the device stuck.
  //
  for (Index = 10; Index > 0; Index--) {
    DEBUG ((EFI_D_ERROR, "payload: alive, %u\n", Index));
    if (SystemTable->ConOut != NULL) {
      Print (L"  running, %u seconds left on screen \r", Index);
    }

    gBS->Stall (1000000);
  }

  if (SystemTable->ConOut != NULL) {
    Print (L"\n\n  Handing the phone back to the bootloader.\n");
  }

  DEBUG ((EFI_D_ERROR, "payload: returning to the firmware\n"));

  return EFI_SUCCESS;
}

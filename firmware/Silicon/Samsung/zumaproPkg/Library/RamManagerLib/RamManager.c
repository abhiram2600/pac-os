/**
  Usable DRAM ranges for the Google Tensor G4 (zumapro).

  The Samsung implementation of this library reads the total memory size out
  of a word at NS_IRAM + 0x848, left there by the Exynos bootloader. Tensor
  has no iram/sram node at all, so that lookup fails and RamManagerDxe
  asserts:

    Failed to Locate "NS_IRAM" Memory Region!
    ASSERT [RamManagerDxe] AutoGen.c(337)

  We do not need the bootloader to tell us, because the layout is in the
  device tree. Note the 8 GB is NOT contiguous: memory@80000000,
  memory@880000000, memory@900000000 and memory@980000000, 2 GB each, with a
  large hole in between.

  The ranges below are the two large stretches of the first bank that are
  genuinely free, taken from the running device's /proc/iomem and
  cross-checked against the bootloader's own log. RamManagerDxe walks the
  EFI memory map within each range and only claims the gaps, so regions we
  already declare in MemoryMapLib are left alone.

  Deliberately conservative. The upper banks are left out for now because
  most of their span shows as reserved on the live device, and claiming
  memory the secure world is using would corrupt it.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/RamManagerLib.h>

STATIC CONST EFI_MEMORY_RANGE mUsableRanges[] = {
  //
  // 0x97400000 - 0xB6200000.
  //
  // /proc/iomem reports this whole span as System RAM with nothing reserved
  // inside it. It ends where the secure DRAM carve-out begins, which the
  // bootloader log states outright:
  //
  //   [BOOT] secure dram base 0xb6200000, size 0x9e00000
  //
  // The bootloader does stage the Android kernel image around 0xA0011000
  // inside this range, but the boot shim has already relocated us to
  // PcdFdBaseAddress by the time this runs, so that copy is dead.
  //
  { 0x97400000, 0x1EE00000 },

  //
  // 0xE3800000 - 0xF6200000.
  //
  // Also System RAM. It starts right above the secure page table carve-out,
  // again per the bootloader log:
  //
  //   [BOOT] secure PGTBL base 0xe0000000, size 58720256   (0x3800000)
  //
  // and stops short of the lone reserved page at 0xF6200000. Our own FD,
  // stack and DXE heap live inside this range, but they are declared in
  // MemoryMapLib and so appear in the EFI memory map, which keeps
  // RamManagerDxe from handing them out twice.
  //
  { 0xE3800000, 0x12A00000 },
};

EFI_STATUS
GetUsableMemoryRanges (
  OUT EFI_MEMORY_RANGE  **Range,
  OUT UINT8              *RangeCount
  )
{
  EFI_MEMORY_RANGE  *LocalRange;
  UINT8              Index;

  // RamManagerDxe frees this buffer, so it has to come from the pool rather
  // than be a pointer to the static table above.
  LocalRange = AllocateZeroPool (sizeof (mUsableRanges));
  if (LocalRange == NULL) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate Memory for Memory Ranges!\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < ARRAY_SIZE (mUsableRanges); Index++) {
    LocalRange[Index] = mUsableRanges[Index];

    DEBUG ((
      EFI_D_WARN,
      "Usable RAM Range: 0x%lx - 0x%lx\n",
      mUsableRanges[Index].Address,
      mUsableRanges[Index].Address + mUsableRanges[Index].Length
      ));
  }

  *Range      = LocalRange;
  *RangeCount = ARRAY_SIZE (mUsableRanges);

  return EFI_SUCCESS;
}

/**
  SerialPortLib backed by the device's ramoops pmsg zone.

  The Pixel 10a has no usable serial port and reserves no splash
  framebuffer, so neither of the stock SerialPortLib implementations can
  show anything: the framebuffer one draws into memory that does not exist,
  and the null one discards everything. That left the firmware completely
  silent.

  This writes every byte of debug output into the device's ramoops pmsg
  zone instead. After a warm reset the stock Android kernel finds the zone
  and exposes it at /sys/fs/pstore/pmsg-ramoops-0, so the entire UEFI debug
  log can be read back over adb.

  It shares one append-only buffer with the SEC boot trace in
  SiliciumPkg/Sec/Sec.c, using the same header layout, so SEC checkpoints
  and DXE debug output interleave in one log.

  Layout is struct persistent_ram_buffer from the kernel's
  fs/pstore/ram_core.c:

    +0x0  UINT32 Sig   = PERSISTENT_RAM_SIG, 0x43474244 ("DBGC")
    +0x4  UINT32 Start = read cursor, kept at 0 so the log stays linear
    +0x8  UINT32 Size  = valid bytes in Data
    +0xC  UINT8  Data[]

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Library/SerialPortLib.h>
#include <Library/CacheMaintenanceLib.h>

//
// The ramoops region starts at 0xFD3FF000 with a 2 MB console zone, then a
// 2 MB pmsg zone. The console zone belongs to the Pixel bootloader, which
// writes its own log there and runs after us on every boot retry, so we
// must stay in pmsg or our output gets overwritten before it is read.
//
#define PAC_TRACE_BASE  0xFD5FF000ULL
#define PAC_TRACE_SIG   0x43474244U

//
// Leave headroom inside the 2 MB zone. The kernel refuses to surface a
// buffer whose Size exceeds the zone minus its header.
//
#define PAC_TRACE_MAX   0x001F0000U

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN   NumberOfBytes
  )
{
  volatile UINT32  *Header = (volatile UINT32 *)(UINTN)PAC_TRACE_BASE;
  volatile UINT8   *Data   = (volatile UINT8 *)(UINTN)(PAC_TRACE_BASE + 12);
  UINT32            Start;
  UINT32            Offset;
  UINTN             Index;

  if (Buffer == NULL || NumberOfBytes == 0) {
    return 0;
  }

  // Claim the buffer if nobody has stamped it yet, or if its length field
  // is nonsense (stale content from a previous owner).
  if (Header[0] != PAC_TRACE_SIG || Header[2] > PAC_TRACE_MAX) {
    Header[0] = PAC_TRACE_SIG;
    Header[1] = 0;
    Header[2] = 0;
  }

  Start  = Header[2];
  Offset = Start;

  for (Index = 0; Index < NumberOfBytes; Index++) {
    if (Offset >= PAC_TRACE_MAX) {
      break;
    }
    Data[Offset++] = Buffer[Index];
  }

  Header[1] = 0;
  Header[2] = Offset;

  // Flush only the header and the bytes just appended. Flushing the whole
  // log on every write would be quadratic, and this runs on every DEBUG()
  // in the firmware.
  WriteBackDataCacheRange ((VOID *)(UINTN)PAC_TRACE_BASE, 12);
  if (Offset > Start) {
    WriteBackDataCacheRange ((VOID *)(UINTN)(PAC_TRACE_BASE + 12 + Start), Offset - Start);
  }

  return Index;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN   NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll ()
{
  return FALSE;
}

EFI_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortInitialize ()
{
  return EFI_SUCCESS;
}

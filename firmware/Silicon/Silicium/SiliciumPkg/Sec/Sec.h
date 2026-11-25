/**
  Copyright (c) 2011 - 2020, Arm Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _SEC_H_
#define _SEC_H_

//
// PAC-OS boot trace and self-reset. Defined in Sec.c, used from both Sec.c
// and the exception handler in AArch64/ArchSec.c.
//
VOID
PacTraceInit (VOID);

VOID
PacTrace (
  IN CONST CHAR8  *Message
  );

VOID
PacArmWatchdog (
  IN UINT32  Count,
  IN UINT32  Prescale,
  IN UINT32  Divider
  );

VOID
PacSyncErrors (
  IN CONST CHAR8  *Tag
  );

VOID
PacProbeFrameBuffer (VOID);

VOID
PacBringUpDisplay (VOID);

VOID
PacReset (VOID);

VOID
SecVectorTable ();

VOID
SecCommonExceptionEntry (
  IN UINT32 Type,
  IN UINTN  LR
  );

EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize
  );

EFI_STATUS
EFIAPI
PlatformPeim ();

VOID
EFIAPI
ProcessLibraryConstructorList ();

#endif /* _SEC_H_ */

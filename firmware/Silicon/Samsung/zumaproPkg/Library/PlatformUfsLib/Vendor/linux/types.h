/**
  Shim: the kernel integer types the vendor UFS calibration uses, mapped onto
  EDK2's. Part of PAC-OS, not of the vendor source.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _PAC_SHIM_LINUX_TYPES_H_
#define _PAC_SHIM_LINUX_TYPES_H_

#include <Base.h>

typedef UINT8   u8;
typedef UINT16  u16;
typedef UINT32  u32;
typedef UINT64  u64;
typedef INT32   s32;

#endif

/**
  Shim: the two kernel conveniences the vendor UFS calibration uses.
  Part of PAC-OS, not of the vendor source.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _PAC_SHIM_LINUX_KERNEL_H_
#define _PAC_SHIM_LINUX_KERNEL_H_

#include <Base.h>
#include <Library/DebugLib.h>

#ifndef BIT
#define BIT(n)  (1UL << (n))
#endif

//
// The vendor source does:
//   #define printf(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
// so pr_info has to be a variadic macro, not a function.
//
#define pr_info(fmt, ...)  DEBUG ((EFI_D_INFO, fmt, ##__VA_ARGS__))

#endif

/**
  Shim: register access for the vendor UFS calibration, mapped onto EDK2's
  MMIO helpers. Part of PAC-OS, not of the vendor source.

  The vendor code passes pointers into its own mapped regions, so readl and
  writel take a pointer here rather than an address, matching the kernel
  signatures it was written against. Note writel's argument order is value
  first, which is the kernel's convention and the reverse of MmioWrite32's.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _PAC_SHIM_LINUX_IO_H_
#define _PAC_SHIM_LINUX_IO_H_

#include <Base.h>
#include <Library/ArmLib.h>
#include <Library/IoLib.h>
#include <linux/types.h>

#define readl(a)      MmioRead32 ((UINTN)(a))
#define writel(v, a)  MmioWrite32 ((UINTN)(a), (UINT32)(v))

#define DSB           ArmDataSynchronizationBarrier ()

#endif

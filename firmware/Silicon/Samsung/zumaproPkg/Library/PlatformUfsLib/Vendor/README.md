# Vendor UFS calibration for Tensor G3/G4 (zuma, zumapro)

These four files are **copied unmodified** from Google's published kernel
source, `drivers/ufs/zuma/` in
`https://android.googlesource.com/kernel/google-modules/soc/gs`, branch
`android-gs-caimito-6.1-android16`.

    ufs-cal-if.c
    ufs-cal-if.h
    ufs-cal.h
    ufs-vs-mmio.h

They are **GPL-2.0-only**, Copyright 2022 Samsung Electronics Co., Ltd. Their
SPDX identifiers and copyright notices are intact and must stay that way.
This matters: the equivalent file elsewhere in this tree,
`Silicon/Samsung/S5E9830Pkg/Library/PlatformUfsLib/UfsCal.c`, is plainly
derived from the same source with those notices removed. That is a licence
violation, and copying it would repeat one.

Keeping them unmodified also has an engineering benefit: the PHY and UniPro
calibration values are the part no one can derive by inspection, and local
edits would be a liability every time upstream changes.

## Why the kernel code path, not the vendor's firmware one

`ufs-cal-if.c` carries `__UFS_CAL_LK__` and `__UFS_CAL_FW__` build modes for
use in a bootloader. We deliberately define **neither**, taking the kernel
path, because on inspection the firmware path is the worse of the two here:

- `__get_mclk_period_rnd_off()` uses `float` under `__UFS_CAL_FW__`. EDK2
  firmware has no floating point. The kernel path does the same rounding in
  integer arithmetic.
- `ufs_cal_wait_pll_lock()` becomes `while (1)` with no timeout, so a PHY
  that never locks hangs the firmware instead of returning an error.
- Two of the PHY retry loops return on the first failed attempt under
  `__UFS_CAL_FW__`, which defeats the retry.

Under `__UFS_CAL_FW__` the PMA accessors in `ufs-vs-mmio.h` also gate the
MPHY APB clock around every single access. The kernel instead enables that
clock once around each calibration call, in `ufs_call_cal()`. Those are the
same register: the firmware path's `MISC_CAL 0x11B4` relative to the
standard base is `HCI_FORCE_HCS 0xB4` relative to the vendor base, and its
`MPHY_APBCLK_CAL` is `MPHY_APBCLK_STOP_EN`, both bit 10. `UfsCalGlue.c`
follows the kernel's approach.

## What we supply

`Shim/linux/` provides the handful of things the kernel path expects, mapped
onto EDK2: `readl`, `writel`, `BIT`, `DSB`, `pr_info` and the `u8`/`u32`/`u64`
types. The source states its own requirements:

> UFS CAL just requires some things that don't have an impact on external
> components, such as macros, not functions. The requirement list is below:
> readl, writel, udelay

`UfsCalGlue.c` and `BoardInit.c` are our own work and are
BSD-2-Clause-Patent like the rest of this package.

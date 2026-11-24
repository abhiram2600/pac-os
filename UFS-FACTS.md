# Tensor G4 (zumapro) UFS facts

Everything needed to bring up storage on this board, taken from the device's
own device tree and its running kernel. No value here is guessed or carried
over from another device.

Sources:
- `device-tree.dtb` / `device-tree.dts` in this directory, pulled from
  `/sys/firmware/fdt` on the phone and decompiled locally.
- The running kernel's probe output, `dmesg | grep exynos-ufs`.
- Google's GPL driver, `gs-soc-modules/drivers/ufs/` (cloned from
  android.googlesource.com, branch `android-gs-caimito-6.1-android16`).

## Register regions

The `ufs@13200000` node's six reg ranges, in the order
`exynos_ufs_ioremap()` consumes them:

| Index | Name       | Address    | Length  |
| ----- | ---------- | ---------- | ------- |
| 0     | std (HCI)  | 0x13200000 | 0x200   |
| 1     | reg_hci    | 0x13201100 | 0x2000  |
| 2     | reg_unipro | 0x13280000 | 0x8000  |
| 3     | reg_ufsp   | 0x132A0000 | 0xA014  |
| 4     | reg_phy    | 0x13204000 | 0x4000  |
| 5     | reg_cport  | 0x13208000 | 0x804   |

Plus `sysreg_ufs@13020000`.

## Board init inputs

| What | Value | Note |
| ---- | ----- | ---- |
| PHY isolation release | PMU + 0x3EC0, mask 0x1, val 0x1 | `ufs-phy-iso` |
| IO coherency | sysreg_ufs + 0x710, mask 0x3, val 0x3 | `ufs-iocc`, confirmed in dmesg |
| UniPro clock | 178000000 Hz | dmesg `mclk: 178000000` |
| Lanes | 2 | `ufs,pmd-attr-lane`, and `L(2)` in dmesg |
| Max gear | 4 | `ufs,pmd-attr-gear`, and `PA_MaxRxHSGear(4)` |
| HS series | B | `HS-series(2)` in dmesg |
| Power mode | FAST_MODE | `M(1)` in dmesg |
| Board selector | 1 | `brd-for-cal`, and `board: 1` in dmesg |
| EVT version | 0 | `evt version : 0` in dmesg |
| dma-coherent | yes | property present |

The live link therefore runs at HS gear 4, two lanes, HS-series B.

## The one trap

**The PHY isolation write must go through EL3, not a direct store.**

PMU_ALIVE is write-protected against Non-secure access on this SoC. A direct
store raises an uncontainable external abort (ESR 0xBE000011, EC 0x2F,
DFSC 0x11), and because SErrors are asynchronous it surfaces far away from
the instruction that caused it. We lost most of a session to exactly this
before finding it.

Use the same path the stock kernel uses, which SEC already implements as
`PacPmuSetBit`:

    set bit:   SMC(0x82000504, pmu_base + (offset | 0xC000), 1, bit_index)
    clear bit: SMC(0x82000504, pmu_base + (offset | 0x8000), 1, bit_index)

Note the argument is the bit *index*, not a mask. Reference:
`drivers/soc/google/exynos-pmu-if.c` and `exynos-pd_el3.c`.

Existing UEFI board-init code for other Exynos parts writes PMU registers
with a plain `MmioWrite32`. That will fault here.

## Calibration

Google publishes the calibration for this SoC at
`gs-soc-modules/drivers/ufs/zuma/ufs-cal-if.c`. zuma (Tensor G3) and zumapro
(Tensor G4) share it.

It is **GPL-2.0-only**, Copyright 2022 Samsung Electronics. It carries
`__UFS_CAL_LK__` and `__UFS_CAL_FW__` build modes precisely so it can be
compiled into a bootloader or firmware, and it states its own requirements:

> UFS CAL just requires some things that don't have an impact on external
> components, such as macros, not functions. The requirement list is below:
> readl, writel, udelay

So it can be used nearly unmodified rather than hand-ported.

**Licensing caveat worth a decision.** This firmware tree is
BSD-2-Clause-Patent. The existing `S5E9830Pkg/Library/PlatformUfsLib/UfsCal.c`
in it is plainly derived from this same GPL source, with the SPDX identifier
and Samsung copyright notice removed. That is a licence violation, not a
style choice, and it is not something to copy. Anything we derive from this
source keeps its notice.

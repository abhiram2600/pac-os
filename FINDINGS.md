# PAC-OS — Current State

Last updated: 2026-11-24

Goal: run macOS on the Google Pixel 10a (stallion, Tensor G4 "zumapro") via
`Google bootloader (ABL) -> our UEFI -> UniXNU -> XNU -> macOS`.

## Where we are

| Step | State |
| --- | --- |
| 1. Bootloader loads our UEFI | **done** |
| 2. UEFI brings up the SoC, MMU, timer | **done** |
| 3. UEFI display output | **not done**, was wrongly marked done |
| 4. UEFI storage, reads partitions | **done** |
| 5. UEFI loads an executable from storage | **done** |
| 6. UniXNU menu on screen | next |
| 7. Apple device tree for this board | |
| 8. Drop to EL1, enter XNU | |
| 9. XNU entry, MMU on | |
| 10. Platform expert | |
| 11. Storage inside macOS, APFS root | |
| 12. Userland: launchd, WindowServer, login | |
| 13. GPU and Metal | |
| 14. Touch, battery, thermals | |

The firmware runs to completion with no faults, reads the disk, mounts a
filesystem, and boots a program of ours off it. Nothing is visible on the
panel: the phone shows the bootloader's logo throughout. That program
is a placeholder that prints to the screen and counts down; replacing it with
UniXNU is a matter of dropping a different file into
`uefi-uke/Resources/PacPayload`.

The payload filesystem lives inside the firmware image rather than on the
phone's storage, because none of the 34 partitions can be given up. It is a
360 KB FAT12 volume published as a block device at runtime, so the boot
manager treats it exactly like a disk.

## Hardware, all verified on this device

Everything below came from the phone's own device tree
(`device-tree.dts` here, pulled from `/sys/firmware/fdt`) or from its
running kernel. Nothing is carried over from another device.

- SoC **Tensor G4 (zumapro)**, 8 cores, 2 clusters, Armv9.2.
- RAM is **8 GB in four 2 GB banks, not contiguous**: `0x80000000`,
  `0x880000000`, `0x900000000`, `0x980000000`.
- GICD `0x10400000` len `0x10000`; GICR `0x10440000` len `0x200000`.
- Chip info `0x10000000`; PMU `0x15460000`; UART `0x10870000`.
- Panel **1080 x 2424**, `google-staea`, command mode, on DECON0 window 5.
- Display: DECON0 `0x19470000` (main/win/wincon at `0x19470000`,
  `0x19480000`, `0x194A0000`), planes at `0x19900000` with DMA at
  `0x19930000`.
- Storage: see `UFS-FACTS.md`. Links at HS gear 4, two lanes, HS-series B.
  LUN 0 is 121668 MiB; the GPT has 34 entries.
- Watchdogs: cl0 `0x10060000`, cl1 `0x10070000`.
- ramoops `0xfd3ff000` len `0x400000`: 2 MB console zone then 2 MB pmsg.

## The traps on this SoC

Things that cost real time and will cost it again if forgotten.

**PMU writes must go through EL3.** A direct store to PMU_ALIVE raises an
uncontainable SError (ESR `0xBE000011`). Because SErrors are asynchronous it
surfaces far from the cause; ours appeared inside ArmCpuDxe. Use the atomic
bit-set alias the stock kernel uses:

    SMC(0x82000504, pmu_base + (offset | 0xC000), 1, bit_index)

The argument is the bit index, not a mask. Reads are fine.

**Boot image format is strict.** The kernel payload must be an LZ4 *legacy
frame* (magic `0x184C2102`) with **zero** trailing bytes, and the arm64
header's `text_offset` must be 0 with flags `0xA`. Putting an absolute
address in `text_offset` points at the hole between RAM banks and the image
is never placed. No appended DTB.

**Every reset must be warm.** A clean reset makes the bootloader
re-initialise DRAM, which erases the debug log. zumaproPkg supplies a
ResetSystemLib that resets through the watchdog instead of PSCI.

**Nothing resets this SoC once our firmware runs.** The old ~30 s
"boot failure" was the bootloader's own timeout. SEC arms cluster 0's
watchdog as a hang deadline; disabling it turns any lock-up into a phone
that needs its battery pulled.

**Zeroed buffers read as plausible values.** A UFS attribute read "agreeing"
with us meant nothing until the data path was known good. Verify the
transport before trusting any value it carries.

## Debugging

**Output.** No usable serial port and no splash framebuffer, so
`RamoopsSerialPortLib` routes all firmware output into the ramoops **pmsg**
zone, and the stock kernel surfaces it at
`/sys/fs/pstore/pmsg-ramoops-0`. Not the console zone: the bootloader writes
its own log there and runs after us.

**The bootloader's log is readable** at
`/sys/fs/pstore/console-ramoops-0`, and it is informative. It states the
panel geometry, the kernel load address and size, and its AVB decisions.

**Verbosity.** `stallion.dsc` sets `PcdDebugPrintErrorLevel` to
`0xFFFFFFFF`. The project default leaves DEBUG_INFO off, which hid the bug
that cost the longest.

**The loop.** `python3 build_uefi.py -d stallion -r DEBUG` in the cached
`pac-uefi-build` Docker image takes about 11 s. The capture script flashes,
lets it fail, restores Android and pulls the log, about 4 minutes.

**Always wrap fastboot in `timeout`** and never run two at once. A killed
in-flight command wedges the bootloader's USB until it is power-cycled.

**Check the memory map** with
`python3 Resources/PacDebug/validate_memory_map.py`, which reproduces the
overlap check MemoryPeim dead-loops on.

## Upstream bugs found

Worth reporting; all affect devices other than this one.

- `SimpleFbDxe` used an uninitialised local and ignored the lookup status, so
  on any device with no "Display Reserved" region it published stack garbage
  as a framebuffer base. Here the ensuing screen clear flattened ten running
  drivers.
- `GpioDxe`'s `DAT_SET(x)` was `(0 << (x))`, always zero, so a pin could
  never be driven high. Its enable test also read the pointer, not the value.
- `UfsDxe` hardcoded `bRefClkFreq` and the UTRD granularity, and dereferenced
  `DevPwrAddr` unconditionally. Now PCDs, defaulting to the old behaviour.
- `S5E9830Pkg/Library/PlatformUfsLib/UfsCal.c` is Google's GPL calibration
  with the SPDX line and copyright notice removed. A licence violation, not a
  style choice. Ours keeps the notice; see
  `uefi-uke/Silicon/Samsung/zumaproPkg/Library/PlatformUfsLib/Vendor/README.md`.

## Display, what is actually true

Step 3 was marked done on the strength of a test that checked the wrong
thing. It sampled DECON's frame counter, saw it advancing, and concluded our
pixels were reaching the panel. The counter advances because the controller
is still refreshing the panel with the frame it already had, which it was
doing before we touched anything. Nothing we drew was ever displayed. The
honest test is whether a plane is fetching from a buffer we wrote.

Verified on this phone:

| Thing | Value |
| --- | --- |
| DECON0 | `0x19470000`, enabled, command mode, hardware trigger |
| Active window | 5, 1080 x 2424, `win_end_pos` 0x9770437 |
| DPP0 read DMA | `0x19900000`, channel 0 |
| DPP0 block | `0x19930000` |
| Framebuffer the DMA points at | `0xFAC00000` |
| TRIG_CON | `0x00003070` |
| DATA_PATH_CON_0 | `0x000000B1` |

Two things had been wrong. The framebuffer address was a carve-out of free
RAM we chose ourselves, on the reasoning that the device tree reserves no
framebuffer so there cannot be one. There is one, it is simply not reserved,
because the kernel reclaims the memory later. And the DMA and DPP register
bases were swapped, so every plane register went to the wrong block. The
device tree settles the order: `drmdpp@19900000` names its ranges "dma" then
"dpp".

Published work on the sibling Tensor phones reports the same framebuffer
address and the same TRIG_CON value, which is a useful cross-check that this
family behaves alike.

What remains: the panel is in command mode and self-refreshing. It holds the
last frame it was sent, and DECON's frame counter barely moves, so writing
pixels into the framebuffer changes nothing on screen. A frame has to be
pushed. Latching the shadow registers and unmasking the hardware trigger is
the likely sequence, but the exact register writes are not yet confirmed.

## Loading anything off storage

The FAT driver will not start without a Unicode collation driver. It wants
one to compare filenames without regard to case, and it refuses before it
ever reads the media, so the symptom is that no volume anywhere mounts and
the boot manager reports nothing bootable. Easy to misread as a storage or
filesystem problem. Both drivers must be in the firmware volume, not merely
built.

The boot manager only invents a boot option for the default loader path on
media marked removable. A fixed disk is expected to have registered its own
boot variable, so a mounted volume with a loader on it still goes untried.

A driver that publishes a device from its entry point should connect again at
end of DXE. The first attempt can run before every driver has registered its
binding, and a driver that was not asked about a handle is never asked again.

## Recovery

The phone is not bricked. Flash the stock boot image and reboot:

    fastboot flash boot stallion-factory/stallion-cp1a.260505.005/extracted/boot.img
    fastboot reboot

Only the `boot` partition is ever written. Android, apps and data are
untouched.

## Layout

| Path | What |
| --- | --- |
| `uefi-uke/` | The UEFI firmware. Our work is `Silicon/Samsung/zumaproPkg` and `Platforms/Google/stallionPkg`. |
| `zumapro-kernel/` | Tensor kernel source, for register definitions. |
| `gs-soc-modules/` | Google's SoC modules, GPL, source of the UFS calibration. |
| `stallion-factory/` | Factory images, for recovery. |
| `UniXNU/`, `macOS-uke/`, `macOS-ForSheng/` | The loader and the Snapdragon macOS port, for steps 5 onward. |
| `pac-os/device-tree.dts` | The phone's own device tree, decompiled. Answer hardware questions here first. |

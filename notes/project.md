# PAC-OS

Run macOS on the Google Pixel 10a (Tensor G4). Not a custom Linux, not a skin — boot Apple's own kernel (XNU) on Tensor silicon.

## The Goal

Boot macOS on the Pixel 10a's Tensor G4 SoC, using the same approach as the macOS-on-Snapdragon projects: a UEFI boot firmware brings up the SoC, then hands off to XNU.

## Hardware

- **Device:** Google Pixel 10a (`stallion`)
- **SoC:** Google Tensor G4 (`zumapro`) — arm64, Exynos 2400-based (S5E9945)
- **GIC:** 0x10400000 / 0x10440000
- **Timer:** `samsung,exynos4210-mct` @ 0x10050000
- **RAM:** 8 GB @ `memory@80000000`
- **Bootloader:** unlocked; phone rooted (Magisk)

## The Approach

The boot flow we're building:

```
Google bootloader (ABL)
   ↓  loads boot.img
UEFI firmware (our port)         ← brings up the SoC (CPU, GIC, timer, memory)
   ↓
UniXNU (OpenCore-like UEFI env)  ← loads drivers, boots the OS
   ↓
XNU (Apple's macOS kernel)       ← reads the device tree (Apple ADT format)
   ↓
macOS (system image on APFS)
```

Below the bootloader is locked silicon — that's a given. Everything from the UEFI up is ours to write.

## Milestones

1. **UEFI firmware for Tensor G4** — port uefi-uke to zumapro.
   - Status: BUILT ✅ — `SILICIUM_UEFI.fd` compiles; `Mu-stallion.img` (boot image) produced and flashed. The bootloader loads it, then it crashes early. Debugging now.
2. **UEFI boots on the phone** — get past the early crash (bring up CPU/GIC/timer/memory cleanly).
   - Status: IN PROGRESS.
3. **Device tree → Apple ADT** — convert the zumapro device tree for XNU.
4. **UniXNU adaptation** — boot XNU on zumapro.
5. **Storage (UFS)** — mount the macOS system volume.
6. **GPU** — PanVK (Mali) + metal2vulkan for a usable desktop.

## Current State

- [x] Bootloader unlocked, phone rooted
- [x] zumaproPkg (SoC package) written — GPIO banks, watchdog, config
- [x] stallionPkg (device platform) written — memory map, build config
- [x] UEFI firmware compiled (SILICIUM_UEFI.fd)
- [x] Boot image built (Mu-stallion.img, LZ4, header v4)
- [x] Flashed — bootloader accepted and loaded it, then it crashed early
- [ ] Debug the early crash (memory map / config address)
- [ ] Boot XNU
- [ ] macOS

## Key Concept

The Tensor G4 is Exynos-based, so the uefi-uke framework's Samsung/Exynos path (`S5E9945Pkg` = Exynos 2400) is the closest reference — far closer than the Snapdragon path. The port is thin: a device tree + a small SoC package.

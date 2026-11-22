# PAC-OS — Current State & Working Findings

Last updated: 2026-11-22

This doc is the "where are we right now" snapshot so a fresh session can pick up without reading everything. Read `project.md` for the goal and `PORTING-PLAN.md` for the full roadmap.

---

## TL;DR — Where We Are

- **Goal: run macOS on the Pixel 10a** (not build a custom Linux/OS). Boot Apple's XNU kernel on Tensor G4.
- **Milestone reached: a custom UEFI boot firmware for the Tensor G4 was BUILT and FLASHED.** The bootloader accepted and loaded it, then it crashed early (`boot failure`).
- **Next: debug where the UEFI dies** — most likely a wrong address in the memory map or SoC config.

## Hardware (Pixel 10a)

- Codename: **stallion**
- SoC: **Google Tensor G4 (`zumapro`)** — arm64, Exynos 2400-based (S5E9945)
- GIC: 0x10400000 / 0x10440000
- Timer: `samsung,exynos4210-mct` @ 0x10050000
- RAM: 8 GB @ `memory@80000000`
- Watchdog: `google,zuma-cl0-wdt` @ 0x10060000, cl1 @ 0x10070000
- Display: `samsung,exynos-decon` (drmdecon0 @ 0x19470000)
- Bootloader: unlocked; rooted with Magisk (`stallion_magisk_init_boot.img`)

## The Boot Flow We're Building

```
Google bootloader (ABL) → UEFI (our port) → UniXNU → XNU → macOS
```

Our UEFI goes in `boot.img` (LZ4-compressed, header v4). It brings up the SoC, then hands off to UniXNU → XNU.

## Current State (2026-11-22)

- ✅ Cloned the uefi-uke build env (all submodules)
- ✅ Wrote `zumaproPkg` (SoC package) — GPIO banks (56 banks, 13 controllers), watchdog, GIC/CPU config
- ✅ Wrote `stallionPkg` (device platform) — memory map, dsc/fdf, build config
- ✅ UEFI compiled: `Build/stallionPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd`
- ✅ Boot image built: `Mu-stallion.img` (820 KB, LZ4, header v4)
- ✅ Flashed to `boot` — bootloader accepted and loaded it
- ❌ It crashed early (`boot failure`) — expected first flash

## Key Facts Learned

1. **GKI is a dead end for custom kernels** — the Pixel's Linux kernel is generic (Google-shipped), and the bootloader rejects hand-built Linux kernels (LZ4 format + Google header + config must all match). So we don't build a Linux kernel; we replace it entirely with a UEFI shim → XNU.
2. **The device tree is kernel-agnostic and reusable** — it's the hardware map (addresses, interrupts) and it's what we feed to XNU (converted to Apple ADT). This is the layer we actually control.
3. **Tensor G4 is Exynos-based** — the uefi-uke `S5E9945Pkg` (Exynos 2400) is the closest reference. SoC bring-up is device-tree-driven in the Silicium core; per-SoC we only need a thin package (watchdog + framebuffer + GPIO).
4. **Boot image format:** the kernel in `boot.img` must be LZ4-compressed (legacy block), header v4. `Mu-stallion.img` already does this (`Resources/Configs/stallion.toml` has `kernel_compression = "lz4"`).
5. **Partitions (Pixel 10a):** UEFI → `boot`, device tree → `vendor_kernel_boot`, ramdisk → `vendor_boot`, init → `init_boot`.

## Prime Debug Suspect

The UEFI crashes before any output. The top suspect is **`stallionPkg/Library/ConfigurationMapLib/ConfigurationMapLib.c`** — it was copied from a Snapdragon template and has SoC-specific values that may be wrong for zumapro. Also re-audit `MemoryMapLib.c` against the actual `stallion.dtb`.

## Recovery

Flash the stock `boot.img` from the factory image (`stallion-cp1a.260505.005-factory-bbf7096a.zip`) to return to stock Android. The phone is rooted and NOT bricked.

## Notes for Next Session

- macOS host (Apple Silicon). UEFI builds happen inside Docker (Linux) or via the build env.
- `adb`/`fastboot` installed.
- Key dirs: `~/Documents/pac/uefi-uke/` (UEFI), `~/Documents/pac/zumapro-kernel/` (Tensor kernel source), `~/Documents/pac/stallion-factory/` (factory images).
- Full roadmap + hardware map + GPIO tables: `PORTING-PLAN.md`.

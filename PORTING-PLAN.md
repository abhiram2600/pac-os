# Tensor G4 (Pixel 10a) Porting Plan

Goal: port the macOS-on-Snapdragon stack to the Google Tensor G4 (zumapro) in the Pixel 10a.

> Note: originally targeted the Pixel 7 (Tensor G2 / gs201, Exynos 2200-based). Switched to Pixel 10a (Tensor G4 / zumapro, Exynos 2400-based) — same Exynos family, but an even closer match to the UEFI framework's `S5E9945Pkg` (Exynos 2400).

## The Reference Boot Flow (Snapdragon, from macOS-uke)

```
Google/Xiaomi bootloader (ABL)
   ↓  loads boot_a (boot.img)
UEFI firmware (uefi-uke)           ← brings up the SoC, loads device tree
   ↓
UniXNU (OpenCore-like UEFI env)    ← loads drivers, boots the OS
   ↓
XNU (macOS kernel)                 ← reads the device tree (Apple ADT format)
   ↓
macOS (system image on APFS)
```

## Components and Their Tensor G2 Status

| Component | Snapdragon | Tensor G2 | Notes |
|---|---|---|---|
| UEFI firmware | uefi-uke (SM7675) | NEEDS BUILD (uefi-panther) | Brings up SoC: CPU, GIC, timer, UART, memory |
| UniXNU UEFI env | UniXNU (+ sheng/uke) | NEEDS ADAPT | SoC-agnostic core; needs gs201 support |
| Device tree | uke.dtb → Apple ADT | HAVE gs201 dtb | Convert stock dtb to Apple ADT format |
| XNU kernel | EL1 kernel (ForSheng) | NEEDS PORT | XNU is Apple's; the EL1 bring-up is SoC-specific |
| GPU driver | Adreno740 + Turnip + metal2vulkan | NEEDS BUILD | Mali-G710 needs PanVK + metal2vulkan |
| Storage | NativeUfs (UFS) | NEEDS ADAPT | Tensor G2 UFS controller (Exynos-style) |

## Porting Order (each is a milestone)

1. **UEFI firmware for Tensor G2 (uefi-panther)** — the foundation. Bring up the SoC: CPU cores (PSCI), GIC, arch timer, UART, memory map. The uefi-uke sources show exactly what this involves; swap SM7675 specifics for gs201.

2. **Device tree conversion** — take the stock gs201 dtb (we already have it in `tensor-g2-kernel/` and the extracted stock dtb) and convert to Apple's ADT format (like `make-uke-adt.py` does for uke).

3. **UniXNU adaptation** — add gs201 as a platform. UniXNU's core is SoC-agnostic; the SoC-specific parts (device tree, boot args, memory) need gs201 values.

4. **XNU boot** — get the EL1 XNU kernel to boot on gs201 (CPU + GIC + timer). This is the "kernel boots" milestone.

5. **Storage** — adapt the UFS driver for Tensor G2's controller so macOS can find its system volume.

6. **GPU (the mountain)** — PanVK (Mali Vulkan) + metal2vulkan translator + a Metal plugin, replacing the Adreno/Turnip path. Needed for a usable desktop.

## Pixel 7 Partition Mapping (open question)

Snapdragon (uke): UEFI in `boot_a`, kernel+dtb in `vendor_boot_a`.

Pixel 7 (gs201): has `boot`, `vendor_kernel_boot` (dtb), `vendor_boot` (ramdisk), `init_boot`.

Likely mapping: UEFI → `boot`, device tree → `vendor_kernel_boot`. Exact placement of the XNU kernel TBD (needs the UEFI to load it from the right partition).

## UEFI Firmware Structure (cloned `uefi-uke`)

The UEFI firmware (`uefi-uke`) is EDK2-based with this layout:

- **`Silicon/`** — SoC support: Qualcomm (Snapdragon), **Samsung (Exynos)**, MediaTek, Nvidia, Silicium.
  - Samsung has `S5E*Pkg` per-Exynos SoC (S5E9810, S5E9820, S5E9830, S5E9945, ...) + `SamsungPkg` (common).
- **`Platforms/`** — device platforms: Xiaomi (many `*Pkg`), Samsung (many `*Pkg`), etc.
- **`Silicium-ACPI/`** — the ACPI layer (SoC-agnostic).
- `ukePkg` (the macOS-uke reference device) is added via macOS-uke patches (`patches/uefi/new-files/`), not in the base repo.

**KEY: the Tensor G2 (gs201) is Exynos 2200-based (S5E9925). So `Silicon/Samsung/` is the closest SoC reference — much closer than the Snapdragon `Silicon/Qualcomm/` path that macOS-uke uses.**

## Tensor G2 UEFI Port (concrete)

Two new pieces, following the Samsung/Exynos pattern:

1. **`gs201Pkg` (SoC support)** — Tensor G2 bring-up: CPU cores (Cortex-X1/A78/A55, PSCI), GIC-v3 (0x10400000), arch timer, UART, memory map. Model it on the closest `S5E*Pkg` (Exynos 2200).
2. **`pantherPkg` (device platform)** — Pixel 7 board: the gs201 device tree, UFS storage, display, etc. Model it on `ukePkg` (the macOS-uke reference).

Then pack: UEFI FD + gs201 dtb → `boot.img` (like `make-boot-uefi.sh` does).

## KEY FINDING — the port is smaller than feared

Read `Silicon/Samsung/S5E9945Pkg` (Exynos). The per-SoC package is **tiny**:

- `PlatformSecLib/PlatformSec.c` — just `DisableWatchdogTimer()` + `EnableFrameBufferWrites()`. Both locate memory regions **by name from the device tree** (`LocateMemoryRegionByName("Watchdog Timer")`, `"DRM Decon"`).
- `PlatformRegisters.h` — 2 defines (watchdog enable bits, display trigger).
- `GpioBankLib` — GPIO bank layout.

The **actual SoC bring-up (CPU PSCI, GIC, timer, memory) is device-tree-driven** in the Silicium core (`Silicon/Silicium/SiliciumPkg`), not per-SoC.

**Conclusion:** for Tensor G2 we only need (a) the gs201 device tree (we HAVE it) and (b) a thin `gs201Pkg` modeled on `S5E9945Pkg` — watchdog + framebuffer + GPIO, with gs201 region names/addresses. The common `SamsungPkg` (UFS, PMIC, GPIO, etc.) is already Exynos-generic.

## Tensor G4 (zumapro) hardware map — extracted from stallion.dtb

Decompiled the Pixel 10a device tree (`stallion.dtb`, 377 KB, from `vendor_kernel_boot.img`). Confirmed **Exynos-based** (uses `samsung,exynos-*` drivers everywhere):

- **SoC**: `google,zumapro`
- **GIC**: `arm,gic-v3` (standard)
- **Timer**: `arm,armv8-timer` + `samsung,exynos4210-mct` @ 0x10050000
- **RAM**: `memory@80000000`
- **Watchdog**: `google,zuma-cl0-wdt` @ 0x10060000, `cl1` @ 0x10070000
- **Display**: `samsung,exynos-decon` (drmdecon0 @ 0x19470000, decon1 @ 0x19471000, decon2 @ 0x19472000)
- **Reserved memory**: gsa @ 0x90200000, gxp_scratchpad @ 0x91300000, ...

This matches the Samsung Exynos reference almost exactly — the port is confirmed tractable.

## What a device package actually contains (studied b5qPkg = S22)

Each device platform (`Platforms/<vendor>/<device>Pkg/`) has ~12 files:

- `DeviceBuild.py` — build config
- `<device>.dsc` — EDK2 platform description
- `<device>.fdf` — firmware volume layout
- `<device>.dec` — package declarations
- `Include/RAW.inc`, `DXE.inc`, `APRIORI.inc` — firmware volume includes
- `Library/MemoryMapLib/MemoryMapLib.c` — **a static table of every hardware region** (name, address, length, attributes), e.g. `{"HYP", 0x80000000, 0xA00000, ...}`
- `Library/ConfigurationMapLib/` — config map

And each SoC package (`Silicon/<vendor>/<SoC>Pkg/`) has:
- `Library/PlatformSecLib/` — watchdog disable + framebuffer enable (locates regions by name)
- `Library/GpioBankLib/` — GPIO banks

**The real port work:** translate `stallion.dtb` (which we have) into the `MemoryMapLib` table + the SoC `PlatformSecLib` addresses. The `stallion.dtb` has every address; we convert it to the table format.

## Build requirements

- Full source with submodules (~5GB): `Mu_Basecore` (EDK2), `Common/Mu`, `Silicium-ACPI`, `Binaries`, openssl.
- Linux (use Docker).
- Build: `python3 build_uefi.py -d <device>`.

## PROGRESS (2026-11-22)

- ✅ Cloned the full UEFI build env (all submodules).
- ✅ Created `Silicon/Samsung/zumaproPkg/` (copied from S5E9945Pkg template).
- ✅ Wrote `zumaproPkg.dsc.inc` with correct Tensor G4 values: GIC 0x10400000/0x10440000, 8 cores/2 clusters, 13 pinctrl addresses (0x10840000, 0x10C40000, 0x12040000, 0x13040000, 0x13060000, 0x15060000, 0x154D0000, 0x154E0000, 0x16140000, 0x16280000-0x162B0000), SMBIOS "Tensor G4"/"zumapro".

## KEY FINDING — GPIO bank offsets need the kernel source

The dtb gives GPIO bank **names** (`gpa0`, `gpa1`, ...) and controller addresses, but NOT the bank **register offsets** (the `0x20`, `0x40`, `0x60` values in `GpioBank.c`). Those offsets live in Google's zumapro pinctrl *driver* source (the kernel), not the device tree.

So `GpioBank.c` needs: either Google's kernel source (read the pinctrl driver), or use S5E9945 offsets as a first guess (risky). Same for the watchdog/decon register bits in `PlatformRegisters.h`.

## GPIO bank data — EXTRACTED (zuma = zumapro)

Cloned `zumapro-kernel` (Google Tensor kernel). The pinctrl banks are in `google-modules/soc/gs/drivers/pinctrl/gs/pinctrl-gs.c`.

**Key: zumapro (Tensor G4) shares the zuma (Tensor G3) pinctrl banks.** 13 controllers, matching the dtb:

| Ctrl | Name | Addr | Banks |
|---|---|---|---|
| 0 | alive | 0x154D0000 | gpa0-gpa10 |
| 1 | custom | 0x15060000 | gpn0-gpn9 |
| 2 | far | 0x154E0000 | gpa5 |
| 3 | gsacore0 | 0x16280000 | gps0 |
| 4 | gsacore1 | 0x16290000 | gps1 |
| 5 | gsacore2 | 0x162A0000 | gps2 |
| 6 | gsacore3 | 0x162B0000 | gps3 |
| 7 | gsactrl | 0x16140000 | gps4 |
| 8 | hsi1 | 0x12040000 | gph0-gph2 |
| 9 | hsi2 | 0x13040000 | gph3-gph4 |
| 10 | hsi2ufs | 0x13060000 | gph5 |
| 11 | peric0 | 0x10840000 | gpp0-gpp17 |
| 12 | peric1 | 0x10C40000 | gpp19-gpp24 |

Bank offsets are 0x20 apart (0x0, 0x20, 0x40, ...), and bank name prefix maps to BANK_ID (gpa=A, gpn=N, gps=S, gph=H, gpp=P). All data present to write `GpioBank.c` with exact values.

## zumaproPkg (SoC package) — DONE ✅

- ✅ `GpioBank.c` — 56 banks, 13 controllers (from zuma pinctrl data)
- ✅ `PlatformRegisters.h` — watchdog bits `(1<<5)|1`, decon trigger `0x30` (confirmed from s3c2410_wdt.c)
- ✅ `PlatformSec.c`, `PlatformSecLib.inf`, `PlatformHelper.S`, `GpioBankLib.inf` — copied (generic, correct)
- ✅ `zumaproPkg.dsc.inc` — GIC/CPU/GPIO/SMBIOS/LibraryClasses correct

## stallionPkg (device platform) — SCAFFOLDED + CONFIGURED ✅

- ✅ Renamed `b5q.*` → `stallion.*` (dsc/fdf/dec + all includes)
- ✅ `DeviceBuild.py` — added `Platforms/Google` to PackagesPath
- ✅ `stallion.dsc` — includes `zumaproPkg/zumaproPkg.dsc.inc`, SMBIOS "Google"/"Pixel 10a", framebuffer 1080x2400
- ✅ `MemoryMapLib.c` — 8GB RAM + 13 reserved regions (gsa, gxp, tpu, aoc, etc.) + UEFI regions + register regions (Watchdog Timer, GIC, UART, DRM Decon)
- ⚠️ `ConfigurationMapLib.c` — copied from the Snapdragon template; some SoC-specific values need review (non-critical for first boot)

## Next Step

Build the UEFI (`python3 build_uefi.py -d stallion`) and fix any build errors. Then pack into boot.img and flash.

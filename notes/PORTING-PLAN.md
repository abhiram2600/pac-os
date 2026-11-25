# Tensor G4 (Pixel 10a) Porting Plan

Goal: run macOS on the Google Tensor G4 (zumapro) in the Pixel 10a, by porting the macOS-on-Snapdragon stack (UEFI → UniXNU → XNU → macOS).

> Historical note: the project originally targeted the Pixel 7 (Tensor G2 / gs201). Switched to the Pixel 10a (Tensor G4 / zumapro, Exynos 2400-based) — a closer match to the UEFI framework's `S5E9945Pkg` (Exynos 2400).

## The Reference Boot Flow

```
Google bootloader (ABL)
   ↓  loads boot_a (boot.img)
UEFI firmware (uefi-uke)           ← brings up the SoC, loads device tree
   ↓
UniXNU (OpenCore-like UEFI env)    ← loads drivers, boots the OS
   ↓
XNU (macOS kernel)                 ← reads the device tree (Apple ADT format)
   ↓
macOS (system image on APFS)
```

## Remaining Work (in order)

1. **Debug the UEFI early crash** — the UEFI is built and flashed but crashes before output. (IN PROGRESS)
2. **Device tree → Apple ADT** — convert the zumapro device tree for XNU.
3. **UniXNU adaptation** — boot XNU on zumapro.
4. **Storage (UFS)** — adapt the UFS driver so macOS can find its system volume.
5. **GPU (the mountain)** — PanVK (Mali) + metal2vulkan translator, replacing the Adreno/Turnip path.

## UEFI Firmware Structure (cloned `uefi-uke`)

The UEFI firmware (`uefi-uke`) is EDK2-based:

- **`Silicon/`** — SoC support: Qualcomm, Samsung (Exynos), MediaTek, Nvidia, Silicium. Samsung has `S5E*Pkg` per-Exynos SoC + `SamsungPkg` (common).
- **`Platforms/`** — device platforms (Xiaomi, Samsung, Google, ...).
- **`Silicium-ACPI/`** — the ACPI layer (SoC-agnostic).
- `ukePkg` (the macOS-uke reference device) is added via patches, not the base repo.

**KEY: Tensor G4 (zumapro) is Exynos 2400-based (S5E9945). `Silicon/Samsung/S5E9945Pkg` is the closest reference. The SoC bring-up is device-tree-driven in the Silicium core; per-SoC we only need a thin package (watchdog + framebuffer + GPIO).**

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
- `Library/MemoryMapLib/MemoryMapLib.c` — a static table of every hardware region (name, address, length, attributes)
- `Library/ConfigurationMapLib/` — config map

Each SoC package (`Silicon/<vendor>/<SoC>Pkg/`) has:
- `Library/PlatformSecLib/` — watchdog disable + framebuffer enable
- `Library/GpioBankLib/` — GPIO banks

**The real port work:** translate `stallion.dtb` (which we have) into the `MemoryMapLib` table + the SoC `PlatformSecLib` addresses.

## Build requirements

- Full source with submodules (~5GB): `Mu_Basecore` (EDK2), `Common/Mu`, `Silicium-ACPI`, `Binaries`, openssl.
- Linux (use Docker).
- Build: `python3 build_uefi.py -d <device>`.

## PROGRESS (2026-11-22)

- ✅ Cloned the full UEFI build env (all submodules).
- ✅ Created `Silicon/Samsung/zumaproPkg/` (copied from S5E9945Pkg template).
- ✅ Wrote `zumaproPkg.dsc.inc` with correct Tensor G4 values: GIC 0x10400000/0x10440000, 8 cores/2 clusters, 13 pinctrl addresses, SMBIOS "Tensor G4"/"zumapro".

## KEY FINDING — GPIO bank offsets need the kernel source

The dtb gives GPIO bank names and controller addresses, but NOT the bank register offsets. Those live in Google's zumapro pinctrl driver source (the kernel), not the device tree. So `GpioBank.c` needs the kernel source (or S5E9945 offsets as a guess).

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

Bank offsets are 0x20 apart, and bank name prefix maps to BANK_ID (gpa=A, gpn=N, gps=S, gph=H, gpp=P).

## zumaproPkg (SoC package) — DONE ✅

- ✅ `GpioBank.c` — 56 banks, 13 controllers (from zuma pinctrl data)
- ✅ `PlatformRegisters.h` — watchdog bits `(1<<5)|1`, decon trigger `0x30` (from s3c2410_wdt.c)
- ✅ `PlatformSec.c`, `PlatformSecLib.inf`, `PlatformHelper.S`, `GpioBankLib.inf` — copied (generic, correct)
- ✅ `zumaproPkg.dsc.inc` — GIC/CPU/GPIO/SMBIOS/LibraryClasses correct

## stallionPkg (device platform) — SCAFFOLDED + CONFIGURED ✅

- ✅ Renamed `b5q.*` → `stallion.*` (dsc/fdf/dec + all includes)
- ✅ `DeviceBuild.py` — added `Platforms/Google` to PackagesPath
- ✅ `stallion.dsc` — includes `zumaproPkg/zumaproPkg.dsc.inc`, SMBIOS "Google"/"Pixel 10a", framebuffer 1080x2400
- ✅ `MemoryMapLib.c` — 8GB RAM + 13 reserved regions + UEFI regions + register regions (Watchdog Timer, GIC, UART, DRM Decon)
- ⚠️ `ConfigurationMapLib.c` — copied from the Snapdragon template; some SoC-specific values need review (prime debug suspect)

## BUILD SUCCEEDED ✅ (2026-11-22)

The UEFI firmware for the Tensor G4 **compiled successfully** (`Return Code: 0x00000000`).

Output:
- `Build/stallionPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd` — the UEFI firmware
- `Mu-stallion.img` (820 KB) — the boot image (UEFI + boot shim + dtb)

Fixes applied during the build:
- Re-scaffolded `stallionPkg` from the **Exynos** `e1sPkg` template (not the Snapdragon `b5q` — wrong template, referenced Qualcomm drivers)
- Fixed an unused variable in `PlatformSec.c`
- Added `Resources/DTBs/stallion.dtb` (the device tree)

## Next Step (updated 2026-11-22)

1. ✅ Compression is already LZ4 — `Resources/Configs/stallion.toml` has `kernel_compression = "lz4"`.
2. ✅ Flashed `Mu-stallion.img` to `boot` — bootloader accepted and loaded it.
3. ❌ It crashed early (`boot failure`) — expected first flash. Now debug where it dies.
4. Prime suspect: `ConfigurationMapLib.c` and/or a wrong address in `MemoryMapLib.c`.

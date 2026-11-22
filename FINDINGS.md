# PAC-OS — Working Findings & Session Notes

Last updated: 2026-11-08

This doc exists so a fresh session (no chat history) can pick up where we left off. Read `project.md` first for the goal, then this for the current state and what we've learned.

---

## TL;DR — Where We Are

- Goal: replace Android with a custom OS on a **Pixel 7**, starting from the kernel.
- **Bootloader is UNLOCKED** on the Pixel 7 (`fastboot flashing unlock`). No account, no wait.
- A **custom kernel was compiled and flashed** — the bootloader runs it, but it fails mid-boot (expected: it's wired for the Pixel 6, not the Pixel 7).
- Next: adapt the kernel's device tree from Pixel 6 to Pixel 7.

## Hardware Facts (Pixel 7)

- Codename: **panther**
- SoC: **Google Tensor G2 (GS201)** — arm64
- CPU: Cortex-X1 / A78 / A55
- GPU: **Mali-G710**
- Android 13, build `TQ2A.230405.003.E1`, security patch 2023-04-05 (before unlock)
- Bootloader now: **unlocked**

## Key Facts Learned (the mental model)

1. **Below the kernel is locked silicon.** Boot ROM + signed stages are physically/cryptographically locked by the chip maker. The kernel is the door; everything above is replaceable.
2. **arm64 is the same everywhere.** Pixel (Tensor G2), Snapdragon, Apple silicon — all arm64. The CPU runs code natively; the hard part is boot + drivers + device tree, never the instruction set.
3. **Root is irrelevant** — root is admin inside Android. We're deleting Android.

## Kernel Build Lessons

- **You cannot build a Linux kernel on macOS.** The kernel's host tools use Linux-only APIs (`elf.h`, `copy_file_range`, `O_LARGEFILE`). This fails regardless of kernel source.
- **Solution: Docker.** Build inside a Linux container. Docker Desktop is installed and working.
- The Mac is Apple Silicon, so an arm64 Ubuntu container runs natively — the kernel build is native arm64→arm64, no cross-compiler needed.
- **macOS's filesystem is case-insensitive**, which corrupts the Linux kernel source (it has case-sensitive file names). Clone the source directly into a case-sensitive Docker volume, not on the Mac disk.
- Build: clone mainline Linux into a Docker volume, then build `Image` + `dtbs` inside an Ubuntu container.

## Mainline Linux Support Status (as of Linux 7.4-rc1, 2026)

- **Pixel 6 (gs101)** IS in mainline: `arch/arm64/boot/dts/exynos/google/gs101.dtsi` (+ `gs101-oriole.dts` = Pixel 6, `gs101-raven.dts` = Pixel 6 Pro).
- **Pixel 7 (gs201 / Tensor G2)** is NOT in mainline. Google merged gs101 but not gs201.
- Tensor G1-G4 are enabled via `ARCH_EXYNOS` (they're Exynos offshoots). Tensor G5 is `ARCH_GOOGLE`.
- **Key finding:** gs101 and gs201 are nearly identical at the core — same GIC address (`0x10400000`), same ARM arch timer. So adapting gs101 → gs201 is tractable.
- The gs201 device tree from Google's kernel uses different driver bindings, so it's a *translation* task into mainline's Exynos format, not a copy.

## MILESTONE 1 RESULT — Custom Kernel Ran (partially)

Date: 2026-11-08. We compiled a mainline Linux kernel (gs101/Pixel 6 base) and flashed it to the Pixel 7.

- **Built** the kernel inside Docker (Linux container).
- **Packed** the kernel into a boot image (header v4) using `mkbootimg`.
- **Flashed** via `fastboot flash boot` and rebooted.
- **Result:** `enter-reason: boot failure` — the bootloader ACCEPTED our kernel and tried to run it, but it failed to boot. Expected: it's a Pixel 6 kernel on Pixel 7 hardware.

**Proof established:** the bootloader loads and runs our custom-compiled kernel. The pipeline works end to end.

## Next: Adapt gs101 → gs201 (the gate)

The kernel runs but stops early because it's wired for Pixel 6. To boot further, translate the Pixel 7 (gs201) differences into mainline's format.

Differences to fix (so far): UART address (gs101 = 0x10900000, gs201 = 0x18040000), CPU type (`cortex-a76` → `cortex-a78`), clock controller IDs, pinctrl. GIC (0x10400000) and ARM arch timer are identical.

## gs201 device tree — first pass

Created `gs201.dtsi` (copy of gs101.dtsi, root compatible → "google,gs201") + `gs201-panther.dts` in the mainline tree (Docker volume). Added `gs201-panther.dtb` to the exynos/google Makefile. The dtb BUILDS successfully (49 KB).

**Still pending (the real translation):** the gs201.dtsi still has gs101 peripheral addresses (UART 0x10900000, clock, etc.) and CPU `cortex-a76` (gs201 uses `cortex-a78`). These must be translated to gs201 addresses before the kernel boots further.

## gs201 adaptation — round 2 (CPU + UART, flashed correctly)

- Fixed `gs201.dtsi`: CPU `cortex-a76` → `cortex-a78` (+ pmu), UART driver `google,gs101-uart` → `samsung,exynos-uart`.
- Rebuilt `gs201-panther.dtb`.
- **Learned the Pixel 7 boot flow:** kernel and device tree go in SEPARATE partitions. Kernel → `boot` (header v4), device tree → `vendor_boot` (header v3). Flashing them together in one image does NOT work ("use unmerged" warning).
- Built `vendor_boot.img` with our dtb via `mkbootimg --vendor_boot ... --dtb gs201-panther.dtb --header_version 3`.
- Flashed both: `fastboot flash boot boot_v4.img` + `fastboot flash vendor_boot vendor_boot.img`.
- **Result:** still `boot failure`. The kernel still dies early (likely clock/pinctrl, which are not translated yet).

## THE KEY BLOCKER — no console output

The Pixel 7 has no accessible console: no screen output from a mainline kernel, and no serial port without opening the phone. So we cannot see WHERE the kernel panics. This makes the adaptation blind guesswork.

**Next options:**
1. **Pstore/ramoops** — kernel writes panic log to a reserved RAM region, read back after reboot. Needs kernel config (`CONFIG_PSTORE*`) + a reserved memory region in the dtb + a way to read it back.
2. Continue translating addresses blindly (slow).

## Factory image + recovery

- Downloaded the Pixel 7 factory image (2.6 GB): `panther-tq3a.230705.001.a1-factory-fb43c114.zip`.
- Extracted stock `boot.img` (64M) + `vendor_boot.img` (64M) — saved at `Documents/pac/panther-factory/panther-tq3a.230705.001.a1/extracted/`.
- Flashed stock boot + vendor_boot to recover the phone. It boots Android again.
- **Recovery is now trivial:** re-flash these two images anytime to return to stock Android.
- Next: use the stock `vendor_boot.img` as the base, replacing only the dtb (preserving the real vendor ramdisk, instead of our empty one).

## CRITICAL LESSON — boot loop from mismatched build

- We flashed `boot.img` + `vendor_boot.img` from the July factory image onto a phone whose `system` partition was still the April build → **boot loop** (bootloader → Google logo → off, repeating).
- **Fix:** flash the FULL factory image (`flash-all.sh`), which flashes ALL partitions (bootloader, radio, boot, vendor_boot, system, vendor, vendor_dlkm) to the same build. This resolved the loop.
- **Rule:** never flash boot/vendor_boot from a different build than the system partition. Always use matching builds, or flash the full image.

## Pstore debug — round 2 (root + correct address)

- Rooted the phone with Magisk v31.1 (patched `init_boot.img`). `su` works, `uid=0`.
- Found the STOCK ramoops address via root: `0xfd3ff000`, size 4 MB, console 2 MB.
- Our earlier custom address `0xfd400000` was WRONG (it overlapped the stock region).
- Fixed our dtb to use `0xfd3ff000` and re-ran the crash cycle.
- **Result: pstore STILL empty.** The stock kernel's dmesg shows `ramoops: using 0x400000@0xfd3ff000`, but no log was written by our kernel.

**Key conclusion: our kernel dies BEFORE ramoops init (~0.15s into boot).** The failure is in very early boot — CPU init, memory, or device tree parse — NOT the peripherals (clock/UART) we were translating.

**Next: isolate the early failure** with a minimal device tree (CPU + GIC + timer only, no peripherals/reserved-memory). If it reaches ramoops, the issue is in the peripheral config; if not, it's CPU/memory/device-tree.

## ROOT CAUSE FOUND — kernel header format mismatch

The Pixel 7 bootloader rejects our kernel because of a **header format difference**:

- Our mainline kernel: ARM64 magic `"ARMd"` at offset **0x38** (standard ARM64 Image).
- Stock Pixel 7 kernel: `"ARMd"` at offset **0x21** (Google-specific header layout), plus `"PE"` at 0x29.

The bootloader checks the magic at Google's offset (0x21), doesn't find it in our standard kernel, and rejects it → instant "boot failure" before any output. This explains ALL prior failures (dtb, config, page size were red herrings).

**Fix:** build/patch our kernel to match Google's header format (magic at 0x21). Need to determine the exact Google header spec or the build flag that produces it.

## Correct partition layout (Pixel 7)

- Kernel → `boot`
- **Device tree → `vendor_kernel_boot`** (NOT `vendor_boot`!) — this was a key mistake. `vendor_kernel_boot.img` contains the dtb (567 KB) + vendor kernel ramdisk (6 MB), header v4, page size 2048.
- Vendor ramdisk → `vendor_boot`
- Init ramdisk → `init_boot`

`vendor_kernel_boot` unpack: header v4, page size 0x800 (2048), kernel load address 0x10008000, dtb 567 KB at 0x11f00000.

## STILL UNSOLVED — kernel header format

Even with the correct partitions, our kernel still fails. The stock kernel has `"ARMd"` at offset **0x21** (Google-specific), ours at **0x38** (standard ARM64). The bootloader likely expects Google's header layout. Need to research the exact Pixel GKI kernel header spec.

## DEEP RESEARCH — how custom ROMs actually do it (LineageOS)

Read the full `BoardConfig-common.mk` for LineageOS gs201 (Pixel 7). KEY findings:

1. **`TARGET_NO_KERNEL := true`** + **`BOARD_PREBUILT_BOOTIMAGE`** — LineageOS does NOT build a kernel. It uses Google's STOCK prebuilt `boot.img` (GKI kernel).
2. **`BOARD_USES_GENERIC_KERNEL_IMAGE := true`** — GKI architecture. The kernel is generic (same for all devices); device-specific stuff is in dtb + vendor modules.
3. **`BOARD_RAMDISK_USE_LZ4 := true`** — ramdisk is LZ4 compressed.
4. **`BOARD_INCLUDE_DTB_IN_BOOTIMG := true`** + prebuilt DTB — the device tree is prebuilt and packed into `vendor_kernel_boot`.
5. Kernel cmdline: `earlycon=exynos4210,0x10A00000 console=ttySAC0,115200 clocksource=arch_sys_counter maxcpus=8` — note UART is at **0x10A00000** (not the addresses we were using).

**CONCLUSION: building a custom kernel from scratch on the Pixel 7 fights the GKI design.** The correct "custom OS" approach is: use the stock GKI kernel (it boots) + build your OWN device tree and vendor modules. That's what LineageOS does.

**Revised plan:** stop trying to build the kernel from source (needs Bazel/AOSP). Instead: stock GKI kernel + our own dtb + vendor modules.

## macOS port architecture (from Snapdragon reference)

Read macOS-uke + UniXNU READMEs fully. The boot flow is:

```
bootloader (ABL) → UEFI firmware (uefi-uke) → UniXNU (OpenCore-like) → XNU → macOS
```

- **UniXNU** = a general UEFI environment (like OpenCore) that loads drivers and boots iOS/macOS on non-Apple ARM64 via UEFI.
- **uefi-uke** = the UEFI firmware for the Snapdragon SoC, packed into `boot.img` (with the dtb).
- **XNU** = Apple's kernel, booted through UEFI (DarwinBoot).
- **GPU** = Adreno740 + Turnip + metal2vulkan (Snapdragon-specific).

For Tensor G2, the components to port: UEFI firmware (SoC bring-up), UniXNU platform, device tree → Apple ADT, XNU boot, UFS storage, GPU (PanVK for Mali).

**See `PORTING-PLAN.md` for the full roadmap.** Immediate next step: study the uefi-uke SoC bring-up and write the gs201 equivalent.

## Recovery note

The Pixel 7 is currently stuck in fastboot (our kernel fails to boot). To restore stock Android, re-flash the stock boot image (need factory image or OTA). The bootloader is unlocked and the phone is NOT bricked.

## Notes for Next Session

- Everything was set up on a Mac (darwin, arm64).
- `adb` and `fastboot` are installed (version 37.0.2) via Homebrew.
- Kernel builds happen inside Docker (Linux container), using a case-sensitive Docker volume.
- The Pixel 7 USB debugging may need re-authorization after unlock (data was wiped).

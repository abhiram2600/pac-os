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

## Recovery note

The Pixel 7 is currently stuck in fastboot (our kernel fails to boot). To restore stock Android, re-flash the stock boot image (need factory image or OTA). The bootloader is unlocked and the phone is NOT bricked.

## Notes for Next Session

- Everything was set up on a Mac (darwin, arm64).
- `adb` and `fastboot` are installed (version 37.0.2) via Homebrew.
- Kernel builds happen inside Docker (Linux container), using a case-sensitive Docker volume.
- The Pixel 7 USB debugging may need re-authorization after unlock (data was wiped).

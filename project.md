# PAC-OS

Build a custom operating system for the Google Pixel 7, starting from the lowest level the hardware allows.

## The Goal

Replace Android entirely with a custom OS on the Pixel 7's Tensor G2 silicon. Not a skin, not root — a full OS swap, proving the machine can be owned from the kernel up.

## Hardware

- **Device:** Google Pixel 7 (`panther`)
- **SoC:** Google Tensor G2 (GS201) — arm64
- **CPU:** Cortex-X1 / A78 / A55
- **GPU:** Mali-G710
- **Bootloader:** unlocked

## The Approach

Work from the lowest level the bootloader allows. Below the kernel is locked silicon. The kernel is the door. Above it, everything is replaceable.

```
SILICON (Tensor G2)          locked (given)
   ↓
BOOT ROM                     locked (given)
   ↓
BOOTLOADER (unlocked)        the door
   ↓
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   ↓   ← our work starts here
KERNEL                       we build this
   ↓
OS / everything above        we build this
```

## Milestones

1. **Boot a custom kernel** — build, flash, and boot a kernel we compiled ourselves.
   - Status: DONE — the kernel compiles, flashes, and the bootloader runs it. It fails mid-boot, which is expected; it needs Pixel 7 adaptation.
2. **Adapt the kernel to the Pixel 7** — translate the Tensor G2's hardware details so the kernel boots fully.
   - Status: IN PROGRESS.
3. **Full OS** — build the rest on top of a booting kernel.

## Current State

- [x] Bootloader unlocked
- [x] Custom kernel compiled and flashed (runs, then fails — expected)
- [x] Pixel 7 device tree scaffolded and building
- [ ] Pixel 7 address translation (CPU type, UART, clock)
- [ ] Full boot
- [ ] OS on top

## Key Concept

The bootloader is the gate. Once it's unlocked, the phone runs whatever we flash. The kernel is the first thing we author. Everything above it is ours to build.

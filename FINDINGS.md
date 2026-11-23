# PAC-OS — Current State & Working Findings

Last updated: 2026-11-22

Where we are right now, so a fresh session can pick up without reading
everything. See `project.md` for the goal and `PORTING-PLAN.md` for the
roadmap.

---

## TL;DR

- **Goal: run macOS on the Pixel 10a.** Boot Apple's XNU on Tensor G4, via
  the chain `Google bootloader (ABL) -> our UEFI -> UniXNU -> XNU -> macOS`.
- **The custom UEFI builds and flashes, but the bootloader was never
  running it.** The boot image was malformed. Fixed; not yet retested.
- **Blocked on one physical action:** the fastboot USB response path is
  wedged and needs the cable replugged. See "Recovery" below.

## Hardware (Pixel 10a)

- Codename **stallion**, SoC **Google Tensor G4 (`zumapro`)**, Exynos 2400
  derived (S5E9945). Bootloader unlocked, rooted with Magisk.
- RAM is **8 GB in four banks with a hole**, not contiguous:
  `0x80000000`, `0x880000000`, `0x900000000`, `0x980000000`, each 2 GB.
- GIC distributor `0x10400000` len `0x10000`; redistributors `0x10440000`
  len `0x200000`. 8 cores, 2 clusters.
- Watchdogs: cl0 `0x10060000`, cl1 `0x10070000`, both 30 s.
- ramoops reserved at `0xfd3ff000` len `0x400000` (console 2 MB, pmsg 2 MB).
- debug-snapshot header at `0xfd3f0000`; bootloader log region
  `bldr_log` at `0xfd800000`.
- DDR: Hynix LPDDR5, 8 GB (per `fastboot getvar`).

## The bug that mattered

Every earlier fix changed nothing, and the device always returned to
fastboot after the same ~30 s with `enter-reason: boot failure`. That
uniformity was the clue: **the firmware never executed.** The boot image
was malformed in two ways, both now fixed in `uefi-uke`:

1. **LZ4 framing.** We emitted a bare LZ4 block with no frame header. The
   stock Pixel kernel starts with the LZ4 *legacy* frame magic
   `0x184C2102`, and that is the only flavour this bootloader decodes.
2. **Appended DTB.** `append_dtb` glued 386 KB starting with `0xd00dfeed`
   after the compressed stream. The stock boot.img payload is exactly the
   LZ4 stream with **zero** trailing bytes. This device gets its device
   tree from `vendor_kernel_boot`.

Both verified byte for byte against the factory image. **The next test is
simply to flash the current `Mu-stallion.img` and see what changes.**

## Corrections to earlier notes

- **There is no `ConfigurationMapLib` in `stallionPkg`.** An earlier
  session named it the prime suspect; that file does not exist.
- **"Disabling both watchdogs caused a dead hang" was a misread.** The
  device was in fastboot the whole time; the poll just missed it. So there
  was never any evidence that the firmware executed.
- The claim that the image was already "LZ4 legacy block" was wrong. It
  was a raw block with no magic at all.

## Getting debug output off this device

No serial port without a special cable, and the device reserves no splash
framebuffer, so there is no console. What has been established:

- `/dev/mem` and `/proc/kcore` are compiled out. `ioremap` of RAM panics
  because `ioremap_guard` is on the kernel cmdline; `memremap` is correct
  for ramoops, which is normal memory, not `no-map`.
- An out-of-tree module can be built against `zumapro-kernel` (patch
  `out/include/generated/utsrelease.h` to the running
  `6.1.145-android14-...`, and disable `CONFIG_DEBUG_INFO_BTF` and the GKI
  symbol trimming). A no-op module is built at `~/Documents/pac/readmem`
  but has never been loaded successfully.
- **The working channel is ramoops**, no module needed. SEC writes plain
  text into the console zone using `struct persistent_ram_buffer` from
  `fs/pstore/ram_core.c`: signature `0x43474244`, start, size, then data.
  The stock kernel exposes it at `/sys/fs/pstore/console-ramoops-0`.
- **But this device scrubs DRAM on a clean reboot.** Proven by writing a
  marker to `/dev/pmsg0` and comparing: a plain `adb reboot` loses it
  entirely, while a forced panic (`echo c > /proc/sysrq-trigger`) brings it
  back intact with `ro.boot.bootreason = kernel_panic`. So the trace only
  survives if the boot that reads it follows **directly** from an abnormal
  reset, with no `fastboot reboot` in between.
- Unexplored and promising: the **`klog` partition**. Read it from Android
  with `dd if=/dev/block/by-name/klog`. If the bootloader logs there, it
  may say outright why it rejected the image.
- Also unexplored: writing the debug-snapshot panic sign (`0xBABA` at
  `0xFD3F0300`, per `dbg_snapshot_report_reason`) before resetting, to ask
  the bootloader to preserve DRAM.

## Build and test loop

The toolchain lives in a cached Docker image, so a rebuild is ~11 s:

```
docker build -t pac-uefi-build <dir with the Dockerfile>
docker run --rm -v ~/Documents/pac/uefi-uke:/uefi-uke -w /uefi-uke \
  pac-uefi-build bash -lc "python3 build_uefi.py -d stallion"
```

`Sec.c` carries 19 numbered checkpoints and `PAC_STOP_AT_STEP`. Setting it
to N forces an immediate watchdog reset on reaching checkpoint N, so a
fast return to fastboot means "got here" and a ~30 s return means "died
earlier". **The reset primitive itself is still unvalidated** — it never
produced a fast return, which is consistent with the firmware not running
at all, but it has not been proven to work. Validate it before trusting a
negative result. `Resources/PacDebug/bootshim-reachability-probe.patch`
puts the same reset in the shim's first instructions.

**Always wrap fastboot calls in `timeout`,** and never run a second
fastboot command while one is in flight. A killed in-flight command wedges
the response path.

## Recovery

- The phone is **not bricked**. Our UEFI is on `boot_a`; stock is at
  `stallion-factory/stallion-cp1a.260505.005/extracted/boot.img`.
- Restore with
  `fastboot flash boot <that path>` then `fastboot reboot`.
- **Current state: fastboot accepts no commands.** `fastboot devices` lists
  the phone (USB enumeration only) but every `getvar`/`flash` times out
  with no response. Commands are delivered, replies are not. Host side, not
  the phone. **Fix: unplug and replug the USB cable** (or power cycle the
  phone with Power held ~30 s, then Volume Down plus USB to re-enter
  fastboot). Then re-flash stock boot.

## Notes

- macOS host (Apple Silicon). UEFI builds happen in Docker.
- Key dirs: `~/Documents/pac/uefi-uke` (UEFI), `~/Documents/pac/zumapro-kernel`
  (Tensor kernel source, 6.1.134 vs 6.1.145 running),
  `~/Documents/pac/stallion-factory` (factory images),
  `~/Documents/pac/readmem` (kernel module scratch).
- The right reference device is **`e1sPkg` / `S5E9945Pkg`** (Galaxy S24,
  Exynos 2400), not the Snapdragon packages.

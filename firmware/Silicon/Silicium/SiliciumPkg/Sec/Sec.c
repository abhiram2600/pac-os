/**
  Copyright (c) 2011-2017, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/PrePiLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/TimerLib.h>
#include <Library/HobLib.h>
#include <Library/ArmSmcLib.h>

#include "Sec.h"

//
// PAC-OS boot trace.
//
// The Pixel 10a has no usable serial port and reserves no splash
// framebuffer, so there is no conventional way to see how far this
// firmware gets. Instead we write plain text into the device's ramoops
// console zone, which the stock device tree reserves at 0xFD3FF000.
// After a warm reset the stock Android kernel finds the zone, copies it
// aside and exposes the bytes at /sys/fs/pstore/console-ramoops-0. That
// turns "boot failure" into a readable trace.
//
// Use the PMSG zone, not the console zone. The ramoops region starts at
// 0xFD3FF000 with a 2 MB console zone followed by a 2 MB pmsg zone, and the
// Pixel bootloader writes its own log into that console zone. Sharing it
// means whoever writes last wins, and the bootloader runs after us on every
// retry, so our trace was being overwritten before anything could read it.
// Keeping to pmsg means both logs survive: the bootloader's at
// /sys/fs/pstore/console-ramoops-0 and ours at pmsg-ramoops-0.
//
// Layout is struct persistent_ram_buffer from fs/pstore/ram_core.c:
//   +0x0  UINT32 Sig   = PERSISTENT_RAM_SIG, 0x43474244 ("DBGC")
//   +0x4  UINT32 Start = read cursor, kept at 0 so the log stays linear
//   +0x8  UINT32 Size  = valid bytes in Data
//   +0xC  UINT8  Data[]
//
// The zone is 2 MB and ECC is off on this device (no ecc-size in the
// device tree), so no parity footer has to be maintained.
//
//
// Set by _ModuleEntryPoint from x0 before anything can clobber it: the
// address of the flattened device tree the bootloader passes us.
//
UINTN  mPacBootFdt = 0;

#define PAC_FDT_MAGIC   0xD00DFEED

#define PAC_TRACE_BASE  0xFD5FF000ULL
#define PAC_TRACE_SIG   0x43474244U
#define PAC_TRACE_MAX   0x8000U

VOID
PacTraceInit (VOID)
{
  volatile UINT32 *Header = (volatile UINT32 *)(UINTN)PAC_TRACE_BASE;

  Header[0] = PAC_TRACE_SIG;
  Header[1] = 0;
  Header[2] = 0;

  WriteBackDataCacheRange ((VOID *)(UINTN)PAC_TRACE_BASE, 12);
}

VOID
PacTrace (
  IN CONST CHAR8  *Message
  )
{
  volatile UINT32 *Header = (volatile UINT32 *)(UINTN)PAC_TRACE_BASE;
  volatile UINT8  *Data   = (volatile UINT8  *)(UINTN)(PAC_TRACE_BASE + 12);
  UINT32           Offset;
  UINT32           Index;

  if (Header[0] != PAC_TRACE_SIG) {
    PacTraceInit ();
  }

  Offset = Header[2];
  if (Offset > PAC_TRACE_MAX) {
    Offset = 0;
  }

  for (Index = 0; Message[Index] != '\0'; Index++) {
    if (Offset >= PAC_TRACE_MAX - 1) {
      break;
    }
    Data[Offset++] = (UINT8)Message[Index];
  }

  Data[Offset++] = '\n';

  Header[1] = 0;
  Header[2] = Offset;

  // Push it out of the caches now: the next thing that happens may be a
  // watchdog reset, and anything still sitting in a dirty cache line is
  // lost when that fires.
  WriteBackDataCacheRange ((VOID *)(UINTN)PAC_TRACE_BASE, 12 + Offset);
}

//
// Watchdog, properly armed this time.
//
// Writing WTCON/WTDAT/WTCNT alone does nothing on this SoC: the counter is
// gated and the reset line masked in the PMU. Values below come from the
// Google kernel's own driver, google-modules/soc/gs/drivers/watchdog/
// s3c2410_wdt.c, drv_data_zuma_cl0:
//
//   noncpu_out    0x1220, cnt_en_bit 4  (CNT_EN_WDT)
//   noncpu_int_en 0x1244, mask_bit   2  (nWDTRESET; set = reset enabled)
//
// with the PMU itself at system-controller@15460000. Note these differ from
// the generic Exynos850 values (bit 7, 0x1620/0x1644) that a first guess
// would use.
//
// This matters because nothing else resets this SoC once our firmware is
// running. Without it every hang needs the battery pulled by hand.
//
#define PAC_PMU_BASE               0x15460000ULL
#define PAC_PMU_CL0_NONCPU_OUT     0x1220
#define PAC_PMU_CL0_NONCPU_INT_EN  0x1244
#define PAC_PMU_CNT_EN_BIT         4
#define PAC_PMU_NWDTRESET_BIT      2

#define PAC_WDT_CL0_BASE           0x10060000ULL
#define PAC_WTCON                  0x00
#define PAC_WTDAT                  0x04
#define PAC_WTCNT                  0x08

#define PAC_WTCON_RSTEN            (1 << 0)
#define PAC_WTCON_ENABLE           (1 << 5)
#define PAC_WTCON_DIV16            (0 << 3)
#define PAC_WTCON_DIV128           (3 << 3)
#define PAC_WTCON_PRESCALE(x)      ((x) << 8)

//
// Roughly 20 seconds: OSCCLK is about 24.6 MHz, so with prescaler 256 and
// the /128 divider the counter ticks near 750 Hz, and 15000 ticks is ~20 s.
// Only the order of magnitude matters here.
//
// Maximum the counter allows, roughly 87 seconds at the prescaler and
// divider below. The earlier 20 second value was cutting a boot short that
// had stopped crashing: the log simply ended mid-DXE with no fault, because
// the deadline fired while the firmware was still making progress.
#define PAC_WDT_DEADLINE_COUNT     0xFFFF
#define PAC_WDT_DEADLINE_PRESCALE  0xFF
#define PAC_WDT_DEADLINE_DIV       PAC_WTCON_DIV128


//
// PMU register writes have to go through EL3.
//
// Reading PMU_ALIVE from here works, but writing it raises an uncontainable
// external abort: the block is write-protected against Non-secure access.
// That single store was the SError that stopped this port, and because
// SErrors are asynchronous it only surfaced later, inside ArmCpuDxe.
//
// The stock kernel never writes it directly either. From
// google-modules/soc/gs/drivers/soc/google/exynos-pmu-if.c, writes below
// offset 0x4000 go through an atomic bit-set alias handled by the EL3
// monitor:
//
//   set bit:   set_priv_reg(pmu_alive_pa + (offset | 0xC000), bit_index)
//   clear bit: set_priv_reg(pmu_alive_pa + (offset | 0x8000), bit_index)
//
// and set_priv_reg() is SMC 0x82000504 with option 1 (write), per
// drivers/soc/google/exynos-pd_el3.c. Note the argument is the bit *index*,
// not a mask.
//
#define SMC_CMD_PRIV_REG         0x82000504
#define PRIV_REG_OPTION_WRITE    1
#define PAC_PMU_ATOMIC_SET_BIT   0xC000
#define PAC_PMU_ATOMIC_CLR_BIT   0x8000

VOID
PacPmuSetBit (
  IN UINT32  Offset,
  IN UINT32  BitIndex
  )
{
  ARM_SMC_ARGS  Args;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_PRIV_REG;
  Args.Arg1 = (UINTN)(PAC_PMU_BASE + (Offset | PAC_PMU_ATOMIC_SET_BIT));
  Args.Arg2 = PRIV_REG_OPTION_WRITE;
  Args.Arg3 = BitIndex;
  ArmCallSmc (&Args);
}

VOID
PacArmWatchdog (
  IN UINT32  Count,
  IN UINT32  Prescale,
  IN UINT32  Divider
  )
{
  volatile UINT32 *Wdt = (volatile UINT32 *)(UINTN)PAC_WDT_CL0_BASE;

  // Ungate the counter and unmask the reset line, through EL3. A direct
  // store here aborts; see PacPmuSetBit above.
  PacPmuSetBit (PAC_PMU_CL0_NONCPU_OUT,    PAC_PMU_CNT_EN_BIT);
  PacPmuSetBit (PAC_PMU_CL0_NONCPU_INT_EN, PAC_PMU_NWDTRESET_BIT);

  Wdt[PAC_WTCON / 4] = 0;
  Wdt[PAC_WTDAT / 4] = Count;
  Wdt[PAC_WTCNT / 4] = Count;
  Wdt[PAC_WTCON / 4] = PAC_WTCON_ENABLE | PAC_WTCON_RSTEN | Divider |
                       PAC_WTCON_PRESCALE (Prescale);
}

//
// Self-reset.
//
// Nothing on this device resets the SoC once our firmware is running. The
// ~30 s "boot failure" we used to see was the bootloader's own timeout while
// it failed to load the image; now that the image loads, the bootloader is
// gone and any hang is permanent until the battery is pulled. So every dead
// end in this firmware has to reset deliberately.
//
// PSCI SYSTEM_RESET, because BL31 is present on this device and Android
// reboots through it. Before resetting we stamp the debug-snapshot "panic"
// reason, which is what the stock kernel writes in
// dbg_snapshot_report_reason(): the bootloader reads it and preserves DRAM
// instead of re-initialising it, which is the only way our trace survives
// into the next boot.
//
#define PAC_DSS_HEADER            0xFD3F0000ULL
#define PAC_DSS_EMERGENCY_REASON  0x300
#define PAC_DSS_SIGN_PANIC        0xBABA

#define PSCI_SYSTEM_RESET         0x84000009

VOID
PacReset (VOID)
{
  ARM_SMC_ARGS  Args;

  *(volatile UINT32 *)(UINTN)(PAC_DSS_HEADER + PAC_DSS_EMERGENCY_REASON) = PAC_DSS_SIGN_PANIC;
  WriteBackDataCacheRange ((VOID *)(UINTN)(PAC_DSS_HEADER + PAC_DSS_EMERGENCY_REASON), 4);

  // Reset via the watchdog, NOT via PSCI.
  //
  // PSCI SYSTEM_RESET is a clean reset, and this device re-initialises DRAM
  // on a clean boot, which destroys the trace we just wrote. A watchdog
  // reset is a warm reset and DRAM survives it. That difference is exactly
  // why an early fault handled through PSCI came back with an empty log
  // while a later fault that hung until the watchdog fired came back with
  // the full one.
  //
  // So: fire the watchdog with the smallest count and wait for it.
  // Use the same prescaler and divider as the deadline, which is known to
  // fire on this SoC, with a count worth roughly a second. A count of 1 with
  // a different divider was not reliably resetting, and falling through to
  // PSCI below scrubs DRAM and loses the log.
  PacArmWatchdog (750, PAC_WDT_DEADLINE_PRESCALE, PAC_WDT_DEADLINE_DIV);

  // Give the watchdog a long time to do its job before considering PSCI.
  // Reaching the PSCI call below means the watchdog is not working, and a
  // scrubbed log is better than a phone that needs its battery pulled.
  for (UINTN Spin = 0; Spin < 0x40000000; Spin++) {
    ArmDataSynchronizationBarrier ();
  }

  PacTrace ("reset: watchdog did not fire, falling back to PSCI");
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = PSCI_SYSTEM_RESET;
  ArmCallSmc (&Args);

  while (TRUE) {
  }
}

//
// SError tripwire.
//
// The fault that stops this port is an SError: an asynchronous external
// abort, ESR 0xBE000011 (EC 0x2F, DFSC 0x11, uncontainable). Asynchronous
// means it is raised by a bad bus access but only *taken* once something
// unmasks aborts, which on this firmware is ArmCpuDxe. That made the CPU
// driver look guilty when the real bad access happened much earlier.
//
// This briefly unmasks aborts so any pending SError is taken right here,
// with a trace line on either side. Whichever "sync" line is last in the
// log is the window the bad access lives in. In particular, if the very
// first sync point trips before we have touched any hardware ourselves,
// the SError was inherited from the bootloader.
//
VOID
PacSyncErrors (
  IN CONST CHAR8  *Tag
  )
{
  PacTrace (Tag);

  ArmDataSynchronizationBarrier ();
  ArmEnableAsynchronousAbort ();
  ArmInstructionSynchronizationBarrier ();
  ArmDataSynchronizationBarrier ();
  ArmDisableAsynchronousAbort ();
  ArmInstructionSynchronizationBarrier ();
}

//
// Find the bootloader's framebuffer.
//
// This board reserves no framebuffer: 0xFAC00000, where the Pixel 9 family
// keeps one, reads as plain System RAM in /proc/iomem here. But the
// bootloader clearly has one, since it draws the boot logo, and its log says
// which path is live:
//
//   [DECON] single dsi mode
//   [decon0] sel(0x0) OFIFO.0 - dsim0 - google-staea
//   [DECON] Display 1st window(5) xres(1080) yres(2424)
//
// So the address is in the display planes' DMA registers. Each drmdpp node
// in the device tree carries a second reg range for its DMA block, at
// 0x19930000 upwards, and RDMA_BASEADDR_Y8 sits at offset 0x40 within it
// (google-modules/display/samsung/cal_9845/regs-dpp.h).
//
// Read every plane and log whatever is there. The non-zero one is the
// buffer the bootloader is scanning out, and that address becomes the
// "Display Reserved" region SimpleFbDxe needs.
//
#define PAC_DPP_DMA_BASEADDR_Y8   0x40

#define PAC_DECON_BASE            0x19470000ULL
#define PAC_DECON_GLOBAL_CON      0x0020
#define PAC_DECON_BLD_BG_IMG_SIZE 0x0220
#define PAC_DECON_EN              (1 << 1)
#define PAC_DECON_RUN_STATUS      (1 << 4)
#define PAC_DECON_IDLE_STATUS     (1 << 5)

//
// DECON0's register blocks, from the drmdecon@19470000 reg/reg-names pair:
//   main 0x19470000  win 0x19480000  sub 0x19490000  wincon 0x194A0000
// Window registers are indexed by id at 0x1000 strides inside their block,
// per WIN_OFFSET(_id) in regs-decon.h.
//
#define PAC_DECON_WIN_BASE        0x19480000ULL
#define PAC_DECON_WINCON_BASE     0x194A0000ULL
#define PAC_DECON_WIN_STRIDE      0x1000

#define PAC_DECON_TRIG_CON        0x0030
#define PAC_DECON_RSC_STATUS_0    0x0100
#define PAC_DECON_DATA_PATH_CON_0 0x0200

#define PAC_WIN_START_POSITION    0x000C
#define PAC_WIN_END_POSITION      0x0010
#define PAC_WIN_COLORMAP_0        0x0014
#define PAC_DECON_SHD_REG_UP_REQ  0x0050
#define PAC_SHD_UP_REQ_GLOBAL     (1u << 31)
#define PAC_SHD_UP_REQ_CMP        (1u << 20)
#define PAC_SHD_UP_REQ_FOR_DECON  0x3FFF
#define PAC_SHD_UP_REQ_WIN(w)     (1u << (w))

#define PAC_TRIG_HW_TRIG_EN       (1u << 0)
#define PAC_TRIG_HW_MASK_DECON    (1u << 4)

//
// Counters, so this can be verified without looking at the panel. FRAME_COUNT
// advances once per frame DECON pushes out, HW_TE_CNT counts the panel's
// tearing-effect pulses, and the DMA reports busy while it is fetching.
//
#define PAC_DECON_FRAME_COUNT     0x0004
#define PAC_DECON_HW_TE_CNT       0x0038
#define PAC_DECON_INT_PEND        0x0070
#define PAC_DECON_INT_FRAME_START (1u << 12)
#define PAC_DECON_INT_FRAME_DONE  (1u << 13)
#define PAC_IDMA_OP_STATUS        (1u << 2)

//
// Display plane 0. DECON0 owns window 5, per RSC_STATUS_0 and RSC_STATUS_2.
//
// These two were the wrong way round for a long time, which is why nothing
// ever appeared on the panel: every plane register went into the other
// block. The device tree settles it. drmdpp@19900000 lists reg-names as
// "dma" then "dpp", and its reg ranges in that order are 0x19900000 and
// 0x19930000. So the DMA is the low block and the DPP the high one, and
// channel N is at base + N * 0x1000 in each.
//
#define PAC_DPP0_DMA_BASE         0x19900000ULL
#define PAC_DPP0_BASE             0x19930000ULL
#define PAC_DPP_DMA_STRIDE        0x1000
#define PAC_DPP_CHANNELS          7
#define PAC_DECON_WINDOW          5

// IDMA registers, offsets from cal_9845/regs-dpp.h
//
// This generation splits the source surface across two registers, where the
// older one packed both into 0x10. Writing the packed value here set a
// nonsense width and a height of zero, which stalled the plane the moment we
// started writing the right block at all. Confirmed by what the bootloader
// leaves behind: 0x10 reads 1080, the panel width, with nothing in the high
// half.
//
#define PAC_RDMA_ENABLE           0x0000
#define PAC_RDMA_IN_CTRL_0        0x0008
#define PAC_RDMA_SRC_WIDTH        0x0010
#define PAC_RDMA_SRC_HEIGHT       0x0014
#define PAC_RDMA_SRC_OFFSET       0x0018
#define PAC_RDMA_IMG_SIZE         0x001C
#define PAC_RDMA_BASEADDR_P0      0x0040
#define PAC_IDMA_SFR_UPDATE_FORCE (1u << 4)
#define PAC_IDMA_IMG_FORMAT_BGRX  4
#define PAC_IDMA_ALPHA_OPAQUE     (0xFFu << 24)

// DPP registers
#define PAC_DPP_COM_IO_CON        0x0038
#define PAC_DPP_COM_IMG_SIZE      0x003C
#define PAC_DPP_BPC_MODE_8BPC     (0u << 6)
#define PAC_DPP_IMG_FORMAT_ARGB   (0u << 0)

// The framebuffer the panel is actually being refreshed from, declared as
// "Display Reserved" in MemoryMapLib. See the comment there.
#define PAC_FB_BASE               0xFAC00000ULL

//
// Whether to paint into the buffer the bootloader is feeding the panel from.
//
// Off, because a boot that tried it hung hard with no USB and no way to read
// the log back, which also destroys the evidence. That buffer is very likely
// in a protected carveout, and a write into one of those on this SoC raises
// an abort nothing can contain. Find the address first, with reads only,
// then decide what is safe to write.
//
#define PAC_DRAW_INTO_LIVE_FB  0

//
// Filled in by the probe: the DMA block the bootloader is actually feeding,
// and the address it is fetching from. Zero means we did not find it.
//
STATIC UINTN   mPacActiveDmaBase = 0;
STATIC UINT32  mPacActiveFb      = 0;

#define PAC_FB_WIDTH              1080
#define PAC_FB_HEIGHT             2424

VOID
PacProbeFrameBuffer (VOID)
{
  CHAR8   Line[112];
  UINT32  Index;
  UINT32  GlobalCon;
  UINT32  BgSize;
  UINT32  Value;

  //
  // First settle the question that matters: is the display controller still
  // running when we get control?
  //
  // GLOBAL_CON is at 0x20 in each DECON, with DECON_EN at bit 1, RUN_STATUS
  // at bit 4 and IDLE_STATUS at bit 5. BLD_BG_IMG_SIZE_PRI at 0x220 holds the
  // configured image size. If that reads back as this panel's 1080 x 2424 and
  // DECON_EN is set, the bootloader left the display live and the framebuffer
  // is still being scanned out from somewhere. If it reads zero, the block is
  // powered down and we have to bring the display up ourselves.
  //
  // Offsets from google-modules/display/samsung/cal_9865/regs-decon.h.
  //
  for (Index = 0; Index < 3; Index++) {
    UINTN Decon = (UINTN)(PAC_DECON_BASE + (Index * 0x1000));

    GlobalCon = *(volatile UINT32 *)(Decon + PAC_DECON_GLOBAL_CON);
    BgSize    = *(volatile UINT32 *)(Decon + PAC_DECON_BLD_BG_IMG_SIZE);

    AsciiSPrint (
      Line,
      sizeof (Line),
      "fb: decon%d GLOBAL_CON=0x%08x en=%d run=%d idle=%d  BG_SIZE=0x%08x",
      Index,
      GlobalCon,
      (GlobalCon & PAC_DECON_EN) ? 1 : 0,
      (GlobalCon & PAC_DECON_RUN_STATUS) ? 1 : 0,
      (GlobalCon & PAC_DECON_IDLE_STATUS) ? 1 : 0,
      BgSize
      );
    PacTrace (Line);
  }

  PacSyncErrors ("fb: after reading DECON");
  PacTrace ("fb: DECON reads clean");

  //
  // Then read every plane's DMA block and report the whole state of each, not
  // just a base address. The point is to find the channel the bootloader is
  // feeding its logo from: that one is enabled, has a plausible DRAM address
  // and a source size matching the panel. Once we know which, we can draw
  // into the buffer the panel is already scanning out instead of setting up a
  // plane of our own and hoping.
  //
  // Offsets from regs-dpp.h: ENABLE 0x00, IN_CTRL_0 0x08, SRC_SIZE 0x10,
  // BASEADDR_Y8 0x40.
  //
  mPacActiveDmaBase = 0;
  mPacActiveFb     = 0;

  for (Index = 0; Index < PAC_DPP_CHANNELS; Index++) {
    UINTN   Dma = (UINTN)(PAC_DPP0_DMA_BASE + (Index * PAC_DPP_DMA_STRIDE));
    UINT32  Enable;
    UINT32  SrcSize;

    Enable  = *(volatile UINT32 *)(Dma + PAC_RDMA_ENABLE);
    SrcSize = *(volatile UINT32 *)(Dma + PAC_RDMA_SRC_WIDTH);
    Value   = *(volatile UINT32 *)(Dma + PAC_RDMA_BASEADDR_P0);

    AsciiSPrint (
      Line,
      sizeof (Line),
      "fb: dma%d en=0x%08x w=%u h=%u img=0x%08x base=0x%08x",
      Index,
      Enable,
      SrcSize,
      *(volatile UINT32 *)(Dma + PAC_RDMA_SRC_HEIGHT),
      *(volatile UINT32 *)(Dma + PAC_RDMA_IMG_SIZE),
      Value
      );
    PacTrace (Line);

    //
    // A real scanout address is in DRAM, which starts well above the
    // register space, and the surface has to be the size of the panel.
    //
    if (Value > 0x80000000 && SrcSize == PAC_FB_WIDTH) {
      mPacActiveDmaBase = Dma;
      mPacActiveFb      = Value;

      AsciiSPrint (
        Line,
        sizeof (Line),
        "fb: ACTIVE channel %d, panel is scanning out 0x%08x",
        Index,
        Value
        );
      PacTrace (Line);
    }
  }

  for (Index = 0; Index < 16; Index++) {
    UINTN Dma = (UINTN)(0x19D30000 + (Index * 0x1000));

    Value = *(volatile UINT32 *)(Dma + PAC_DPP_DMA_BASEADDR_Y8);
    if (Value != 0) {
      AsciiSPrint (Line, sizeof (Line), "fb: FOUND dma 0x%08x -> base 0x%08x", (UINT32)Dma, Value);
      PacTrace (Line);
    }
  }

  PacSyncErrors ("fb: after sweeping the plane DMA blocks");
  PacTrace ("fb: plane sweep clean");

  //
  // Dump what the bootloader left configured. Its log says the live path is
  //   [decon0] sel(0x0) OFIFO.0 - dsim0 - google-staea
  //   [DECON] Display 1st window(5) xres(1080) yres(2424)
  // so if window 5 is still enabled with sane geometry, most of the pipeline
  // is standing and we only have to point a plane at our own buffer and
  // trigger a frame, rather than configure the whole chain from scratch.
  //
  AsciiSPrint (
    Line,
    sizeof (Line),
    "fb: TRIG_CON=0x%08x DATA_PATH_CON_0=0x%08x",
    *(volatile UINT32 *)(UINTN)(PAC_DECON_BASE + PAC_DECON_TRIG_CON),
    *(volatile UINT32 *)(UINTN)(PAC_DECON_BASE + PAC_DECON_DATA_PATH_CON_0)
    );
  PacTrace (Line);

  for (Index = 0; Index < 7; Index++) {
    AsciiSPrint (
      Line,
      sizeof (Line),
      "fb: rsc%d=0x%08x",
      Index,
      *(volatile UINT32 *)(UINTN)(PAC_DECON_BASE + PAC_DECON_RSC_STATUS_0 + (Index * 4))
      );
    PacTrace (Line);
  }

  for (Index = 0; Index < 7; Index++) {
    UINTN Win    = (UINTN)(PAC_DECON_WIN_BASE    + (Index * PAC_DECON_WIN_STRIDE));
    UINTN WinCon = (UINTN)(PAC_DECON_WINCON_BASE + (Index * PAC_DECON_WIN_STRIDE));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "fb: win%d con=0x%08x start=0x%08x end=0x%08x cmap=0x%08x",
      Index,
      *(volatile UINT32 *)WinCon,
      *(volatile UINT32 *)(Win + PAC_WIN_START_POSITION),
      *(volatile UINT32 *)(Win + PAC_WIN_END_POSITION),
      *(volatile UINT32 *)(Win + PAC_WIN_COLORMAP_0)
      );
    PacTrace (Line);
  }

  PacSyncErrors ("fb: after reading the window configuration");
  PacTrace ("fb: window reads clean");
}

//
// Put our own pixels on the panel.
//
// The bootloader hands over with the display still standing: DECON0 enabled
// and sized for this panel, window 5 enabled full-screen, both DSC encoders
// and dsim0 live, and the panel retaining the boot logo on its own because
// this is a command-mode panel. What it does NOT leave is a plane pointed at
// memory, so every RDMA_BASEADDR_Y8 reads zero and nothing is being fetched.
//
// So the job is small: point channel 0 at a buffer, tell it the geometry and
// pixel format, latch the change, then let the trigger through so frames
// reach the panel. No power sequencing, no clocks, no panel init, because the
// bootloader already did all of that and it is still in effect.
//
// Register offsets and field encodings come from the Tensor kernel source,
// google-modules/display/samsung/cal_9865/regs-decon.h and
// cal_9845/regs-dpp.h.
//
VOID
PacBringUpDisplay (VOID)
{
  volatile UINT32  *Dma   = (volatile UINT32 *)(UINTN)PAC_DPP0_DMA_BASE;
  volatile UINT32  *Decon = (volatile UINT32 *)(UINTN)PAC_DECON_BASE;
  volatile UINT32  *Pixel;
  CHAR8             Line[112];
  UINT32            X;
  UINT32            Y;
  UINT32            Index;
  UINT32            Trig;

  //
  // Draw into the buffer the panel is already showing, when the probe found
  // it. That skips the whole question of configuring a plane correctly: the
  // controller is running, the panel is being refreshed, and the only thing
  // standing between us and visible output is which bytes it reads. Our own
  // reserved framebuffer is the fallback, and only useful once a plane of
  // ours is actually fetching.
  //
  // Writing into it is held back deliberately, see PAC_DRAW_INTO_LIVE_FB.
  if (mPacActiveFb != 0 && PAC_DRAW_INTO_LIVE_FB) {
    Pixel = (volatile UINT32 *)(UINTN)mPacActiveFb;
    AsciiSPrint (Line, sizeof (Line), "disp: drawing into the live buffer 0x%08x", mPacActiveFb);
    PacTrace (Line);
  } else {
    Pixel = (volatile UINT32 *)(UINTN)PAC_FB_BASE;
    PacTrace ("disp: drawing into our own reserved framebuffer");
  }

  //
  // Show what is in the buffer before we touch it. The logo is on screen, so
  // a buffer full of varied non-zero pixels is the logo and confirms we have
  // the right address; all zeroes would mean we are about to paint somewhere
  // nothing is reading.
  //
  AsciiSPrint (
    Line,
    sizeof (Line),
    "disp: fb 0x%08x holds 0x%08x 0x%08x 0x%08x",
    (UINT32)PAC_FB_BASE,
    ((volatile UINT32 *)(UINTN)PAC_FB_BASE)[0],
    ((volatile UINT32 *)(UINTN)PAC_FB_BASE)[(PAC_FB_WIDTH * PAC_FB_HEIGHT) / 2],
    ((volatile UINT32 *)(UINTN)PAC_FB_BASE)[PAC_FB_WIDTH * PAC_FB_HEIGHT - 1]
    );
  PacTrace (Line);

  PacTrace ("disp: painting test bands into the framebuffer");

  //
  // Four horizontal bands. Solid colour alone would prove the plane fetches
  // something; bands also prove the stride and height are right, because a
  // wrong stride shears them and a wrong height repeats or clips them.
  // Format is BGRX, so the byte order in memory is blue, green, red, unused.
  //
  for (Y = 0; Y < PAC_FB_HEIGHT; Y++) {
    UINT32 Colour;

    if (Y < PAC_FB_HEIGHT / 4) {
      Colour = 0x00FF0000;        // blue
    } else if (Y < PAC_FB_HEIGHT / 2) {
      Colour = 0x0000FF00;        // green
    } else if (Y < (PAC_FB_HEIGHT * 3) / 4) {
      Colour = 0x000000FF;        // red
    } else {
      Colour = 0x00FFFFFF;        // white
    }

    for (X = 0; X < PAC_FB_WIDTH; X++) {
      Pixel[(Y * PAC_FB_WIDTH) + X] = Colour;
    }
  }

  //
  // Deliberately do not touch the plane's geometry, pixel format or the DSC
  // settings behind it. The bootloader has them right: the panel is driven
  // through both DSC encoders and the combiner, and the output path register
  // says so. Reprogramming that from scratch means getting compression and
  // slice configuration right for no gain, when the only thing we need
  // changed is the contents of a buffer it is already fetching.
  //
  // Rewriting the base address with the value already there is safe and
  // keeps us honest about which buffer is live. The driver rewrites every
  // plane register on every frame even when nothing changed, because they
  // are shadowed and only take effect on the latch below.
  //
  PacTrace ("disp: pushing a frame");

  Dma[PAC_RDMA_BASEADDR_P0 / 4] = (UINT32)PAC_FB_BASE;

  PacSyncErrors ("disp: after rewriting the plane base");
  PacTrace ("disp: plane writes clean");

  //
  // Push one frame to the panel.
  //
  // This panel is command mode: it keeps showing the last frame it was sent
  // and refreshes itself, so new pixels in memory change nothing until a
  // transfer is started. A transfer starts on the panel's tearing-effect
  // signal, but only while the hardware trigger is unmasked, and the
  // bootloader leaves it masked. TRIG_CON reading 0x3070 is exactly that:
  // the trigger source is selected and the polarity configured, but
  // HW_TRIG_MASK_DECON is set and HW_TRIG_EN is clear.
  //
  // So the sequence is: wait for any outstanding latch to retire, mask the
  // trigger while we change things, ask for the shadow registers to be
  // latched, clear the stale frame-start flag, then unmask. The next
  // tearing-effect edge from the panel does the rest.
  //

  // 1. Any shadow update still in flight. The whole register reads back zero
  //    when the hardware has taken them.
  for (Index = 0; Index < 10000; Index++) {
    if (Decon[PAC_DECON_SHD_REG_UP_REQ / 4] == 0) {
      break;
    }

    MicroSecondDelay (10);
  }

  // 2. Mask the trigger while the shadow registers are being set up.
  Trig = Decon[PAC_DECON_TRIG_CON / 4];
  Trig = (Trig & ~(PAC_TRIG_HW_TRIG_EN | PAC_TRIG_HW_MASK_DECON)) |
         PAC_TRIG_HW_MASK_DECON;
  Decon[PAC_DECON_TRIG_CON / 4] = Trig;

  // 3. Latch every window, then the global and compression state. Two
  //    writes, in that order, which is what the driver does.
  Decon[PAC_DECON_SHD_REG_UP_REQ / 4] = PAC_SHD_UP_REQ_FOR_DECON;
  Decon[PAC_DECON_SHD_REG_UP_REQ / 4] = PAC_SHD_UP_REQ_GLOBAL |
                                        PAC_SHD_UP_REQ_CMP;

  // 4. Clear the stale frame-start flag so we can tell a new frame apart.
  Decon[PAC_DECON_INT_PEND / 4] = PAC_DECON_INT_FRAME_START;

  // 5. Unmask. This is the step that was missing: everything else was
  //    already in place and the trigger was simply gated off.
  Trig = Decon[PAC_DECON_TRIG_CON / 4];
  Trig = (Trig & ~(PAC_TRIG_HW_TRIG_EN | PAC_TRIG_HW_MASK_DECON)) |
         PAC_TRIG_HW_TRIG_EN;
  Decon[PAC_DECON_TRIG_CON / 4] = Trig;

  AsciiSPrint (Line, sizeof (Line), "disp: TRIG_CON now 0x%08x (unmasked)", Trig);
  PacTrace (Line);

  PacSyncErrors ("disp: after the latch and trigger");
  PacTrace ("disp: display bring-up clean");

  //
  // Now prove it without looking at the screen.
  //
  // Sample the counters, wait long enough for a handful of frames at panel
  // rate, then sample again. If FRAME_COUNT has advanced, DECON is pushing
  // frames; if HW_TE_CNT has advanced, the panel is asking for them; and if
  // the DMA reports busy, it is fetching our buffer. All three moving means
  // our pixels are reaching the panel whether or not anyone is watching.
  //
  {
    UINT32  FrameBefore = Decon[PAC_DECON_FRAME_COUNT / 4];
    UINT32  TeBefore    = Decon[PAC_DECON_HW_TE_CNT / 4];
    UINT32  FrameAfter;
    UINT32  TeAfter;
    UINT32  DmaStatus;

    AsciiSPrint (Line, sizeof (Line), "disp: before  frames=%u te=0x%08x", FrameBefore, TeBefore);
    PacTrace (Line);

    // ~500 ms. A command-mode panel at 60 Hz sends a tearing-effect edge
    // about every 16 ms, so this is tens of chances for a transfer to start.
    MicroSecondDelay (500000);

    FrameAfter = Decon[PAC_DECON_FRAME_COUNT / 4];
    TeAfter    = Decon[PAC_DECON_HW_TE_CNT / 4];

    //
    // Catch the DMA in the act of fetching.
    //
    // Busy is a momentary state: the plane reads the buffer in a burst at
    // the start of each frame and is idle the rest of the time, so sampling
    // once almost always reads idle and says nothing. Sampling across a
    // couple of frame intervals and counting the hits does say something,
    // because a plane that never reads is never busy.
    //
    {
      UINTN   Probe;
      UINT32  BusyHits = 0;
      UINTN   DmaBase  = (mPacActiveDmaBase != 0) ?
                         mPacActiveDmaBase :
                         (UINTN)PAC_DPP0_DMA_BASE;

      DmaStatus = 0;

      for (Probe = 0; Probe < 20000; Probe++) {
        UINT32  Sample = *(volatile UINT32 *)(DmaBase + PAC_RDMA_ENABLE);

        DmaStatus |= Sample;
        if (Sample & PAC_IDMA_OP_STATUS) {
          BusyHits++;
        }
      }

      AsciiSPrint (
        Line,
        sizeof (Line),
        "disp: dma fetching on %u of 20000 samples, enable=0x%08x",
        BusyHits,
        DmaStatus
        );
      PacTrace (Line);
    }

    AsciiSPrint (Line, sizeof (Line), "disp: after   frames=%u te=0x%08x", FrameAfter, TeAfter);
    PacTrace (Line);

    AsciiSPrint (
      Line,
      sizeof (Line),
      "disp: VERDICT frames+%u dma_busy=%u int_pend=0x%08x",
      FrameAfter - FrameBefore,
      (DmaStatus & PAC_IDMA_OP_STATUS) ? 1 : 0,
      Decon[PAC_DECON_INT_PEND / 4]
      );
    PacTrace (Line);

    //
    // Frames advancing on its own proves only that the controller is still
    // refreshing the panel with whatever it had before, which it was doing
    // before we touched anything. This used to claim success on that alone
    // and it was wrong for weeks. Our pixels are only reaching the panel if
    // a plane is fetching from a buffer we wrote.
    //
    if (FrameAfter == FrameBefore) {
      PacTrace ("disp: no frames pushed; trigger or latch is still wrong");
    } else if (mPacActiveFb == (UINT32)PAC_FB_BASE) {
      PacTrace ("disp: OUR PIXELS ARE ON THE PANEL (we drew where it is fetching)");
    } else if (DmaStatus & PAC_IDMA_OP_STATUS) {
      PacTrace ("disp: OUR PIXELS ARE ON THE PANEL (our plane is fetching)");
    } else {
      PacTrace ("disp: panel refreshing but no plane of ours is fetching");
    }
  }
}

//
// PAC-OS checkpoint probe.
//
// DRAM does not survive the clean reboot we need in order to get Android
// back up and read the trace above, so the trace alone cannot tell us how
// far this firmware got. This does, using the only signal the bootloader
// gives us for free: how long the device takes to come back to fastboot.
//
// Cluster 1's watchdog resets this SoC after its stock 30 second timeout,
// which is what we have been seeing as "boot failure". If instead we reach
// the checkpoint selected below and force that same watchdog to fire
// immediately, the device returns to fastboot in a couple of seconds. So:
//
//   back in ~2s   -> execution reached PAC_STOP_AT_STEP
//   back in ~30s  -> it died before PAC_STOP_AT_STEP
//
// Walk PAC_STOP_AT_STEP forward (or bisect) to find the last checkpoint
// that still gives the fast return. The step after that is where it dies.
// Set PAC_STOP_AT_STEP to 0 for a normal build with no forced reset.
//
#define PAC_STOP_AT_STEP  0

//
// Watchdog "reset right now".
//
// This is the exact register sequence the stock kernel uses in
// s3c2410wdt_restart() (drivers/watchdog/s3c2410_wdt.c): disable the
// watchdog so the reload takes, load a small count, then re-enable it with
// its reset line armed. Known-good code, so a failure to reset means we
// never got here rather than that the sequence is wrong.
//
// Both clusters are fired because we do not know which one the bootloader
// armed, and firing an already-stopped one is harmless.
//
#define PAC_WDT_CL0_BASE  0x10060000ULL
#define PAC_WDT_CL1_BASE  0x10070000ULL

#define PAC_WTCON_RSTEN         (1 << 0)
#define PAC_WTCON_ENABLE        (1 << 5)
#define PAC_WTCON_DIV16         (0 << 3)
#define PAC_WTCON_PRESCALE(x)   ((x) << 8)

STATIC
VOID
PacWdtFire (
  IN UINTN  Base
  )
{
  volatile UINT32 *Wdt = (volatile UINT32 *)Base;

  Wdt[0] = 0;     // WTCON - stop it first
  Wdt[2] = 0x80;  // WTCNT
  Wdt[1] = 0x80;  // WTDAT
  Wdt[0] = PAC_WTCON_ENABLE | PAC_WTCON_DIV16 | PAC_WTCON_RSTEN | PAC_WTCON_PRESCALE (0x20);
}

STATIC
VOID
PacHardReset (VOID)
{
  PacWdtFire (PAC_WDT_CL0_BASE);
  PacWdtFire (PAC_WDT_CL1_BASE);

  // Wait for it.
  while (TRUE) {
  }
}

STATIC
VOID
PacStep (
  IN UINT32        Step,
  IN CONST CHAR8  *Message
  )
{
  // Reset check first, deliberately. Checkpoint 1 has to be reachable
  // without this file having written a single byte of memory beforehand,
  // otherwise a fault in the trace write itself looks like "never got here".
  if (PAC_STOP_AT_STEP != 0 && Step == PAC_STOP_AT_STEP) {
    PacHardReset ();
  }

  PacTrace (Message);
}

#ifndef MDEPKG_NDEBUG
STATIC
VOID
ClearFrameBuffer (VOID)
{
  EFI_MEMORY_REGION_DESCRIPTOR FrameBufferRegion;
  EFI_STATUS                   Status;

  // Same hazard as SimpleFbDxe: LocateMemoryRegionByName leaves its output
  // untouched when it fails, so without clearing this first a device that
  // declares no framebuffer region would ZeroMem a stack-garbage address
  // and length. Clear it and check the status.
  ZeroMem (&FrameBufferRegion, sizeof (FrameBufferRegion));

  // Locate "Display Reserved" Memory Region
  Status = LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  if (EFI_ERROR (Status)) {
    Status = LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);
  }

  // Clear Memory Region
  if (!EFI_ERROR (Status) && FrameBufferRegion.Address != 0 && FrameBufferRegion.Length != 0) {
    ZeroMem ((VOID *)FrameBufferRegion.Address, FrameBufferRegion.Length);
  }
}

STATIC
VOID
PrintFirmwareVersion (VOID)
{
  // Clear Frame Buffer
  ClearFrameBuffer ();

  // Print UEFI Version Message
  DEBUG ((EFI_D_WARN, "\n"));
  DEBUG ((EFI_D_WARN, "PAC-OS for %a %a\n",                      FixedPcdGetPtr (PcdSmbiosSystemManufacturer), FixedPcdGetPtr (PcdSmbiosSystemModel)));
  DEBUG ((EFI_D_WARN, "Built on Project Silicium, Project Mu and EDK2\n"));
  DEBUG ((EFI_D_WARN, "Firmware Version %s Build at %a on %a\n", PcdGetPtr (PcdFirmwareVersionString), __TIME__, __DATE__));
  DEBUG ((EFI_D_WARN, "\n"));
}
#endif

STATIC
EFI_STATUS
InitializeMemory (VOID)
{
  EFI_STATUS                    Status;
  EFI_MEMORY_REGION_DESCRIPTOR  UefiMemoryRegion;
  EFI_HOB_HANDOFF_INFO_TABLE   *HobList;

  // Locate "DXE Heap" Memory Region
  LocateMemoryRegionByName ("DXE Heap", &UefiMemoryRegion);
  LocateMemoryRegionByName ("DXE_Heap", &UefiMemoryRegion);

  // Save UEFI Memory Details
  UINTN UefiMemoryBase = UefiMemoryRegion.Address;
  UINTN UefiMemorySize = UefiMemoryRegion.Length;

  // Verify UEFI Memory Base
  if (UefiMemoryBase == 0 && UefiMemorySize == 0) {
    PacTrace ("mem: DXE Heap region not found");
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"DXE Heap\" Memory Region!\n"));
    return EFI_NOT_FOUND;
  }
  PacStep (8, "mem: located DXE Heap");

  // Initialize HOB List
  HobList = HobConstructor ((VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase, (VOID *)(UefiMemoryBase + UefiMemorySize));
  PrePeiSetHobList (HobList);
  PacStep (9, "mem: HOB list constructed");

  // Initialize MMU & Memory HOBs
  PacStep (10, "mem: entering MemoryPeim (MMU enable)");
  Status = MemoryPeim (UefiMemoryBase, UefiMemorySize);
  if (EFI_ERROR (Status)) {
    PacTrace ("mem: MemoryPeim FAILED");
    return Status;
  }
  PacStep (11, "mem: MemoryPeim returned, MMU is on");

  // Build Stack HOB
  BuildStackHob (FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));
  PacStep (12, "mem: stack HOB built");

  return EFI_SUCCESS;
}

STATIC
VOID
SecMain (VOID)
{
  EFI_STATUS Status;

  // Do Platform Specific Initialization
  PacStep (6, "sec: calling PlatformInitialize");
  PlatformInitialize ();
  PacStep (7, "sec: PlatformInitialize done");
  PacSyncErrors ("sync D: after PlatformInitialize");
  PacTrace ("sync D: clean");

#ifndef MDEPKG_NDEBUG
  // Initialize Serial Port
  Status = SerialPortInitialize ();
  if (EFI_ERROR (Status)) {
    PacTrace ("sec: SerialPortInitialize FAILED");
    return;
  }

  // Print Firmware Version
  PrintFirmwareVersion ();

  // Initialize Debug Agent
  InitializeDebugAgent (DEBUG_AGENT_INIT_POSTMEM_SEC, NULL, NULL);
  SaveAndSetDebugTimerInterrupt (TRUE);
#endif

  // Initialize Memory
  Status = InitializeMemory ();
  if (EFI_ERROR (Status)) {
    PacTrace ("sec: InitializeMemory FAILED");
    return;
  }
  PacStep (13, "sec: memory initialized");
  PacSyncErrors ("sync E: after MMU enable");
  PacTrace ("sync E: clean");

  // Needs the MMU, since the display registers are only mapped by MemoryPeim.
  PacProbeFrameBuffer ();
  PacBringUpDisplay ();

  // Build CPU HOB
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));
  PacStep (14, "sec: CPU HOB built");

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);
  PacStep (15, "sec: boot mode set");

  // Initialize Platform HOBs
  Status = PlatformPeim ();
  if (EFI_ERROR (Status)) {
    PacTrace ("sec: PlatformPeim FAILED");
    return;
  }
  PacStep (16, "sec: PlatformPeim done");

  // Run Library Cconstructors
  ProcessLibraryConstructorList ();
  PacStep (17, "sec: library constructors done");

  // Decompress all FVs
  Status = DecompressFirstFv ();
  if (EFI_ERROR (Status)) {
    PacTrace ("sec: DecompressFirstFv FAILED");
    DEBUG ((EFI_D_ERROR, "Failed to Decompress FV! Status = %r\n", Status));
    return;
  }
  PacStep (18, "sec: FV decompressed");

  // Load DXE Core
  PacSyncErrors ("sync F: just before DXE Core");
  PacTrace ("sync F: clean");
  PacStep (19, "sec: loading DXE Core");
  Status = LoadDxeCoreFromFv (NULL, 0);
  if (EFI_ERROR (Status)) {
    PacTrace ("sec: LoadDxeCoreFromFv FAILED");
    DEBUG ((EFI_D_ERROR, "Failed to Load DXE Core! Status = %r\n", Status));
  }
  PacTrace ("sec: LoadDxeCoreFromFv returned (should not happen)");
}

VOID
SecEntry (VOID)
{
  // Stamp the debug-snapshot panic reason as the very first thing we do.
  //
  // This asks the bootloader to preserve DRAM instead of re-initialising it,
  // and without it nothing we write afterwards survives to be read. It has
  // to come before even the first trace line, because if we die early the
  // trace is the only evidence and a scrubbed DRAM destroys it. A previous
  // run stamped this after the first tripwire, died at the tripwire, and
  // came back with an empty log that said nothing at all.
  *(volatile UINT32 *)(UINTN)(PAC_DSS_HEADER + PAC_DSS_EMERGENCY_REASON) = PAC_DSS_SIGN_PANIC;
  WriteBackDataCacheRange ((VOID *)(UINTN)(PAC_DSS_HEADER + PAC_DSS_EMERGENCY_REASON), 4);

  // Checkpoint 1 runs before anything else touches memory.
  PacStep (1, "PAC-OS trace: SecEntry reached");

  // Install our exception vector table FIRST. The SError tripwires below
  // are useless without a handler of our own, and an SError taken against
  // whatever VBAR the bootloader left behind goes somewhere unknowable.
  ASSERT (((UINTN)SecVectorTable & ARM_VECTOR_TABLE_ALIGNMENT) == 0);
  ArmWriteVBar ((UINTN)SecVectorTable);

  // Start a fresh trace. Anything already in the zone is the previous
  // Android boot's console log, which we do not want to append to.
  PacTraceInit ();
  PacTrace ("sec: vector table installed");

  // Tripwire A. Nothing of ours has touched hardware yet, so if this one
  // trips the SError came in from the bootloader.
  PacSyncErrors ("sync A: entry, before any of our MMIO");
  PacTrace ("sync A: clean");

  //
  // Report the device tree the bootloader handed over. Its header is
  // big-endian: magic 0xD00DFEED then the total size.
  //
  // Read before MemoryPeim deliberately. The bootloader puts this tree
  // wherever it likes in RAM, and MemoryMapLib maps only named regions,
  // so once the MMU is on the address is unmapped and reading it takes a
  // translation fault. Doing it here, with the MMU still off, every
  // physical address is reachable. Whatever we go on to use out of this
  // tree has to be mapped explicitly.
  //
  {
    CHAR8   Line[112];
    UINT32  Magic = 0;
    UINT32  Size  = 0;

    if (mPacBootFdt != 0) {
      CONST UINT8 *Fdt = (CONST UINT8 *)mPacBootFdt;

      Magic = ((UINT32)Fdt[0] << 24) | ((UINT32)Fdt[1] << 16) |
              ((UINT32)Fdt[2] << 8)  |  (UINT32)Fdt[3];
      Size  = ((UINT32)Fdt[4] << 24) | ((UINT32)Fdt[5] << 16) |
              ((UINT32)Fdt[6] << 8)  |  (UINT32)Fdt[7];
    }

    AsciiSPrint (
      Line,
      sizeof (Line),
      "fdt: x0=0x%lx magic=0x%08x size=%u %a",
      (UINT64)mPacBootFdt,
      Magic,
      Size,
      (Magic == PAC_FDT_MAGIC) ? "VALID" : "not a device tree"
      );
    PacTrace (Line);
  }


  // Arm a hard deadline. Any lock-up from here on warm-resets the SoC
  // instead of wedging the phone, and a warm reset is also what preserves
  // the trace in DRAM.
  //
  // NOTE: must be disabled or petted once the port reaches DXE, or it will
  // kill a firmware that is actually working.
  //
  // Arm the deadline, with a tripwire after each stage.
  //
  // The PMU half now goes through EL3 instead of storing directly, which is
  // what tripwire B2 caught. Keeping the tripwires until a run comes back
  // with every stage clean.
  //
  {
    volatile UINT32 *Wdt = (volatile UINT32 *)(UINTN)PAC_WDT_CL0_BASE;

    PacPmuSetBit (PAC_PMU_CL0_NONCPU_OUT, PAC_PMU_CNT_EN_BIT);
    PacSyncErrors ("sync B2: after PMU noncpu_out via EL3");
    PacTrace ("sync B2: clean");

    PacPmuSetBit (PAC_PMU_CL0_NONCPU_INT_EN, PAC_PMU_NWDTRESET_BIT);
    PacSyncErrors ("sync B3: after PMU noncpu_int_en via EL3");
    PacTrace ("sync B3: clean");

    Wdt[PAC_WTCON / 4] = 0;
    PacSyncErrors ("sync B4: after WTCON stop");
    PacTrace ("sync B4: clean");

    Wdt[PAC_WTDAT / 4] = PAC_WDT_DEADLINE_COUNT;
    Wdt[PAC_WTCNT / 4] = PAC_WDT_DEADLINE_COUNT;
    Wdt[PAC_WTCON / 4] = PAC_WTCON_ENABLE | PAC_WTCON_RSTEN |
                         PAC_WDT_DEADLINE_DIV |
                         PAC_WTCON_PRESCALE (PAC_WDT_DEADLINE_PRESCALE);
    PacSyncErrors ("sync B5: after WTCON enable");
    PacTrace ("sync B5: clean");
  }

  PacTrace ("sec: watchdog deadline armed");

  // Disable Data Cache
  ArmDisableDataCache ();

  // Invalidate Instruction Cache
  ArmInvalidateInstructionCache ();

  // Enable Instruction Cache
  ArmEnableInstructionCache ();
  PacStep (2, "sec: caches configured");

  // Invalidate Stack & UEFI FD D-Cache
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdCPUCoresStackBase), FixedPcdGet32 (PcdCPUCorePrimaryStackSize));
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdFdBaseAddress),     FixedPcdGet32 (PcdFdSize));
  PacStep (3, "sec: stack and FD caches invalidated");

  // Tripwire C. Covers the cache maintenance above.
  PacSyncErrors ("sync C: after cache maintenance");
  PacTrace ("sync C: clean");

  // Enter Main SEC Function
  PacStep (5, "sec: entering SecMain");
  SecMain ();
  PacTrace ("sec: SecMain returned (boot aborted)");
}

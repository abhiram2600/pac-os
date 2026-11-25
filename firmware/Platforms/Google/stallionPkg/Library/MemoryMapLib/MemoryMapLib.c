#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // Reserved memory (firmware + DMA)
  {"gsa", 0x90200000, 0x3ff000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"gxp_scratchpad", 0x91300000, 0x60000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"iif_wait_table", 0x91360000, 0x20000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"iif_signal_table", 0x91380000, 0x20000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"gxp_secure_data", 0x91400000, 0x900000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"gxp_fw", 0x91d00000, 0x300000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"gxp_mcu_fw", 0x92000000, 0x400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"tpu_fw", 0x92e00000, 0x1200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"aoc", 0x94000000, 0x3000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"xhci_dma_dram", 0x97000000, 0x400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"dss_log", 0xfd3f0000, 0xe000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"debug_kinfo", 0xfd3fe000, 0x1000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  // ramoops: the stock kernel exposes this zone at /sys/fs/pstore after a
  // warm reset, so SEC writes its boot trace here. Must stay mapped once
  // the MMU comes up or the later trace writes fault.
  {"ramoops", 0xfd3ff000, 0x400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"bldr_log", 0xfd800000, 0x100000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},

  // UEFI regions (in free RAM below the reserved block)
  // The framebuffer the bootloader is already showing its logo from, and
  // which the display controller is still fetching when we take over. The
  // address used to be a carve-out of free RAM chosen by us, which is why
  // nothing ever appeared: the panel was never reading from it. DPP0's read
  // DMA points here instead, and the same address holds across this family
  // of Tensor phones. It sits in RAM the bootloader leaves free, between
  // its own reservation ending at 0xF9800000 and the debug buffers starting
  // at 0xFD3F0000, so nothing else claims it.
  //
  // 1080 * 2424 * 4 is 0x9FC600, rounded up to a whole number of pages.
  // WRITE_THROUGH because the display fetches this by DMA and must not need
  // a cache flush first.
  {"Display Reserved", 0xFAC00000, 0x00A00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"UEFI FD", 0xF4800000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI Stack", 0xF4B00000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"DXE Heap", 0xF4B40000, 0x01000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},

  // Register Regions
  {"Watchdog Timer", 0x10060000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Watchdog CL1", 0x10070000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // GIC sizes come straight from the device tree node interrupt-controller@10400000:
  //   distributor    0x10400000 len 0x10000
  //   redistributors 0x10440000 len 0x200000
  // The single 0x60000 "GIC" region this replaced covered only 0x20000 of the
  // redistributors. GICv3 uses a 0x20000 stride per core, so with 8 cores the
  // driver would walk off the end of the mapping and take a data abort.
  // Looked up by name from ChipInfoDxe: chipid@10000000 in the device tree,
  // compatible "google,zuma-chipid".
  {"Chip Info", 0x10000000, 0xD000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GICD", 0x10400000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GICR", 0x10440000, 0x200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"UART", 0x10870000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // Looked up by name from PmicDxe: system-controller@15460000, compatible
  // "samsung,gs101-pmu". Note this is NOT the Exynos 2400 address (0x12860000).
  {"PMU", 0x15460000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // DECON0 register blocks, from the drmdecon@19470000 reg/reg-names pair
  // in the device tree: main, win, sub, wincon, dqe, dqe-cgc, cgc-dma.
  // Only main, win and wincon are needed to drive a window.
  {"DRM Decon", 0x19470000, 0x6000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Decon Win", 0x19480000, 0xE000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Decon Wincon", 0x194A0000, 0xE000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // Display planes: the DPP blocks at 0x19900000 and their DMA blocks at
  // 0x19930000, from each drmdpp node's first two reg ranges. Channel 0 is
  // the one DECON0 owns, per RSC_STATUS_0.
  {"DPP", 0x19900000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},

  // Storage. ufs@13200000 lists six reg ranges; one 0x10000 region covers
  // the four that sit together (standard HCI at +0, vendor HCI at +0x1100,
  // PHY at +0x4000, cport at +0x8000). UniPro and the protector block are
  // separate. sysreg_ufs carries the IO coherency bits.
  {"UFS", 0x13200000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"UFS UniPro", 0x13280000, 0x8000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"UFS Protector", 0x132A0000, 0xB000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SYSREG UFS", 0x13020000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // Display planes. Each drmdpp node in the device tree has a second reg
  // range here for its DMA block, and the framebuffer address the
  // bootloader is scanning out lives at offset 0x40 in one of them.
  {"DPP DMA", 0x19930000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"DPP DMA 1", 0x19D30000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},

  // Not present on this SoC, so deliberately absent:
  //   "NS_IRAM"          - zumapro has no iram/sram node at all, so
  //                        RamManagerLib will not find it.
  // Note: there is no framebuffer reservation in the device tree, which was
  // once read as meaning there is no bootloader framebuffer to find. There
  // is one; it is simply not reserved, because the kernel takes the memory
  // back once its own display driver is up.
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}

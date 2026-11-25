##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = stallion
  PLATFORM_GUID                  = 9146BB63-EE16-4238-A6C6-36472B925699
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/stallionPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = stallionPkg/stallion.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = Exynos 2400
  # 1 = Exynos 2400e
  #
  SOC_TYPE                       = 0

!include zumaproPkg/zumaproPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # Debug verbosity
  #
  # SiliciumPkg.dsc.inc uses 0x8007EE0F for DEBUG builds, which leaves
  # DEBUG_INFO (0x40) off, and that is where most of the EDK2 narration
  # lives. With no serial port there is no cost to printing everything into
  # the ramoops buffer, and the port currently stops with no error at all,
  # so we need every line we can get.
  #
  # ERROR | INIT | WARN | LOAD | FS | INFO | BM.
  #
  # Not 0xFFFFFFFF any more: DEBUG_POOL and DEBUG_PAGE log every single
  # allocation, which filled the 2 MB trace buffer mid-boot and truncated the
  # log exactly where it was getting interesting. Everything that explains
  # behaviour is still here.
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000044F

  #
  # UFS
  #
  # bRefClkFreq 2 is 38.4 MHz, which is what the device itself reports once
  # descriptor reads actually work: "UFS RefClk: device says 2".
  #
  # Earlier this was set to 0 on the strength of the kernel line
  # "exynos_rate_to_clk, ref_clk = 19200000", but that is the USB PHY, not
  # UFS. It also looked confirmed by a log line reading "already 0", which
  # was really an empty response buffer before the UTRD granularity was
  # fixed. The bootloader had a working gear 4 link with the device set to 2,
  # so 2 is the value to keep.
  #
  gSamsungPkgTokenSpaceGuid.PcdUfsRefClkFreq|0x02

  # This controller counts the UTRD offsets in 4-byte words, not bytes. Its
  # ufs node declares "fixed-prdt-req_list-ocs", which is exactly what makes
  # the stock driver clear UFSHCD_QUIRK_PRDT_BYTE_GRAN.
  gSamsungPkgTokenSpaceGuid.PcdUfsPrdtByteGranularity|FALSE

  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xF4B00000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Google"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 10a"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"stallion"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_10a_stallion"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 10a"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  # 2424, not 2400. The bootloader reports the active window itself:
  #   [DECON] Display 1st window(5) xres(1080) yres(2424)
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2424

  # Draw every pixel three times over. The console font is a fixed 8 by 19
  # bitmap, so at this panel's density a character is about a millimetre tall
  # and the whole console sits in a small box in the middle of the screen.
  # Three gives characters about 24 by 57 real pixels, and 45 columns by 42
  # rows, which fills the width exactly.
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferScale|3

  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

[LibraryClasses]
  #
  # Debug Libraries
  #
  # This device has no usable serial port and no splash framebuffer, so both
  # stock SerialPortLib choices are dead ends: the framebuffer one draws into
  # memory that does not exist and the null one discards everything. Route
  # debug output into the ramoops pmsg zone instead, where the same buffer
  # carries the SEC boot trace and the stock kernel surfaces it at
  # /sys/fs/pstore/pmsg-ramoops-0.
  #
  # Overrides the choice made in SiliciumPkg.dsc.inc, which this DSC includes
  # above.
  #
  SerialPortLib|SiliciumPkg/Library/RamoopsSerialPortLib/RamoopsSerialPortLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|stallionPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries
  #
  KeypadDeviceLib|stallionPkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

[Components]
  #
  # Payload
  #
  # Built as a standalone application rather than put in the firmware volume.
  # It is copied onto the payload filesystem, so starting it proves the
  # firmware can mount a volume and run an executable from it.
  #
  SiliciumPkg/Application/PacPayloadTest/PacPayloadTest.inf
  SiliciumPkg/Drivers/PacPayloadVolumeDxe/PacPayloadVolumeDxe.inf

  #
  # ACPI Tables
  #
  #stallion/AcpiTables.inf

  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf

[PcdsPatchableInModule]
  # Let the console pick the largest text mode rather than insisting on
  # exactly 80 by 25.
  #
  # The splitter searches the mode list for those exact dimensions and, when
  # it finds none, asks for mode 0xFF, which does not exist; the failure then
  # asserts and the console never comes up. Nothing here is 80 columns wide
  # once the font is drawn at a readable size, so there is no such mode to
  # find. Zero means take the biggest, which is the full screen.
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0

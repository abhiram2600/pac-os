##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  #
  # Timer
  #
  USE_PHYSICAL_TIMER                         = 1

  #
  # KEP
  #
  HAS_CNTFRQ_EL0_EMPTY_ERRATA                = 0

!include SamsungPkg/SamsungPkg.dsc.inc

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -march=armv9.2-a+crypto+rcpc

[PcdsFixedAtBuild]
  #
  # ACPI
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x2035343939453553
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x9945

  #
  # CPU
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # GIC
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x10400000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x10440000

  #
  # GPIO
  # 13 pinctrl controllers, verified against the zumapro device tree:
  # 0x10840000, 0x10C40000, 0x12040000, 0x13040000, 0x13060000, 0x15060000,
  # 0x154D0000, 0x154E0000, 0x16140000, 0x16280000, 0x16290000, 0x162A0000,
  # 0x162B0000
  # (the byte array below is correct; the old comment here listed the
  #  Exynos 2400 reference addresses by mistake)
  #
  gSamsungPkgTokenSpaceGuid.PcdGpioCtrlAddr|{ 0x00, 0x00, 0x84, 0x10, 0x00, 0x00, 0xC4, 0x10, 0x00, 0x00, 0x04, 0x12, 0x00, 0x00, 0x04, 0x13, 0x00, 0x00, 0x06, 0x13, 0x00, 0x00, 0x06, 0x15, 0x00, 0x00, 0x4D, 0x15, 0x00, 0x00, 0x4E, 0x15, 0x00, 0x00, 0x14, 0x16, 0x00, 0x00, 0x28, 0x16, 0x00, 0x00, 0x29, 0x16, 0x00, 0x00, 0x2A, 0x16, 0x00, 0x00, 0x2B, 0x16 }

  #
  # RAM Manager
  #
  gSamsungPkgTokenSpaceGuid.PcdHasSecurePgtbl|TRUE
  gSamsungPkgTokenSpaceGuid.PcdHasHarx|TRUE
  gSamsungPkgTokenSpaceGuid.PcdHasDrmPlugin|TRUE

  #
  # SMBIOS
  #
!if $(SOC_TYPE) == 0
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Tensor G4 @ 3.21 GHz"
!elseif $(SOC_TYPE) == 1
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Tensor G4e @ 3.11 GHz"
!endif
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"zumapro"

[LibraryClasses]
  #
  # Memory Libraries
  #
  # Overrides the Samsung implementation, which reads the memory size from
  # NS_IRAM + 0x848. Tensor has no iram node, so that lookup fails and
  # RamManagerDxe asserts. Ours takes the layout from the device tree.
  #
  RamManagerLib|zumaproPkg/Library/RamManagerLib/RamManagerLib.inf

  #
  # Storage Libraries
  #
  # Replaces the null stub SamsungPkg.dsc.inc selects, so UfsDxe has a real
  # board init and the calibration Google publishes for this SoC.
  #
  PlatformUfsLib|zumaproPkg/Library/PlatformUfsLib/PlatformUfsLib.inf

  #
  # Reset Libraries
  #
  # Every reset must be a WARM reset while this port is being brought up.
  # A clean reset makes the bootloader re-initialise DRAM, which erases the
  # debug log in the ramoops pmsg zone. The default ArmPsciResetSystemLib
  # does a clean PSCI SYSTEM_RESET, and a run that got far enough for the
  # boot manager to reset came back with an empty log as a result.
  #
  HwResetSystemLib|zumaproPkg/Library/ResetSystemLib/ResetSystemLib.inf
  ResetSystemLib|zumaproPkg/Library/ResetSystemLib/ResetSystemLib.inf

  #
  # SEC Libraries
  #
  PlatformSecLib|zumaproPkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # IO Libraries
  #
  GpioBankLib|zumaproPkg/Library/GpioBankLib/GpioBankLib.inf

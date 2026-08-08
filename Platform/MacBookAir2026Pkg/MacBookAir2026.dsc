#Mac Mini 2020 UEFI DSC file
#some parts borrowed from WOA-Project/SurfaceDuoPkg
#Disclaimer: probably not the best UEFI dev out there
#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#
#  This program and the accompanying materials
#  are licensed and made available under the terms and conditions of the BSD License
#  which accompanies this distribution.  The full text of the license may be found at
#  http://opensource.org/licenses/bsd-license.php
#
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
#
#

#SPDX-License-Identifier: BSD 2-Clause

#Basic Defines

[Defines]
  PLATFORM_NAME                  = MacBookAir2026
  PLATFORM_GUID                  = 35B1C834-FE1C-4656-BBDB-E5C6B6B3D48B
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacBookAir2026-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacBookAir2026Pkg/MacBookAir2026.fdf
  SECURE_BOOT_ENABLE             = FALSE #disable secure boot for now
  DEFINE T8142_SYSTEM_MEMORY_SIZE = 0x400000000
  #
  # Start J813 bring-up on Apple's native AIC: this is the path already proven
  # to reach the internal UEFI shell. A later Windows profile will switch this
  # to FALSE once m1n1's emulated GICv3 path is stable on T8142.
  #
  AIC_BUILD                      = TRUE
  NETWORK_TLS_ENABLE             = TRUE


[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8142 -DNTASI_J813_PMCCNTR_EMULATION=1
  GCC:*_*_AARCH64_PP_FLAGS = -DNTASI_J813_PMCCNTR_EMULATION=1
  #*_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS


[PcdsFixedAtBuild.common]
  #
  # Do not reset when the MemoryTypeInformation variable changes.
  #
  # DXE Core measures actual per-type memory usage, writes the
  # MemoryTypeInformation UEFI variable, and resets so the next boot can pre-size
  # its memory bins. That is correct on a platform with persistent variables.
  #
  # We have none -- there is no NVRAM or backing storage behind the variable
  # store yet -- so every boot rediscovers the same "change" and resets again:
  #
  #   Memory Type Information settings change.
  #   ...Warm Reset!!!
  #   DXE ResetSystem2: ResetType Warm, Call Depth = 1.
  #   Warm reboot not supported by platform, issuing cold reboot
  #
  # which m1n1 sees as PSCI SYSTEM_RESET (0x84000009). That is the reset loop
  # that looked like a crash. Revisit once variables are actually persistent.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE

  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Air (M5, 2026)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"Mac17,3"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Air (Mac17,3)"
  # J813 (M5) exposes usb-drd0, usb-drd1 and usb-drd3 -- no usb-drd2. m1n1 brings
  # up all three (USB0/USB1/USB3). These PCDs are used as a loop *bound*, so the
  # non-contiguous indexing cannot be expressed exactly; 4 covers 0..3, and the
  # absent usb-drd2 is skipped harmlessly by the node-presence check in
  # AppleDartIoMmuDxe / AppleUsbTypeCBringupDxe. At the previous value of 2,
  # usb-drd3 was unreachable, leaving only one candidate port for boot media.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|4
  # Unread by any code -- declared in AppleSiliconPkg.dec and set here and in the
  # other platform DSCs, but nothing consumes it. Kept consistent for clarity.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Darts|8
  # J813's only keyboard is internal (MTP over dockchannel), and there is no UEFI
  # driver for it yet, so the USB class keyboard path in ConIn can never resolve.
  # Leaving it there makes the console depend on the USB stack, which stalls in
  # UsbRootHubInit at ReadyToBoot before BDS attempts any boot option -- see
  # docs/NEXT-STEPS.md attempt 30. XhciDxe stays registered either way.
  # Set back to TRUE once USB enumeration completes, or once an external USB
  # keyboard is wanted for the menu.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectUsbKeyboardConsole|FALSE
  # THE actual cause of the UsbRootHubInit stall (attempt 31). PostReadyToBoot()
  # calls EfiBootManagerConnectAll() because the boot target is the internal
  # Shell; that binds XhciDxe and hangs before the Shell image ever starts.
  # FALSE gets a Shell prompt over the VUART, from which `connect -r` reproduces
  # the stall interactively. Set back to TRUE once USB enumeration completes.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectAllForInternalShell|FALSE
  # Turn on BootRamdiskHelperDxe. It no-ops unless a RAW section with
  # gAppleSiliconPkgEmbeddedRamdiskGuid is actually in the FV, so this is safe to
  # leave on -- but the FDF currently embeds a 16 MiB FAT16 *test* image, not
  # WinPE. See docs/NEXT-STEPS.md attempt 39.
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  #will be changed later on, default values
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|2560
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|30

[PcdsDynamicDefault.common]
  # #borrowed from SurfaceDuoPkg
  # gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1920
  # gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1080
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|1920
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|1080
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|300
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|50
  # gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|300
  # gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|50

[Components.common]

MacBookAir2026Pkg/AcpiTables/DeviceAcpiTables.inf

!include MacBookAirFamilyPkg/MacBookAirFamily.dsc.inc
!include T8142FamilyPkg/T8142FamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc

AppleSiliconPkg/Drivers/WindowsPmuCompatDxe/WindowsPmuCompatDxe.inf

#
# Built only so its .efi can be copied into the RAM disk image as
# EFI\BOOT\BOOTAA64.EFI -- see tools/make_ramdisk_img.sh. It is deliberately NOT
# added to the FDF, so it never ends up in the firmware volume.
#
# Purpose: roadmap Phase 1.3, "load a PE from FS1". Mounting the RAM disk and
# reading RAMDISK.TXT (done, FW-10) only proves DiskIo/Partition/Fat work.
# Executing an image off it additionally proves the Shell can load and relocate a
# PE from that volume, which is exactly what bootaa64.efi will need.
#
# HelloWorld rather than Shell.efi on purpose: launching a nested Shell would
# re-run FS0:\startup.nsh and recurse. HelloWorld prints one line and exits.
#
MdeModulePkg/Application/HelloWorld/HelloWorld.inf

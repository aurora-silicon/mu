# SPDX-License-Identifier: BSD-2-Clause-Patent
# J873gAP/T8152 native firmware qualification. Windows ACPI is a later profile.
[Defines]
  PLATFORM_NAME = MacMini2026
  PLATFORM_GUID = 3099D880-F883-4EF3-8563-E3524E5F7B29
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/MacMini2026-$(ARCH)
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = DEBUG|RELEASE
  SKUID_IDENTIFIER = DEFAULT
  FLASH_DEFINITION = MacMini2026Pkg/MacMini2026.fdf
  SECURE_BOOT_ENABLE = FALSE
  AIC_BUILD = TRUE
  NETWORK_TLS_ENABLE = TRUE
  DEFINE NTASI_ENABLE_ANS = FALSE
  DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE
  DEFINE MTP_HID_BUILD = FALSE
  DEFINE NTASI_PLATFORM_BOOT_TIMEOUT = 0
  DEFINE NTASI_DEBUG_PRINT_ERROR_LEVEL = 0x8000004F
!ifndef J873_WINDOWS
  DEFINE J873_WINDOWS = FALSE
!endif
  #
  # J804_M1: the J873 native profile adapted for a first RAM-only run on J804
  # (MacBook Pro 14-inch, M6) at native EL2: Null serial with panel
  # checkpoints, ADT-driven RAM reservations and an untrimmed-RAM-top stop,
  # no WFI in the idle path, and a J804 startup.nsh.
  #
!ifndef J804_M1
  DEFINE J804_M1 = FALSE
!endif
!if $(J804_M1) == TRUE
!if $(J873_WINDOWS) == TRUE
!error "J804_M1 builds the native profile only; J873_WINDOWS must be FALSE"
!endif
  # Nothing drains the DockChannel on J804; both SerialPortLib mappings in
  # AppleSiliconPkg.dsc.inc switch to the Null library on this define.
  DEFINE APPLE_SERIAL_PORT_NULL = TRUE
!endif

[BuildOptions.common]
  # Keep the build host's directory layout out of the binaries. Each PE's
  # CodeView record names only its PDB file instead of the absolute PDB path,
  # and, when the build script passes the checkout root as SOURCE_PREFIX_MAP,
  # __FILE__ in ASSERT/DEBUG strings becomes relative to that root.
  # lld still emits a CodeView PDB signature that changes on every link, even
  # with /Brepro, so GenFw -z zeroes the debug directory data (CodeView record
  # and time stamps) in each .efi. Repeated builds in one path then match.
  GCC:*_CLANGPDB_*_DLINK_FLAGS = /PDBALTPATH:%_PDB% /Brepro
  GCC:*_CLANGPDB_*_GENFW_FLAGS = -z
!ifdef SOURCE_PREFIX_MAP
  GCC:*_CLANGPDB_*_CC_FLAGS = -ffile-prefix-map=$(SOURCE_PREFIX_MAP)/=
!endif
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152
!if $(J804_M1) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1
  # PrePi's ModuleEntryPoint.S has J804_M1 stops too.
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1
!endif
!if $(J873_WINDOWS) == TRUE
  # This existing ArmExceptionLib option preserves the full-width EL1 stack
  # for PMU faults. The EL2 sysreg-assist macro is absent in this base revision.
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ873_WINDOWS=1 -DNTASI_J813_PMCCNTR_EMULATION=1
  GCC:*_*_AARCH64_PP_FLAGS = -DNTASI_J813_PMCCNTR_EMULATION=1
!endif

!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc
!include T8152FamilyPkg/T8152FamilyPkg.dsc.inc

[LibraryClasses.common]
  AcpiLib|EmbeddedPkg/Library/AcpiLib/AcpiLib.inf
!if $(J804_M1) == TRUE
  # CpuDxe's idle callback must not execute a bare WFI at native EL2.
  CpuLib|T8152FamilyPkg/Library/J804CpuLib/J804CpuLib.inf

[LibraryClasses.common.DXE_CORE]
  # First DXE panel checkpoint, from inside DxeMain.
  NULL|T8152FamilyPkg/Library/J804DxeCoreCheckpointLib/J804DxeCoreCheckpointLib.inf
!endif

[PcdsFixedAtBuild.common]
  # Enter the built-in shell without depending on the graphical settings UI.
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x83, 0xA5, 0x04, 0x7C, 0x3E, 0x9E, 0x1C, 0x4F, 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Mac mini (M6, 2026)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"Mac18,5"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"Mac mini (J873gAP)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemFamily|"Mac mini"
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectUsbKeyboardConsole|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectAllForInternalShell|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartMmioEnabled|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|FALSE
!if $(J873_WINDOWS) == TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartBase|0x331200000
  gArmTokenSpaceGuid.PcdGicDistributorBase|0xF00000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0xF10000000
!endif
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|1280
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|720
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|32
  gAppleSiliconPkgTokenSpaceGuid.PcdConsoleScaleDivisor|1

[PcdsDynamicDefault.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0

[Components.common]
  MacMini2026Pkg/Applications/J873BootProbe/J873BootProbe.inf
!if $(J804_M1) == TRUE
  T8152FamilyPkg/Drivers/J804CheckpointDxe/J804CheckpointDxe.inf
!endif
!if $(J873_WINDOWS) == TRUE
  MacMini2026Pkg/AcpiTables/J873AcpiTables.inf
  MacMini2026Pkg/Drivers/J873AcpiDxe/J873AcpiDxe.inf
!endif

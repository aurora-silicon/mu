# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# J804 (MacBook Pro 14-inch, M6, T8152) Mu windows profile.
#
# Mu runs at EL1 as the guest of the EL2 carrier (aura's WINDOWS=1
# personality ported to T8152), which emulates a GICv3, serves PSCI over SMC,
# reflects the timers and owns the real reset (NVMe shutdown, then WD1).
# Derived from MacMini2026Pkg's J873 windows profile, with the J804 M1 safety
# changes kept:
#   - Null serial unless the carrier offers its vUART (J804_WINDOWS_VUART);
#   - the RAM-top trim checks and ADT-driven reservations (J804_M1 in C);
#   - an idle loop without WFI until Phase 3(a) measures WFI on T8152;
#   - AIC timer reflection only at EL1 (AppleAicV2Dxe checks CurrentEL);
#   - panel checkpoints.
# The ACPI tables install only at EL1 on cpu6 of a J804 (J804AcpiDxe). This FD
# is for EL1 under the carrier only: with NTASI_AURA_WINDOWS (the default) it
# links AuraArmExceptionLib, which AUDIT-0.8 forbids in a native-EL2 image
# (its HVC forward has no CurrentEL check; at EL2 the HVC traps to EL2
# itself). Start natively at EL2 only the M1 FD (build-j804-m1.sh), never this
# one. An NTASI_AURA_WINDOWS=FALSE build drops the forwarding; it is not
# tested at EL2 either.
#
[Defines]
  PLATFORM_NAME = MacBookPro2026
  PLATFORM_GUID = 06C8FF88-D4B4-4DE1-9F1E-6839C04DA9ED
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/MacBookPro2026-$(ARCH)
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = DEBUG|RELEASE
  SKUID_IDENTIFIER = DEFAULT
  FLASH_DEFINITION = MacBookPro2026Pkg/MacBookPro2026.fdf
  SECURE_BOOT_ENABLE = FALSE
  AIC_BUILD = TRUE
  NETWORK_TLS_ENABLE = TRUE
  # No Mu storage driver: Mu never touches the internal SSD.
  DEFINE NTASI_ENABLE_ANS = FALSE
  DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE
  DEFINE MTP_HID_BUILD = FALSE
  DEFINE NTASI_PLATFORM_BOOT_TIMEOUT = 0
  DEFINE NTASI_DEBUG_PRINT_ERROR_LEVEL = 0x8000004F
  #
  # Aura carrier firmware pieces (windows-mu-handoff, AUDIT-0.8 OK):
  # AuraArmExceptionLib (sysreg forwarding by HVC) and
  # AuraVarPolicyFoundationDxe (phase indicators before the runtime switch).
  #
!ifndef NTASI_AURA_WINDOWS
  DEFINE NTASI_AURA_WINDOWS = TRUE
!endif
  #
  # J804_WINDOWS_ANS: emits ANS0 (NTAS2004) at all. FALSE for M2/M3 (no
  #   storage); the default DSDT then names no ANS, PMGR or mailbox page.
  # J804_WINDOWS_SMC: SMC0 (NTAS2032) _STA. FALSE: the T8152 carrier's stage
  #   2 is an allowlist (guest RAM, carrier pages, AIC) and its ADT denylist
  #   holds every /arm-io smc* node, so a bound SMC driver would only take
  #   stage-2 aborts. Enable only once the carrier allowlists the SMC pages.
  # J804_WINDOWS_VUART: the carrier offers a vUART at the uart0 address; adds
  #   the Samsung UART serial library, \_SB.COM0 and DBG2. FALSE = Null serial.
  # J804_WINDOWS_AHCI: D1, the carrier-emulated AHCI disk (J804Windows.h,
  #   windows-m6/D1-AHCI-DESIGN.md): AHC0 in the DSDT, the FD marker block at
  #   0x110, and the Mu AHCI stack (NonDiscoverablePciDeviceDxe,
  #   SataControllerDxe, AtaAtapiPassThru, AtaBusDxe, J804AhciDxe), which
  #   acts only when the launcher's /chosen handshake is present. FALSE = the
  #   M2/M3 image, unchanged.
  # J804_WINDOWS_USB: D4, usb-drd1's xHCI passed through by the carrier
  #   (J804Windows.h, windows-m6/D4-USB-DESIGN.md): the \_SB.XHC1 SSDT
  #   (J804Xhc1Ssdt), J804XhciAcpiDxe, which installs it only when the
  #   launcher's /chosen USB handshake is present, and the FD marker block at
  #   0x120. No Mu USB driver: Mu never touches the controller. Needs
  #   J804_WINDOWS_AHCI (the D1 desktop route). FALSE = the AHCI image,
  #   unchanged.
  # J804_WINDOWS_GPU: G0, the carrier-admitted G18 GPU (J804Windows.h,
  #   AppleSiliconPkg J804GpuHandoff.h, windows-m6/G0-CARRIER-CONTRACT.md):
  #   PrePi lowers the RAM top to the GPU pool and reserves the pool when the
  #   launcher's /chosen GPU handshake names a valid carrier handoff page;
  #   the \_SB.GPU0 SSDT template (J804Gpu0Ssdt) and J804GpuAcpiDxe, which
  #   patches it from that page and installs it only then. Mu never touches
  #   the GPU. Needs J804_WINDOWS_AHCI (the D1 desktop route). FALSE = the
  #   AHCI or USB image, unchanged.
  # J804_WINDOWS_INPUT: I0, the carrier's keyboard and pointer mailbox
  #   (J804Windows.h, windows-m6/INPUT-DESIGN.md): the \_SB.INP0 SSDT
  #   (J804Inp0Ssdt), J804InputAcpiDxe, which installs it only when the
  #   launcher's /chosen input handshake is present, and the FD marker block
  #   at 0x140. No Mu input driver and no C or ASL define: Mu never touches
  #   the page. Needs J804_WINDOWS_AHCI (the D1 desktop route). FALSE = the
  #   AHCI, USB or GPU image, unchanged.
  #
!ifndef J804_WINDOWS_ANS
  DEFINE J804_WINDOWS_ANS = FALSE
!endif
!ifndef J804_WINDOWS_SMC
  DEFINE J804_WINDOWS_SMC = FALSE
!endif
!ifndef J804_WINDOWS_VUART
  DEFINE J804_WINDOWS_VUART = FALSE
!endif
!ifndef J804_WINDOWS_AHCI
  DEFINE J804_WINDOWS_AHCI = FALSE
!endif
!ifndef J804_WINDOWS_USB
  DEFINE J804_WINDOWS_USB = FALSE
!endif
!ifndef J804_WINDOWS_GPU
  DEFINE J804_WINDOWS_GPU = FALSE
!endif
!ifndef J804_WINDOWS_INPUT
  DEFINE J804_WINDOWS_INPUT = FALSE
!endif
!if $(J804_WINDOWS_USB) == TRUE
!if $(J804_WINDOWS_AHCI) == FALSE
!error "J804_WINDOWS_USB builds on the D1 AHCI profile: set J804_WINDOWS_AHCI = TRUE as well"
!endif
!endif
!if $(J804_WINDOWS_GPU) == TRUE
!if $(J804_WINDOWS_AHCI) == FALSE
!error "J804_WINDOWS_GPU builds on the D1 AHCI profile: set J804_WINDOWS_AHCI = TRUE as well"
!endif
!endif
!if $(J804_WINDOWS_INPUT) == TRUE
!if $(J804_WINDOWS_AHCI) == FALSE
!error "J804_WINDOWS_INPUT builds on the D1 AHCI profile: set J804_WINDOWS_AHCI = TRUE as well"
!endif
!endif
!if $(SECURE_BOOT_ENABLE) == TRUE
!error "The J804 windows profile builds with SECURE_BOOT_ENABLE = FALSE"
!endif
!if $(J804_WINDOWS_VUART) == FALSE
  # Nothing drains the DockChannel on J804; both SerialPortLib mappings in
  # AppleSiliconPkg.dsc.inc switch to the Null library on this define.
  DEFINE APPLE_SERIAL_PORT_NULL = TRUE
!endif
!if $(J804_WINDOWS_ANS) == TRUE
  DEFINE J804_ASL_ANS = 1
!else
  DEFINE J804_ASL_ANS = 0
!endif
!if $(J804_WINDOWS_SMC) == TRUE
  DEFINE J804_ASL_SMC = 1
!else
  DEFINE J804_ASL_SMC = 0
!endif
!if $(J804_WINDOWS_VUART) == TRUE
  DEFINE J804_ASL_VUART = 1
!else
  DEFINE J804_ASL_VUART = 0
!endif
!if $(J804_WINDOWS_AHCI) == TRUE
  DEFINE J804_ASL_AHCI = 1
!else
  DEFINE J804_ASL_AHCI = 0
!endif

[BuildOptions.common]
  # Keep the build host's directory layout out of the binaries (as the J804
  # M1 profile): PDB names only, GenFw -z, and __FILE__ relative to the
  # checkout root when the build script passes SOURCE_PREFIX_MAP.
  GCC:*_CLANGPDB_*_DLINK_FLAGS = /PDBALTPATH:%_PDB% /Brepro
  GCC:*_CLANGPDB_*_GENFW_FLAGS = -z
!ifdef SOURCE_PREFIX_MAP
  GCC:*_CLANGPDB_*_CC_FLAGS = -ffile-prefix-map=$(SOURCE_PREFIX_MAP)/=
!endif
  # J804_M1 in C selects the J804 memory and early-stop safety (PrePi RAM-top
  # checks, ADT reservations, boot hand-off check); J804_WINDOWS the windows
  # profile switches (BOOTAA64 ramdisk path, WD1 mapping, build marker).
  # J804_WINDOWS_AHCI adds -DJ804_WINDOWS_AHCI=1, J804_WINDOWS_USB
  # -DJ804_WINDOWS_USB=1 and J804_WINDOWS_GPU -DJ804_WINDOWS_GPU=1; without
  # them the flags are exactly those of the M2/M3 builds (and without
  # J804_WINDOWS_GPU exactly those of the AHCI and USB builds).
!if $(J804_WINDOWS_GPU) == TRUE
!if $(J804_WINDOWS_USB) == TRUE
!if $(J804_WINDOWS_VUART) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1 -DJ804_WINDOWS_GPU=1
!else
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1 -DJ804_WINDOWS_GPU=1
!endif
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1 -DJ804_WINDOWS_GPU=1
!else
!if $(J804_WINDOWS_VUART) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_GPU=1
!else
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_GPU=1
!endif
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_GPU=1
!endif
!else
!if $(J804_WINDOWS_USB) == TRUE
!if $(J804_WINDOWS_VUART) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1
!else
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1
!endif
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1 -DJ804_WINDOWS_USB=1
!else
!if $(J804_WINDOWS_AHCI) == TRUE
!if $(J804_WINDOWS_VUART) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1 -DJ804_WINDOWS_AHCI=1
!else
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1
!endif
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_AHCI=1
!else
!if $(J804_WINDOWS_VUART) == TRUE
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1
!else
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8152 -DJ804_M1=1 -DJ804_WINDOWS=1
!endif
  GCC:*_*_AARCH64_PP_FLAGS = -DJ804_M1=1 -DJ804_WINDOWS=1
!endif
!endif
!endif
  GCC:*_*_AARCH64_ASLPP_FLAGS = -DJ804_WINDOWS_ANS=$(J804_ASL_ANS) -DJ804_WINDOWS_SMC=$(J804_ASL_SMC) -DJ804_WINDOWS_VUART=$(J804_ASL_VUART) -DJ804_WINDOWS_AHCI=$(J804_ASL_AHCI)

!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc
!include T8152FamilyPkg/T8152FamilyPkg.dsc.inc

[LibraryClasses.common]
  AcpiLib|EmbeddedPkg/Library/AcpiLib/AcpiLib.inf
  # CpuDxe's idle callback must not execute a bare WFI (PLAN Phase 3(a)).
  CpuLib|T8152FamilyPkg/Library/J804CpuLib/J804CpuLib.inf
  # PSCI SYSTEM_RESET at EL1 (the carrier resets through WD1 after an NVMe
  # shutdown), WD1 at EL2, shutdown parks.
  HwResetSystemLib|MacBookPro2026Pkg/Library/J804Wd1ResetSystemLib/J804Wd1ResetSystemLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  HwResetSystemLib|MacBookPro2026Pkg/Library/J804Wd1ResetSystemLib/J804Wd1ResetSystemLib.inf

[LibraryClasses.common.DXE_CORE]
  # First DXE panel checkpoint, from inside DxeMain.
  NULL|T8152FamilyPkg/Library/J804DxeCoreCheckpointLib/J804DxeCoreCheckpointLib.inf

!if $(NTASI_AURA_WINDOWS) == TRUE
[LibraryClasses.AARCH64]
  CpuExceptionHandlerLib|AppleSiliconPkg/Library/AuraArmExceptionLib/AuraArmExceptionLib.inf
!endif

[PcdsFixedAtBuild.common]
  # Enter the built-in shell without depending on the graphical settings UI.
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x83, 0xA5, 0x04, 0x7C, 0x3E, 0x9E, 0x1C, 0x4F, 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  # RAM-only variables: nothing can reach the QSPI NVRAM.
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  # SMBIOS fallbacks only: model, product name, board name and both serial
  # numbers are read from the ADT at run time (PcdSmbiosLiftAdtIdentity).
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Pro (14-inch, M6)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"Mac18,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"J804"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemFamily|"MacBook Pro"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosLiftAdtIdentity|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectUsbKeyboardConsole|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectAllForInternalShell|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
!if $(J804_WINDOWS_VUART) == TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartMmioEnabled|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartBase|0x331200000
!else
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartMmioEnabled|FALSE
!endif
  # The carrier's emulated GICv3 (J804Windows.h; MADT.aslc asserts these).
  gArmTokenSpaceGuid.PcdGicDistributorBase|0xF00000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0xF10000000
!if $(J804_WINDOWS_AHCI) == TRUE
  # The carrier's IDENTIFY data reports no SMART feature set (word 82 bit 0).
  gEfiMdeModulePkgTokenSpaceGuid.PcdAtaSmartEnable|FALSE
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
  MacBookPro2026Pkg/Applications/J804BootProbe/J804BootProbe.inf
  T8152FamilyPkg/Drivers/J804CheckpointDxe/J804CheckpointDxe.inf
  MacBookPro2026Pkg/AcpiTables/J804AcpiTables.inf
  MacBookPro2026Pkg/Drivers/J804AcpiDxe/J804AcpiDxe.inf
!if $(J804_WINDOWS_VUART) == TRUE
  MacBookPro2026Pkg/AcpiTables/J804Dbg2Table.inf
!endif
!if $(J804_WINDOWS_AHCI) == TRUE
  # NonDiscoverablePciDeviceDxe is already a component (AppleSiliconPkg.dsc.inc).
  MdeModulePkg/Bus/Pci/SataControllerDxe/SataControllerDxe.inf
  MdeModulePkg/Bus/Ata/AtaAtapiPassThru/AtaAtapiPassThru.inf
  MdeModulePkg/Bus/Ata/AtaBusDxe/AtaBusDxe.inf
  MacBookPro2026Pkg/Drivers/J804AhciDxe/J804AhciDxe.inf
!endif
!if $(J804_WINDOWS_USB) == TRUE
  MacBookPro2026Pkg/AcpiTables/J804Xhc1Ssdt.inf
  MacBookPro2026Pkg/Drivers/J804XhciAcpiDxe/J804XhciAcpiDxe.inf
!endif
!if $(J804_WINDOWS_GPU) == TRUE
  MacBookPro2026Pkg/AcpiTables/J804Gpu0Ssdt.inf
  MacBookPro2026Pkg/Drivers/J804GpuAcpiDxe/J804GpuAcpiDxe.inf
!endif
!if $(J804_WINDOWS_INPUT) == TRUE
  MacBookPro2026Pkg/AcpiTables/J804Inp0Ssdt.inf
  MacBookPro2026Pkg/Drivers/J804InputAcpiDxe/J804InputAcpiDxe.inf
!endif

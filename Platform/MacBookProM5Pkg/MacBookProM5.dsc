# SPDX-License-Identifier: BSD-2-Clause-Patent
# T6050 / J714s (Apple M5 Pro, MacBook Pro) Mu platform.
#
# Port of MacMini2026Pkg (j873 / T8152), the hardware-proven Apple-silicon Windows
# firmware. This platform targets an EL1 guest of the x1n1 hypervisor in QEMU
# (see WINDOWS_HANDOFF.md), eventually loading Windows PE.
#
#   J714_WINDOWS == FALSE : native/shell qualification profile.
#   J714_WINDOWS == TRUE  : Windows profile - GIC MADT + PSCI(HVC) ACPI and the
#                           appended WinPE RAM disk. It also defines J873_WINDOWS
#                           to reuse the already-proven shared Windows behaviors
#                           (BootRamdiskHelperDxe's \EFI\BOOT\BOOTAA64.EFI path and
#                           MuVarPolicyFoundationDxe's EBS phase-indicator TPL fix).
[Defines]
  PLATFORM_NAME = MacBookProM5
  PLATFORM_GUID = 1d157446-0d6b-4330-b394-7a15731843ed
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/MacBookProM5-$(ARCH)
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = DEBUG|RELEASE
  SKUID_IDENTIFIER = DEFAULT
  FLASH_DEFINITION = MacBookProM5Pkg/MacBookProM5.fdf
  SECURE_BOOT_ENABLE = FALSE
  AIC_BUILD = TRUE
  NETWORK_TLS_ENABLE = TRUE
  DEFINE NTASI_ENABLE_ANS = FALSE
  DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE
  DEFINE MTP_HID_BUILD = FALSE
  DEFINE NTASI_PLATFORM_BOOT_TIMEOUT = 0
  DEFINE NTASI_DEBUG_PRINT_ERROR_LEVEL = 0x8000004F
  # QEMU darwin machine gives the T6050 guest 8 GiB at the 1 TiB mark. SEC patches
  # the real size from boot_args at runtime; this is the fixed-build fallback.
  DEFINE T6050_SYSTEM_MEMORY_SIZE = 0x200000000
  # x1n1 exposes the Samsung console UART to the guest at this IPA (-> PA 0x505200000).
  DEFINE T6050_UART_BASE = 0x60000000
!ifndef J714_WINDOWS
  DEFINE J714_WINDOWS = FALSE
!endif
!ifndef J714_NATIVE_FIQ
  DEFINE J714_NATIVE_FIQ = FALSE
!endif
!ifndef J714_BGR_DIAGNOSTIC
  DEFINE J714_BGR_DIAGNOSTIC = FALSE
!endif
!if $(J714_BGR_DIAGNOSTIC) == TRUE
  DEFINE J714_BGR_DIAGNOSTIC_VALUE = 1
!else
  DEFINE J714_BGR_DIAGNOSTIC_VALUE = 0
!endif
# WinPE needs a bank larger than the 512 MiB memalign bank AND a conventional
# LOW guest-physical map so Windows bootmgr's fixed low-address allocations
# (0x102000, image-reloc 0x10000000+) land in RAM. When J714_LARGE_BANK is TRUE,
# x1n1 maps a low guest-IPA bank (phys_base 0) that stage-2 remaps to the real
# high host DRAM (SPTM_GUEST_LARGE_BASE = 0x10080000000). boot_args/ADT sit at
# NONZERO low offsets (0x8000/0x20000) so PcdBootArgsPointer is never NULL
# (Mu's AcpiPlatformDxe rejects a NULL BootArgs). These MUST match x1n1's
# SPTM_GUEST_BOOTARGS_OFF/SPTM_GUEST_ADT_OFF and the FD IPA. Run x1n1 with a big
# --ram-size (<=1.5 GiB, below the 0x60000000 UART/IO window), the image loaded
# at host PA 0x10081000000, and dispatched at guest FD IPA 0x1000000.
!ifndef J714_HARDWARE
  DEFINE J714_HARDWARE = FALSE
!endif
# Opt-in debug profile: publish the last CPU's GICC entry disabled so Windows
# never starts it; x1n1 keeps that core in host context as a debug monitor.
!ifndef J714_SMC
  DEFINE J714_SMC = FALSE
!endif
!if $(J714_SMC) == TRUE
  DEFINE J714_SMC_VALUE = 1
!else
  DEFINE J714_SMC_VALUE = 0
!endif
!ifndef J714_FULL_RAM
  DEFINE J714_FULL_RAM = FALSE
!endif
!ifndef J714_DCP
  DEFINE J714_DCP = FALSE
!endif
!if $(J714_DCP) == TRUE
  DEFINE J714_DCP_VALUE = 1
!else
  DEFINE J714_DCP_VALUE = 0
!endif
!if $(J714_FULL_RAM) == TRUE
  DEFINE J714_FULL_RAM_VALUE = 1
!else
  DEFINE J714_FULL_RAM_VALUE = 0
!endif
!ifndef J714_KBL
  DEFINE J714_KBL = FALSE
!endif
!ifndef J714_UEFI_MTP
  DEFINE J714_UEFI_MTP = FALSE
!endif
!if $(J714_UEFI_MTP) == TRUE
  DEFINE J714_UEFI_MTP_VALUE = 1
!else
  DEFINE J714_UEFI_MTP_VALUE = 0
!endif
!ifndef J714_UEFI_MENU
  DEFINE J714_UEFI_MENU = FALSE
!endif
!if $(J714_UEFI_MENU) == TRUE
  DEFINE J714_UEFI_MENU_VALUE = 1
!else
  DEFINE J714_UEFI_MENU_VALUE = 0
!endif
!ifndef J714_UEFI_KBL
  DEFINE J714_UEFI_KBL = FALSE
!endif
!ifndef J714_UEFI_ANS
  DEFINE J714_UEFI_ANS = FALSE
!endif
!if $(J714_KBL) == TRUE
  DEFINE J714_KBL_VALUE = 1
!else
  DEFINE J714_KBL_VALUE = 0
!endif
!ifndef J714_N1
  DEFINE J714_N1 = FALSE
!endif
!if $(J714_N1) == TRUE
  DEFINE J714_N1_VALUE = 1
!else
  DEFINE J714_N1_VALUE = 0
!endif
!ifndef J714_USB_HOSTS
  DEFINE J714_USB_HOSTS = FALSE
!endif
!if $(J714_USB_HOSTS) == TRUE
  DEFINE J714_USB_HOSTS_VALUE = 1
!else
  DEFINE J714_USB_HOSTS_VALUE = 0
!endif
!ifndef J714_USB3
  DEFINE J714_USB3 = FALSE
!endif
!if $(J714_USB3) == TRUE
  DEFINE J714_USB3_VALUE = 1
!else
  DEFINE J714_USB3_VALUE = 0
!endif
!ifndef J714_USB_INSTALLER
  DEFINE J714_USB_INSTALLER = FALSE
!endif
!if $(J714_USB_INSTALLER) == TRUE
  DEFINE J714_USB_INSTALLER_VALUE = 1
!else
  DEFINE J714_USB_INSTALLER_VALUE = 0
!endif
!ifndef J714_NVME
  DEFINE J714_NVME = FALSE
!endif
!if $(J714_NVME) == TRUE
  DEFINE J714_NVME_VALUE = 1
!else
  DEFINE J714_NVME_VALUE = 0
!endif
!ifndef J714_MONITOR_CPU
  DEFINE J714_MONITOR_CPU = FALSE
!endif
!if $(J714_MONITOR_CPU) == TRUE
  DEFINE J714_MONITOR_CPU_VALUE = 1
!else
  DEFINE J714_MONITOR_CPU_VALUE = 0
!endif
!if $(J714_HARDWARE) == TRUE
  DEFINE J714_HARDWARE_VALUE = 1
!else
  DEFINE J714_HARDWARE_VALUE = 0
!endif

!ifndef J714_LARGE_BANK
  DEFINE J714_LARGE_BANK = FALSE
!endif
!if $(J714_LARGE_BANK) == TRUE
  DEFINE T6050_BOOTARGS_PTR = 0x8000
  DEFINE T6050_ADT_PTR      = 0x20000
  DEFINE T6050_STACK_BASE   = 0x1000000
!else
  DEFINE T6050_BOOTARGS_PTR = 0x1000a000000
  DEFINE T6050_ADT_PTR      = 0x1000a004000
  DEFINE T6050_STACK_BASE   = 0x1000b000000
!endif

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=6050
!if $(J714_WINDOWS) == TRUE
  # SILICON_PLATFORM=6050 selects the Samsung MMIO UART path (not 8152/DockChannel).
  # NTASI_J813_PMCCNTR_EMULATION preserves the full-width EL1 stack for PMU faults
  # (existing ArmExceptionLib option). J873_WINDOWS turns on the shared Windows
  # behaviors described above.
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=6050 -DJ714_WINDOWS=1 -DJ714_HARDWARE=$(J714_HARDWARE_VALUE) -DJ873_WINDOWS=1 -DNTASI_J813_PMCCNTR_EMULATION=1 -DJ714_BGR_DIAGNOSTIC=$(J714_BGR_DIAGNOSTIC_VALUE) -DJ714_USB3=$(J714_USB3_VALUE) -DJ714_USB_HOSTS=$(J714_USB_HOSTS_VALUE) -DJ714_KBL=$(J714_KBL_VALUE) -DJ714_N1=$(J714_N1_VALUE) -DJ714_FULL_RAM=$(J714_FULL_RAM_VALUE) -DJ714_USB_INSTALLER=$(J714_USB_INSTALLER_VALUE) -DJ714_UEFI_MENU=$(J714_UEFI_MENU_VALUE) -DJ714_UEFI_MTP=$(J714_UEFI_MTP_VALUE)
  GCC:*_*_AARCH64_PP_FLAGS = -DNTASI_J813_PMCCNTR_EMULATION=1
  # .aslc uses ASLCC, independently of the DXE driver's CC flags. Both
  # consumers must select the same hardware/QEMU CPU-affinity table.
  GCC:*_*_AARCH64_ASLCC_FLAGS = -DJ714_HARDWARE=$(J714_HARDWARE_VALUE) -DT6050_J714S_RESERVE_LAST_CPU=$(J714_MONITOR_CPU_VALUE) -DJ714_NVME=$(J714_NVME_VALUE) -DJ714_SMC=$(J714_SMC_VALUE) -DJ714_USB3=$(J714_USB3_VALUE) -DJ714_USB_HOSTS=$(J714_USB_HOSTS_VALUE) -DJ714_KBL=$(J714_KBL_VALUE) -DJ714_N1=$(J714_N1_VALUE) -DJ714_DCP=$(J714_DCP_VALUE)
  GCC:*_*_AARCH64_ASLPP_FLAGS = -DJ714_NVME=$(J714_NVME_VALUE) -DJ714_SMC=$(J714_SMC_VALUE) -DJ714_USB3=$(J714_USB3_VALUE) -DJ714_USB_HOSTS=$(J714_USB_HOSTS_VALUE) -DJ714_KBL=$(J714_KBL_VALUE) -DJ714_N1=$(J714_N1_VALUE) -DJ714_DCP=$(J714_DCP_VALUE)
!endif

!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc
!include T6050FamilyPkg/T6050FamilyPkg.dsc.inc

[LibraryClasses.common]
  J714MtpKeyboardLib|MacBookProM5Pkg/Drivers/J714MtpKeyboardDxe/J714MtpKeyboardLib.inf
  AcpiLib|EmbeddedPkg/Library/AcpiLib/AcpiLib.inf

[PcdsFixedAtBuild.common]
!if $(J714_UEFI_MENU) == TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x6b, 0x41, 0x47, 0x3f, 0x40, 0x71, 0xb0, 0x4b, 0xa0, 0x3c, 0x56, 0x41, 0x35, 0x21, 0x00, 0x01 }
!else
  # Enter the built-in shell without depending on the graphical settings UI.
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x83, 0xA5, 0x04, 0x7C, 0x3E, 0x9E, 0x1C, 0x4F, 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
!endif
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Pro (M5 Pro, 2026)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"Mac17,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Pro (J714s)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemFamily|"MacBook Pro"
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectUsbKeyboardConsole|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectAllForInternalShell|FALSE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartMmioEnabled|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|FALSE
!if $(J714_WINDOWS) == TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  gArmTokenSpaceGuid.PcdGicDistributorBase|0xF00000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0xF10000000
!endif
  # Nominal geometry; SEC patches the real geometry from boot_args (SimpleFbDxe).
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
!if $(J714_UEFI_ANS) == TRUE
  MacBookProM5Pkg/Drivers/J714AnsDxe/J714AnsDxe.inf
!endif
!if $(J714_UEFI_KBL) == TRUE
  MacBookProM5Pkg/Drivers/J714KeyboardBacklightDxe/J714KeyboardBacklightDxe.inf
!endif
  MacBookProM5Pkg/Applications/J714BootProbe/J714BootProbe.inf
!if $(J714_UEFI_MENU) == TRUE
  MacBookProM5Pkg/Applications/J714BootMenu/J714BootMenu.inf
!endif
!if $(J714_WINDOWS) == TRUE
!if $(J714_N1) == TRUE
  MacBookProM5Pkg/AcpiTables/J714N1AcpiTables.inf
!else
  MacBookProM5Pkg/AcpiTables/J714AcpiTables.inf
!endif
  MacBookProM5Pkg/Drivers/J714AcpiDxe/J714AcpiDxe.inf
!if $(J714_NATIVE_FIQ) == TRUE
  MacBookProM5Pkg/Drivers/J714NativeFiqDxe/J714NativeFiqDxe.inf
!endif
!endif

!if $(J714_USB3) == TRUE
  MacBookProM5Pkg/Drivers/J714Usb3Dxe/J714Usb3Dxe.inf
  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
!endif

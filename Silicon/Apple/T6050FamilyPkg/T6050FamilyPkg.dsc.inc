# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# T6050 (Apple M5 Pro / J714s) SoC configuration.
#
# Port of T8152FamilyPkg.dsc.inc (Apple M6 / J873, the hardware-proven Windows
# template). Every address here is taken from the live J714s ADT fixture or the
# x1n1 guest ABI contract (WINDOWS_HANDOFF.md), NOT invented.
#
# Unlike T8152 (which drives a DockChannel UART and hardcodes ~23 GiB of RAM),
# T6050 boots as an EL1 guest of the x1n1 hypervisor in QEMU:
#   - RAM base 0x10000000000, default size 8 GiB (QEMU darwin machine).
#   - x1n1 does NOT identity-map all 8 GiB: its 0x1512 dispatch maps a BOUNDED
#     boot bank. The QEMU-verified bank (WINDOWS_HANDOFF "Measured results") is
#     phys_base 0x1000a000000, mem_size 0x20000000 (512 MiB), with the FD loaded
#     at 0x1000b000000 and x0=boot_args, x1=FD base. Every fixed Mu pointer below
#     therefore lives INSIDE that bank. T8152's 0x10220000000 / 0x10300000000
#     values lie outside it and would fault. These MUST stay in agreement with
#     the x1n1 0x1512 dispatch (see WINDOWS_HANDOFF.md); if x1n1 moves the bank
#     base, PcdBootArgsPointer must move with it (it equals the bank base).
#   - The console UART is the Samsung/s5l MMIO UART, exposed by x1n1 at guest
#     IPA 0x60000000 (mapped to real PA 0x505200000).

#
# Overridable defaults so this .inc can be included standalone. The platform
# DSC may DEFINE these before the !include.
#
!ifndef T6050_SYSTEM_MEMORY_SIZE
  DEFINE T6050_SYSTEM_MEMORY_SIZE = 0x200000000   # QEMU: 8 GiB
!endif
!ifndef T6050_UART_BASE
  DEFINE T6050_UART_BASE = 0x60000000             # x1n1 guest console IPA
!endif
#
# Boot-bank pointers. Default = the 512 MiB memalign bank base 0x1000a000000
# (WINDOWS_HANDOFF "Measured results"). A WinPE-sized bank uses x1n1's bounded
# above-heap large bank (SPTM_GUEST_LARGE_BASE = 0x10080000000); the platform DSC
# overrides these when BLD_*_J714_LARGE_BANK=TRUE. PcdBootArgsPointer MUST equal
# the x1n1 bank base (EarlySetup copies boot_args/ADT to these fixed addresses).
#
!ifndef T6050_BOOTARGS_PTR
  DEFINE T6050_BOOTARGS_PTR = 0x1000a000000
!endif
!ifndef T6050_ADT_PTR
  DEFINE T6050_ADT_PTR = 0x1000a004000
!endif
!ifndef T6050_STACK_BASE
  DEFINE T6050_STACK_BASE = 0x1000b000000
!endif

[PcdsPatchableInModule.common]
  #
  # Patched at runtime by SEC (EarlySetup) from boot_args (phys_base/mem_size).
  # DRAM on T6050 starts at the 1 TiB mark, like T8142/T602X/T600X.
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x10000000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|$(T6050_SYSTEM_MEMORY_SIZE)
  gArmTokenSpaceGuid.PcdFdBaseAddress|0
  gArmTokenSpaceGuid.PcdFvBaseAddress|0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferAddress|0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferSize|0

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|19
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|17
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|20
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|18

  #
  # Boot args / ADT copy destinations and the SEC stack base. These are
  # PcdsFixedAtBuild, so a value outside x1n1's bounded boot bank is fatal.
  #
  # Layout inside the QEMU-verified x1n1 boot bank [0x1000a000000, 0x1002a000000):
  #   0x1000a000000  boot_args : x1n1 places boot_args here and passes x0 = this,
  #                              so PcdBootArgsPointer == the bank base (phys_base).
  #                              SEC's self-copy is harmless; reserve 0x4000.
  #   0x1000a004000  ADT copy dest (reserve 0x200000 = 2 MiB; the J714s ADT is
  #                              ~0xcac94 bytes, well under 2 MiB). Ends
  #                              0x1000a204000, safely below the FD.
  #   0x1000b000000  FD load PA (x1n1 --uefi-pa) and nominal SEC stack base. The
  #                              primary stack is actually computed at the top of
  #                              the bank by the SEC trampoline, so this fallback
  #                              value is unused for the boot CPU (mirrors T8152's
  #                              stack-base == FD-base relationship).
  # The FD (30 MiB) and its appended WinPE RAM disk grow upward from
  # 0x1000b000000; x1n1 must size the bank large enough for the WinPE ramdisk.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdBootArgsPointer|$(T6050_BOOTARGS_PTR)
  gAppleSiliconPkgTokenSpaceGuid.PcdAdtPointer|$(T6050_ADT_PTR)
  gAppleSiliconPkgTokenSpaceGuid.PcdSecPhaseStackBase|$(T6050_STACK_BASE)

  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x6050
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuIdentifier|"T6050"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuModel|"Apple M5 Pro"
  gAppleSiliconPkgTokenSpaceGuid.PcdCoreCount|18
  gAppleSiliconPkgTokenSpaceGuid.PcdClusterCount|3

  #
  # Console UART. T6050 uart0 is a Samsung/s5l "uart-1,samsung" MMIO UART at real
  # PA 0x505200000; the AppleUartSerialPortLib Samsung path is selected because
  # SILICON_PLATFORM != 8152 (set to 6050 in the platform DSC). Under x1n1 the
  # guest sees it at IPA 0x60000000 (see T6050FamilyVirtualMemoryMapDefines.h).
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartBase|$(T6050_UART_BASE)

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00006050
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|300
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|150
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|1000
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|800
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|10
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderData|0
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|42

[LibraryClasses.common]
  MemoryInitPeiLib|T6050FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf
  MsPlatformDevicesLib|T6050FamilyPkg/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf

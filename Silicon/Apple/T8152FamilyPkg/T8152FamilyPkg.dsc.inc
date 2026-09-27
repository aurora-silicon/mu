# SPDX-License-Identifier: BSD-2-Clause-Patent
# Native J873 firmware profile. RAM/framebuffer replaced from boot_args by SEC.
[PcdsPatchableInModule.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x10003a10000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x5db5f0000
  gArmTokenSpaceGuid.PcdFdBaseAddress|0
  gArmTokenSpaceGuid.PcdFvBaseAddress|0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferAddress|0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferSize|0

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|19
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|17
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|20
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|18
  # The launcher must reserve and verify these destinations before entry.
  gAppleSiliconPkgTokenSpaceGuid.PcdBootArgsPointer|0x10220000000
  gAppleSiliconPkgTokenSpaceGuid.PcdAdtPointer|0x10220004000
  gAppleSiliconPkgTokenSpaceGuid.PcdSecPhaseStackBase|0x10300000000
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x8152
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuIdentifier|"T8152"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuModel|"Apple M6"
  gAppleSiliconPkgTokenSpaceGuid.PcdCoreCount|12
  gAppleSiliconPkgTokenSpaceGuid.PcdClusterCount|2
  # Serial library selects the DockChannel layout for this platform.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartBase|0x30812c000
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00008152
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|300
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|150
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|1000
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|800
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|10
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderData|0
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|42

[LibraryClasses.common]
  MemoryInitPeiLib|T8152FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf
  MsPlatformDevicesLib|T8152FamilyPkg/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf

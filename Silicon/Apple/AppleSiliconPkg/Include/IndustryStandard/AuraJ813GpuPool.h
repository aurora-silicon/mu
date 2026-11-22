/** @file
  J813 G17 local-memory pool: the PEI -> DXE handoff.

  T8142 MemoryPeim carves this range out of guest RAM as
  EFI_RESOURCE_MEMORY_RESERVED (EfiReservedMemoryType in the map Windows is
  given) and describes it in one GUID HOB; AuraG17GpuDevice.c publishes it as
  the 17th QWordMemory resource of AURA8171 (GPUP.asl).  It exists only in
  images built with PcdAppleJ813GpuLocalPool when the unified display key is
  on.  See Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/
  FAILURE-MODES-J813-GPU-POOL.md.

  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/
#ifndef AURA_J813_GPU_POOL_H_
#define AURA_J813_GPU_POOL_H_

#define AURA_J813_GPU_POOL_HOB_GUID \
  { 0x7e10d7ec, 0x9434, 0x42ac, { 0x91, 0x3f, 0xe8, 0x79, 0xc9, 0x2f, 0x51, 0xfa } }

#define AURA_J813_GPU_POOL_SIGNATURE  SIGNATURE_32 ('A', 'G', 'L', 'P')
#define AURA_J813_GPU_POOL_VERSION    1

//
// 1 GiB (SHARED-SURFACES.md ruling 2).  Both ends on 32 MiB: the level-2
// block of a 16 KiB-granule table (the GPU's and Aura's stage 2), a multiple
// of VidMm's 64 KiB placement, the AArch64 reserved-memory granule and the
// CPU's 2 MiB large page.
//
#define AURA_J813_GPU_POOL_SIZE       0x40000000ULL
#define AURA_J813_GPU_POOL_ALIGNMENT  0x2000000ULL

//
// The host's unified display key, the same property and rule as
// AuraJ813UnifiedDisplay.c: exactly 4 bytes, little-endian 1.
//
#define AURA_J813_GPU_POOL_UNIFIED_MARKER  "aura,windows-unified-display-v1"

#pragma pack(push, 1)
typedef struct {
  UINT32    Signature;
  UINT16    Version;
  UINT16    StructureSize;
  UINT64    Base;
  UINT64    Size;
  UINT64    GuestMemoryBase;   // the guest RAM MemoryPeim was given
  UINT64    GuestMemoryTop;
  UINT64    Reserved;          // 0
} AURA_J813_GPU_POOL;
#pragma pack(pop)

STATIC_ASSERT (sizeof (AURA_J813_GPU_POOL) == 48, "AURA_J813_GPU_POOL v1 layout");

#endif

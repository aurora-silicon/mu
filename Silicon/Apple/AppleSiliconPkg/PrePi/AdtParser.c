/*
 * Copyright (c) 2015, Linaro Ltd. All rights reserved.
 *
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/DebugLib.h>
#include <Library/AppleDTLib.h>

#if defined(J804_M1)
#include <J804M1.h>
#include <Library/PanelCheckpointLib.h>
#endif

#if defined(J804_WINDOWS)
#include <Library/ArmLib.h>

typedef struct {
  UINT64           Magic;
  volatile UINT64  Stage;
} J804_WIN_CHECKPOINT_RECORD;

// PrePi.c MU_CHECKPOINT_RECORD (same layout).
extern volatile J804_WIN_CHECKPOINT_RECORD  gMuCheckpoint;
#endif

#if defined(J804_WINDOWS_GPU)
#include <Library/CacheMaintenanceLib.h>
#include <Library/HobLib.h>
#define J804_GPU_HANDOFF_NO_SSDT
#include <J804GpuHandoff.h>
#endif

#if defined(J804_M1)

STATIC
VOID
J804M1EarlyStop (
  IN UINTN  Code
  )
{
  PanelCheckpointError (Code);
  CpuDeadLoop ();
}

//
// The checks below run on the temporary stack in the FD header window, after
// boot_args has been copied to PcdBootArgsPointer (the panel checkpoint
// library reads the framebuffer geometry there) and before
// _ModuleEntrySetupStack writes the stack and UEFI region below the RAM top.
// Each failure draws a striped error block and stops; EarlySetup never
// returns FALSE in the J804_M1 build.
//

//
// Needs only boot_args: refuse an untrimmed RAM top before anything else is
// written, and refuse a device tree that would overrun its 2 MiB copy.
//
STATIC
VOID
J804M1CheckBootArgs (
  IN UINT64  RamTop,
  IN UINT64  DevtreeSize
  )
{
  if (RamTop > J804_M1_RAM_TOP_LIMIT) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_M1: untrimmed RAM top 0x%lx above 0x%lx, stopping\n",
      RamTop,
      J804_M1_RAM_TOP_LIMIT
      ));
    J804M1EarlyStop (J804_M1_ERROR_RAM_TOP_UNTRIMMED);
  }

  if ((DevtreeSize == 0) || (DevtreeSize > J804_M1_ADT_COPY_MAX)) {
    DEBUG ((DEBUG_ERROR, "J804_M1: device tree size 0x%lx unusable, stopping\n", DevtreeSize));
    J804M1EarlyStop (J804_M1_ERROR_BAD_ADT);
  }
}

//
// Needs the validated device tree copy: this must be a J804, and the UEFI
// region PrePi is about to use must not overlap a carve-out, a memory-map
// entry, the FD or the boot_args/ADT copies. A RAM top below
// J804_M1_RAM_TOP_LIMIT is accepted only if that region is clear.
//
STATIC
VOID
J804M1CheckAdt (
  IN UINT64  RamBase,
  IN UINT64  RamTop
  )
{
  J804_M1_REGION_CHECK  Check;
  UINT64                RegionBase;
  UINT64                RegionTop;
  UINT64                FdBase;

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_M1: device tree is not a J804, stopping\n"));
    J804M1EarlyStop (J804_M1_ERROR_NOT_J804);
  }

  FdBase = PcdGet64 (PcdFdBaseAddress);
  J804M1UefiRegion (
    RamBase,
    RamTop,
    FdBase,
    FixedPcdGet32 (PcdFdSize),
    FixedPcdGet32 (PcdSystemMemoryUefiRegionSize),
    &RegionBase,
    &RegionTop
    );

  if (!J804M1RegionIsClear (
         RamBase,
         RamTop,
         RegionBase,
         RegionTop,
         FdBase,
         FixedPcdGet32 (PcdFdSize),
         FixedPcdGet64 (PcdBootArgsPointer),
         FixedPcdGet64 (PcdAdtPointer),
         &Check
         ))
  {
    if (Check.HitKey == NULL) {
      DEBUG ((DEBUG_ERROR, "J804_M1: no /chosen/carveout-memory-map, stopping\n"));
      J804M1EarlyStop (J804_M1_ERROR_NO_CARVEOUT_MAP);
    }

    DEBUG ((
      DEBUG_ERROR,
      "J804_M1: %a 0x%lx..0x%lx overlaps the UEFI region 0x%lx..0x%lx, stopping\n",
      Check.HitKey,
      Check.HitBase,
      Check.HitTop,
      RegionBase,
      RegionTop
      ));
    J804M1EarlyStop (J804_M1_ERROR_UEFI_REGION_OVERLAP);
  }
}
#endif

#if defined(J804_WINDOWS_GPU)
//
// G0 GPU pool (windows-m6/G0-CARRIER-CONTRACT.md 6 to 8). The carrier carves
// the GPU pool from the top of its guest RAM window, which is exactly the
// boot_args window Mu runs in, and PrePi places its stack and UEFI region at
// that top. So the pool can only be kept out of Mu's (and Windows') RAM here,
// before anything is written there: when the launcher's handshake names a
// handoff page that validates, Mu's RAM top is lowered to the pool base and
// J804GpuReservePoolHob (below, called from PrePi.c) adds the pool as a
// reserved resource HOB (EfiReservedMemoryType in the UEFI memory map). Anything less than a valid page changes nothing: no
// handshake, a bad page, or a pool that would not leave the UEFI region, the
// FD and the boot hand-off copies clear below it, keeps the whole window as
// RAM (and J804GpuAcpiDxe then publishes no GPU0, since the pool is not
// reserved).
//
// The page is the carrier's, outside guest RAM, mapped read-only at IPA ==
// PA. The MMU is off here, so the reads are uncached: clean and invalidate
// the page to the point of coherency first, in case the carrier's cached
// writes are still in a cache (clean+invalidate needs only read permission).
//
volatile UINT64  gJ804GpuPoolBase;
volatile UINT64  gJ804GpuPoolSize;

STATIC CONST CHAR8  *CONST  mJ804GpuStatusText[] = {
  J804_GPU_STATUS_TEXT
};

STATIC
UINT64
J804GpuEarlyCarvePool (
  IN UINT64  RamBase,
  IN UINT64  RamTop
  )
{
  dt_node_t             *Chosen;
  CONST VOID            *Value;
  size_t                Length;
  J804_GPU_PROPERTY     Property;
  J804_GPU_HANDOFF      Handoff;
  J804_GPU_STATUS       Status;
  J804_M1_REGION_CHECK  Check;
  UINT64                FdBase;
  UINT64                FdSize;
  UINT64                RegionBase;
  UINT64                RegionTop;

  gJ804GpuPoolBase = 0;
  gJ804GpuPoolSize = 0;
  Chosen           = dt_get ("/chosen");
  Length           = 0;
  Value            = (Chosen != NULL) ? dt_prop (Chosen, J804_GPU_ADT_PROPERTY, &Length) : NULL;
  if (Value == NULL) {
    DEBUG ((DEBUG_INFO, "J804_WINDOWS_GPU: no /chosen/%a, RAM top 0x%lx unchanged\n", J804_GPU_ADT_PROPERTY, RamTop));
    return RamTop;
  }

  Status = J804GpuParseProperty (Value, Length, &Property);
  if ((Status == J804GpuOk) && J804GpuOverlap (Property.Ipa, Property.Ipa + Property.Size, RamBase, RamTop)) {
    Status = J804GpuBadPropertyPage;
  }

  if (Status == J804GpuOk) {
    WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Property.Ipa, (UINTN)Property.Size);
    Status = J804GpuParseHandoff (
               (CONST volatile UINT8 *)(UINTN)Property.Ipa,
               Property.Ipa,
               RamBase,
               RamTop,
               &Handoff
               );
  }

  if (Status != J804GpuOk) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_GPU refused (pool not reserved, RAM top 0x%lx unchanged): %a\n",
      RamTop,
      mJ804GpuStatusText[Status]
      ));
    return RamTop;
  }

  //
  // Everything PrePi needs below the new top: the FD, the boot hand-off
  // copies and a UEFI region that overlaps no carve-out or memory-map entry.
  //
  FdBase = PcdGet64 (PcdFdBaseAddress);
  FdSize = FixedPcdGet32 (PcdFdSize);
  J804M1UefiRegion (
    RamBase,
    Handoff.PoolBase,
    FdBase,
    FdSize,
    FixedPcdGet32 (PcdSystemMemoryUefiRegionSize),
    &RegionBase,
    &RegionTop
    );
  if ((FdBase + FdSize > Handoff.PoolBase) ||
      (FixedPcdGet64 (PcdBootArgsPointer) + 0x4000 > Handoff.PoolBase) ||
      (FixedPcdGet64 (PcdAdtPointer) + J804_M1_ADT_COPY_MAX > Handoff.PoolBase) ||
      !J804M1RegionIsClear (
         RamBase,
         Handoff.PoolBase,
         RegionBase,
         RegionTop,
         FdBase,
         FdSize,
         FixedPcdGet64 (PcdBootArgsPointer),
         FixedPcdGet64 (PcdAdtPointer),
         &Check
         ))
  {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_GPU refused (pool not reserved, RAM top 0x%lx unchanged): the pool 0x%lx+0x%lx leaves no clear UEFI region, FD or hand-off copies below it\n",
      RamTop,
      Handoff.PoolBase,
      Handoff.PoolSize
      ));
    return RamTop;
  }

  gJ804GpuPoolBase = Handoff.PoolBase;
  gJ804GpuPoolSize = Handoff.PoolSize;
  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_GPU: pool 0x%lx+0x%lx reserved, RAM top 0x%lx -> 0x%lx (handoff 0x%lx, state 0x%x, %a)\n",
    Handoff.PoolBase,
    Handoff.PoolSize,
    RamTop,
    Handoff.PoolBase,
    Property.Ipa,
    Handoff.State,
    Handoff.Receipt
    ));
  return Handoff.PoolBase;
}

//
// Called from PrePi.c Main once the HOB list exists: describe the pool as
// reserved memory (EfiReservedMemoryType in the UEFI memory map), with the
// cache capabilities of RAM but neither PRESENT nor INITIALIZED, so the DXE
// core never promotes it to system memory and Mu never maps it.
//
VOID
J804GpuReservePoolHob (
  VOID
  )
{
  if (gJ804GpuPoolSize == 0) {
    return;
  }

  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE,
    gJ804GpuPoolBase,
    gJ804GpuPoolSize
    );
  DEBUG ((DEBUG_INFO, "J804_WINDOWS_GPU: pool 0x%lx+0x%lx described as reserved memory\n", gJ804GpuPoolBase, gJ804GpuPoolSize));
}

#endif

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))

// Captured before CEntryPoint so PrePi can report progress directly on the
// already-initialized Apple framebuffer even when both serial paths are quiet.
volatile UINT64 gMuFrameBufferBase;
volatile UINT32 gMuFrameBufferStride;
volatile UINT32 gMuFrameBufferWidth;
volatile UINT32 gMuFrameBufferHeight;

BOOLEAN
EarlySetup (
  IN VOID     *BootArgsAddr,
  OUT UINT64  *SystemMemoryBase,
  OUT UINT64  *SystemMemorySize,
  IN    VOID  *PcdBootArgsDest,
  IN    VOID  *PcdAdtDest
  )
{

  struct boot_args *BootArgs = (struct boot_args *)BootArgsAddr;
  VOID             *AdtSource;

  AdtSource = (VOID *)((UINTN)BootArgs->devtree -
                       (UINTN)BootArgs->virt_base +
                       (UINTN)BootArgs->phys_base);

  DEBUG ((
    DEBUG_ERROR,
    "J813 EARLY: boot_args=%p boot_dest=%p adt_src=%p adt_dest=%p adt_size=0x%x\n",
    BootArgsAddr,
    PcdBootArgsDest,
    AdtSource,
    PcdAdtDest,
    BootArgs->devtree_size
    ));

#if defined(J804_WINDOWS)
  //
  // Nothing is copied yet, so the panel cannot be used (its geometry is read
  // from the copy): stop in a loop with a checkpoint stage only.
  //
  if (!J804BootHandoffSourcesClear (
         BootArgs->phys_base,
         BootArgs->phys_base + BootArgs->mem_size,
         PcdGet64 (PcdFdBaseAddress),
         FixedPcdGet32 (PcdFdSize),
         (UINT64)(UINTN)BootArgsAddr,
         sizeof (struct boot_args),
         (UINT64)(UINTN)AdtSource,
         BootArgs->devtree_size,
         (UINT64)(UINTN)PcdBootArgsDest,
         (UINT64)(UINTN)PcdAdtDest
         ))
  {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS: boot hand-off sources overlap the fixed copies, stopping\n"));
    gMuCheckpoint.Stage = J804_WIN_STAGE_HANDOFF_SOURCES;
    ArmDataMemoryBarrier ();
    CpuDeadLoop ();
  }
#endif

  CopyMem(PcdBootArgsDest, BootArgsAddr, sizeof(struct boot_args));
  DEBUG ((DEBUG_ERROR, "J813 EARLY: boot args copied\n"));
#if defined(J804_M1)
  J804M1CheckBootArgs (BootArgs->phys_base + BootArgs->mem_size, BootArgs->devtree_size);
#endif
  CopyMem(PcdAdtDest, AdtSource, BootArgs->devtree_size);
  DEBUG ((DEBUG_ERROR, "J813 EARLY: ADT copied\n"));

  if(dt_check(PcdAdtDest, BootArgs->devtree_size, NULL) != 0) {
    DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "no ADT supplied, exiting\n"));
#if defined(J804_M1)
    J804M1EarlyStop (J804_M1_ERROR_BAD_ADT);
#endif
    return FALSE;
  }
  DEBUG ((DEBUG_ERROR, "J813 EARLY: ADT validated\n"));

  *SystemMemoryBase = BootArgs->phys_base;
#if defined(J804_WINDOWS_GPU)
  *SystemMemorySize = J804GpuEarlyCarvePool (BootArgs->phys_base, BootArgs->phys_base + BootArgs->mem_size) - BootArgs->phys_base;
#else
  *SystemMemorySize = BootArgs->phys_base + BootArgs->mem_size - BootArgs->phys_base;
#endif


  PatchPcdSet64(PcdFrameBufferAddress, BootArgs->video.base);
  PatchPcdSet64(PcdFrameBufferSize, ALIGN_UP(BootArgs->video.stride * BootArgs->video.height, 0x4000)); // for notched/internal displays, the FB size has to be aligned up!

  gMuFrameBufferBase   = BootArgs->video.base;
  gMuFrameBufferStride = BootArgs->video.stride;
  gMuFrameBufferWidth  = BootArgs->video.width;
  gMuFrameBufferHeight = BootArgs->video.height;

#if defined(J804_M1)
#if defined(J804_WINDOWS_GPU)
  J804M1CheckAdt (BootArgs->phys_base, BootArgs->phys_base + *SystemMemorySize);
#else
  J804M1CheckAdt (BootArgs->phys_base, BootArgs->phys_base + BootArgs->mem_size);
#endif
#endif

  DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "Framebuffer address loaded to PCDs: 0x%llx (size 0x%llx)\n", PcdGet64(PcdFrameBufferAddress), PcdGet64(PcdFrameBufferSize)));

  return TRUE;
}

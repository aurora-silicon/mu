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
  *SystemMemorySize = BootArgs->phys_base + BootArgs->mem_size - BootArgs->phys_base;


  PatchPcdSet64(PcdFrameBufferAddress, BootArgs->video.base);
  PatchPcdSet64(PcdFrameBufferSize, ALIGN_UP(BootArgs->video.stride * BootArgs->video.height, 0x4000)); // for notched/internal displays, the FB size has to be aligned up!

  gMuFrameBufferBase   = BootArgs->video.base;
  gMuFrameBufferStride = BootArgs->video.stride;
  gMuFrameBufferWidth  = BootArgs->video.width;
  gMuFrameBufferHeight = BootArgs->video.height;

#if defined(J804_M1)
  J804M1CheckAdt (BootArgs->phys_base, BootArgs->phys_base + BootArgs->mem_size);
#endif

  DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "Framebuffer address loaded to PCDs: 0x%llx (size 0x%llx)\n", PcdGet64(PcdFrameBufferAddress), PcdGet64(PcdFrameBufferSize)));

  return TRUE;
}

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

//
// Runs on the temporary stack in the FD header window, before
// _ModuleEntrySetupStack writes the stack and UEFI region below the RAM top.
// Stop here, with a striped error block on the panel, if this is not a J804 or
// if the launcher did not lower the RAM top below the J804 top carve-out.
//
STATIC
VOID
J804M1CheckRamTop (
  IN UINT64  RamTop
  )
{
  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_M1: device tree is not a J804, stopping\n"));
    PanelCheckpointError (J804_M1_ERROR_NOT_J804);
    CpuDeadLoop ();
  }

  if (RamTop > J804_M1_RAM_TOP_LIMIT) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_M1: untrimmed RAM top 0x%lx above 0x%lx, stopping\n",
      RamTop,
      J804_M1_RAM_TOP_LIMIT
      ));
    PanelCheckpointError (J804_M1_ERROR_RAM_TOP_UNTRIMMED);
    CpuDeadLoop ();
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

  CopyMem(PcdBootArgsDest, BootArgsAddr, sizeof(struct boot_args));
  DEBUG ((DEBUG_ERROR, "J813 EARLY: boot args copied\n"));
  CopyMem(PcdAdtDest, AdtSource, BootArgs->devtree_size);
  DEBUG ((DEBUG_ERROR, "J813 EARLY: ADT copied\n"));

  if(dt_check(PcdAdtDest, BootArgs->devtree_size, NULL) != 0) {
    DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "no ADT supplied, exiting\n"));
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
  J804M1CheckRamTop (BootArgs->phys_base + BootArgs->mem_size);
#endif

  DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "Framebuffer address loaded to PCDs: 0x%llx (size 0x%llx)\n", PcdGet64(PcdFrameBufferAddress), PcdGet64(PcdFrameBufferSize)));

  return TRUE;
}

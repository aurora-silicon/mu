/**
 * @file MemoryInitPeiLib.c
 *
 * @author amarioguy (Arminder Singh)
 *
 * This file implements page table setup, memory HOB setup, and MMU initialization.
 * Adapted from SurfaceDuoPkg/MemoryInitPeiLib.c
 *
 * @version 1.0
 * @date 2022-07-31
 *
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/AppleDTLib.h>

#include <AppendedRamdisk.h>

#if defined(J804_M1)
#include <J804M1.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PanelCheckpointLib.h>
#endif

//Device memory map configuration file for UEFI (this is to help with pagetable initialization)
// J873 physical apertures measured from its ADT.

//
// T8142 uses 12 core MMIO ranges + 9 PCIe ranges (2 ECAM + 7 BAR windows) +
// DRAM + framebuffer + terminator = 24 descriptors. Kept at 28 for headroom.
//
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 28

#define DDR_ATTRIBUTES_CACHED           ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED         ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

STATIC CONST EFI_GUID  mNtasiAppendedRamdiskLocationHobGuid =
  NTASI_APPENDED_RAMDISK_LOCATION_HOB_GUID;

VOID BuildMemoryTypeInformationHob(VOID);

VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap);

STATIC VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{
    VOID *MemoryTranslationTableBase;
    UINTN MemoryTranslationTableSize;
    RETURN_STATUS StatusCode;

    DEBUG(
        (DEBUG_INFO,
        "MemoryInitPeiLib: Enabling MMU, Page Table Base: 0x%p, Page Table Size: 0x%p\n",
        &MemoryTranslationTableBase, &MemoryTranslationTableSize)
        );
    StatusCode = ArmConfigureMmu(MemoryTable, &MemoryTranslationTableBase, &MemoryTranslationTableSize);

    if(EFI_ERROR(StatusCode))
    {
        DEBUG((DEBUG_ERROR | DEBUG_INFO, "MemoryInitPeiLib: MMU enable failed!! Status: %llx\n", StatusCode));
    }
}

#if !defined(J804_M1)
//borrowed from edk2-platforms:Armada7k8kMemoryInitPeiLib
STATIC VOID ReserveMemoryRegion ( IN EFI_PHYSICAL_ADDRESS ReservedRegionBase, IN UINT32 ReservedRegionSize)
{
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  EFI_PHYSICAL_ADDRESS         ReservedRegionTop;
  EFI_PHYSICAL_ADDRESS         ResourceTop;
  EFI_PEI_HOB_POINTERS         NextHob;
  UINT64                       ResourceLength;

  ReservedRegionTop = ReservedRegionBase + ReservedRegionSize;

  //
  // Search for System Memory Hob that covers the reserved region,
  // and punch a hole in it
  //
  for (NextHob.Raw = GetHobList ();
       NextHob.Raw != NULL;
       NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
                                 NextHob.Raw)) {

    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (ReservedRegionBase >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (ReservedRegionTop <= NextHob.ResourceDescriptor->PhysicalStart +
                      NextHob.ResourceDescriptor->ResourceLength))
    {
      ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
      ResourceLength = NextHob.ResourceDescriptor->ResourceLength;
      ResourceTop = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

      if (ReservedRegionBase == NextHob.ResourceDescriptor->PhysicalStart) {
        //
        // This region starts right at the start of the reserved region, so we
        // can simply move its start pointer and reduce its length by the same
        // value
        //
        NextHob.ResourceDescriptor->PhysicalStart += ReservedRegionSize;
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else if ((NextHob.ResourceDescriptor->PhysicalStart +
                  NextHob.ResourceDescriptor->ResourceLength) ==
                  ReservedRegionTop) {

        //
        // This region ends right at the end of the reserved region, so we
        // can simply reduce its length by the size of the region.
        //
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else {
        //
        // This region covers the reserved region. So split it into two regions,
        // each one touching the reserved region at either end, but not covering
        // it.
        //
        NextHob.ResourceDescriptor->ResourceLength =
                 ReservedRegionBase - NextHob.ResourceDescriptor->PhysicalStart;

        // Create the System Memory HOB for the remaining region (top of the FD)
        BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY,
                                    ResourceAttributes,
                                    ReservedRegionTop,
                                    ResourceTop - ReservedRegionTop);
      }

      //
      // Reserve the memory space.
      //
      BuildResourceDescriptorHob (EFI_RESOURCE_MEMORY_RESERVED,
        0,
        ReservedRegionBase,
        ReservedRegionSize);

      break;
    }
    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }
}
#endif


#if defined(J804_M1)
//
// J804 M1 reservations, built from the device tree this library already reads.
//
// Every range below is clipped to Mu's RAM, rounded outward to whole pages,
// collected, sorted and merged, and only then punched out of the system
// memory HOBs, so overlapping and adjacent ranges are handled once:
//   * every /chosen/carveout-memory-map entry that overlaps RAM, except
//     region-id-24, which is the usable DRAM range itself;
//   * every /chosen/memory-map entry in RAM (SEPFW, SEPPatches, uStuff,
//     TrustCache, DeviceTree, preoslog, BootArgs, RTBuddySeg, ...);
//   * the firmware block chainload.py packs directly after the FD (see
//     J804M1AddChainloadBlock);
//   * all of region-id-353 (through the carve-out rule; it holds the loader,
//     its heap and the original boot blobs);
//   * as before: the boot_args copy, the 2 MiB ADT copy, each cpu's
//     cpm-impl-reg range and an appended ramdisk.
// The FD itself is cut out of every range: it keeps its own system memory HOB
// and its boot services data allocation. A range that reaches the UEFI region
// and stack at the RAM top stops the boot with a panel error block.
//
#define J804_M1_MAX_RANGES  256

//
// chainload.py places the boot blobs and boot_args after the image, each
// start rounded up to 16 KiB (aura proxyclient utils.align), and rewrites the
// matching /chosen/memory-map entries to the new addresses.
//
#define J804_M1_CHAINLOAD_ALIGN  0x4000

typedef struct {
  UINT64    Base;
  UINT64    Top;
} J804_M1_RANGE;

typedef struct {
  UINT64     RamBase;
  UINT64     RamTop;
  UINT64     FdBase;
  UINT64     FdTop;
  UINTN      Count;
  BOOLEAN    Overflow;
  BOOLEAN    SawLoaderRegion;
} J804_M1_STATE;

STATIC J804_M1_RANGE  mJ804M1Ranges[J804_M1_MAX_RANGES];
STATIC J804_M1_STATE  mJ804M1;

//
// Device tree values are only 4-byte aligned and this runs with the MMU off,
// so read 64-bit cells as two 32-bit halves.
//
STATIC
UINT64
J804M1ReadCell64 (
  IN CONST VOID  *Value
  )
{
  CONST volatile UINT32  *Words;

  Words = (CONST volatile UINT32 *)Value;
  return (UINT64)Words[0] | ((UINT64)Words[1] << 32);
}

STATIC
VOID
J804M1Append (
  IN UINT64  Base,
  IN UINT64  Top
  )
{
  if (Base >= Top) {
    return;
  }

  if (mJ804M1.Count >= J804_M1_MAX_RANGES) {
    mJ804M1.Overflow = TRUE;
    return;
  }

  mJ804M1Ranges[mJ804M1.Count].Base = Base;
  mJ804M1Ranges[mJ804M1.Count].Top  = Top;
  mJ804M1.Count++;
}

STATIC
VOID
J804M1AddRange (
  IN UINT64       Base,
  IN UINT64       Size,
  IN CONST CHAR8  *Label
  )
{
  UINT64  Top;

  if ((Size == 0) || (Base == MAX_UINT64) || (Size == MAX_UINT64)) {
    return;
  }

  Top = Base + Size;
  if (Top < Base) {
    Top = MAX_UINT64;
  }

  // Clip to RAM; ranges wholly outside it (MMIO, other DRAM) are not ours.
  if ((Top <= mJ804M1.RamBase) || (Base >= mJ804M1.RamTop)) {
    return;
  }

  if (Base < mJ804M1.RamBase) {
    Base = mJ804M1.RamBase;
  }

  if (Top > mJ804M1.RamTop) {
    Top = mJ804M1.RamTop;
  }

  // Round outward to whole pages, then clip again.
  Base = Base & ~(UINT64)EFI_PAGE_MASK;
  Top  = (Top + EFI_PAGE_MASK) & ~(UINT64)EFI_PAGE_MASK;
  if (Base < mJ804M1.RamBase) {
    Base = mJ804M1.RamBase;
  }

  if (Top > mJ804M1.RamTop) {
    Top = mJ804M1.RamTop;
  }

  DEBUG ((DEBUG_INFO, "J804_M1: reserve %a 0x%lx..0x%lx\n", Label, Base, Top));

  // The FD keeps its own HOBs; reserve whatever lies on either side of it.
  if ((Base < mJ804M1.FdTop) && (Top > mJ804M1.FdBase)) {
    J804M1Append (Base, mJ804M1.FdBase);
    J804M1Append (mJ804M1.FdTop, Top);
  } else {
    J804M1Append (Base, Top);
  }
}

STATIC
int
J804M1CarveoutCallback (
  IN VOID         *Context,
  IN dt_node_t    *Node,
  IN int          Depth,
  IN CONST CHAR8  *Key,
  IN VOID         *Value,
  IN size_t       Length
  )
{
  UINT64  Base;
  UINT64  Size;

  if ((AsciiStrnCmp (Key, "region-id-", 10) != 0) || (Length < 2 * sizeof (UINT64))) {
    return 0;
  }

  if (AsciiStrnCmp (Key, J804_M1_USABLE_RANGE_REGION, DT_KEY_LEN) == 0) {
    return 0;
  }

  Base = J804M1ReadCell64 (Value);
  Size = J804M1ReadCell64 ((CONST UINT8 *)Value + sizeof (UINT64));

  //
  // A carve-out that spans all of Mu's RAM is a description of the usable
  // range, not a hole in it (region-id-24 on the saved J804 tree).
  //
  if ((Base <= mJ804M1.RamBase) && (Base + Size >= mJ804M1.RamTop) && (Size != 0)) {
    DEBUG ((DEBUG_WARN, "J804_M1: %a spans all RAM, treated as the usable range\n", Key));
    return 0;
  }

  if (AsciiStrnCmp (Key, J804_M1_LOADER_REGION, DT_KEY_LEN) == 0) {
    mJ804M1.SawLoaderRegion = TRUE;
  }

  J804M1AddRange (Base, Size, Key);
  return 0;
}

STATIC
int
J804M1MemoryMapCallback (
  IN VOID         *Context,
  IN dt_node_t    *Node,
  IN int          Depth,
  IN CONST CHAR8  *Key,
  IN VOID         *Value,
  IN size_t       Length
  )
{
  if (Length < 2 * sizeof (UINT64)) {
    return 0;     // "name", phandles and flags
  }

  J804M1AddRange (
    J804M1ReadCell64 (Value),
    J804M1ReadCell64 ((CONST UINT8 *)Value + sizeof (UINT64)),
    Key
    );
  return 0;
}

typedef struct {
  UINT64     Cursor;
  UINT64     NextTop;
  BOOLEAN    Found;
} J804_M1_CHAIN;

STATIC
int
J804M1ChainCallback (
  IN VOID         *Context,
  IN dt_node_t    *Node,
  IN int          Depth,
  IN CONST CHAR8  *Key,
  IN VOID         *Value,
  IN size_t       Length
  )
{
  J804_M1_CHAIN  *Chain;
  UINT64         Base;
  UINT64         Size;

  Chain = Context;
  if (Length < 2 * sizeof (UINT64)) {
    return 0;
  }

  Base = J804M1ReadCell64 (Value);
  Size = J804M1ReadCell64 ((CONST UINT8 *)Value + sizeof (UINT64));
  if ((Size == 0) || (Base == MAX_UINT64) || (Size == MAX_UINT64) ||
      (Base + Size < Base))
  {
    return 0;
  }

  if ((Base >= Chain->Cursor) && (Base <= Chain->Cursor + J804_M1_CHAINLOAD_ALIGN)) {
    if (!Chain->Found || (Base + Size > Chain->NextTop)) {
      Chain->NextTop = Base + Size;
    }

    Chain->Found = TRUE;
  }

  return 0;
}

//
// chainload.py --raw packs the boot blobs and boot_args (its "firmware
// block") right behind the image: the first at the next 16 KiB boundary after
// the image plus 4 bytes, each later one at the next 16 KiB boundary after the
// previous, and it rewrites their /chosen/memory-map entries to point there.
// Mu cannot see the block's size directly, so follow that chain from the FD
// top (or the end of an appended ramdisk) through the memory map and reserve
// the whole span, including the padding between blobs. Each blob is also
// reserved on its own by the memory-map rule, so a broken chain only loses
// the padding.
//
STATIC
VOID
J804M1AddChainloadBlock (
  IN dt_node_t  *MemoryMap,
  IN UINT64     Start
  )
{
  J804_M1_CHAIN  Chain;
  UINTN          Steps;

  Chain.Cursor = Start;
  for (Steps = 0; Steps < 64; Steps++) {
    Chain.Found   = FALSE;
    Chain.NextTop = 0;
    dt_parse (MemoryMap, -1, NULL, NULL, NULL, J804M1ChainCallback, &Chain);
    if (!Chain.Found || (Chain.NextTop <= Chain.Cursor)) {
      break;
    }

    Chain.Cursor = Chain.NextTop;
  }

  if (Chain.Cursor == Start) {
    DEBUG ((DEBUG_WARN, "J804_M1: chainload firmware block not found after 0x%lx\n", Start));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "J804_M1: chainload firmware block 0x%lx..0x%lx (%u steps)\n",
    Start,
    Chain.Cursor,
    (UINT32)Steps
    ));
  J804M1AddRange (Start, Chain.Cursor - Start, "chainload firmware block");
}

//
// Punch [Base, Top) out of every system memory HOB it touches, however the
// earlier holes have split them.
//
STATIC
VOID
J804M1PunchHole (
  IN UINT64  Base,
  IN UINT64  Top
  )
{
  EFI_PEI_HOB_POINTERS         Hob;
  EFI_HOB_RESOURCE_DESCRIPTOR  *Resource;
  EFI_RESOURCE_ATTRIBUTE_TYPE  Attributes;
  UINT64                       Start;
  UINT64                       End;
  UINT64                       CutBase;
  UINT64                       CutTop;

  for (Hob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, GetHobList ());
       Hob.Raw != NULL;
       Hob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, GET_NEXT_HOB (Hob)))
  {
    Resource = Hob.ResourceDescriptor;
    if (Resource->ResourceType != EFI_RESOURCE_SYSTEM_MEMORY) {
      continue;
    }

    Start = Resource->PhysicalStart;
    End   = Start + Resource->ResourceLength;
    if ((End <= Base) || (Start >= Top) || (End == Start)) {
      continue;
    }

    CutBase    = MAX (Start, Base);
    CutTop     = MIN (End, Top);
    Attributes = Resource->ResourceAttribute;

    if ((CutBase == Start) && (CutTop == End)) {
      Resource->ResourceType      = EFI_RESOURCE_MEMORY_RESERVED;
      Resource->ResourceAttribute = 0;
      continue;
    }

    if (CutBase == Start) {
      Resource->PhysicalStart  = CutTop;
      Resource->ResourceLength = End - CutTop;
    } else {
      Resource->ResourceLength = CutBase - Start;
      if (CutTop < End) {
        BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, Attributes, CutTop, End - CutTop);
      }
    }

    BuildResourceDescriptorHob (EFI_RESOURCE_MEMORY_RESERVED, 0, CutBase, CutTop - CutBase);
  }
}

STATIC
VOID
J804M1Stop (
  IN UINTN  Code
  )
{
  PanelCheckpointError (Code);
  CpuDeadLoop ();
}

STATIC
VOID
J804M1ReserveFromAdt (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize,
  IN EFI_PHYSICAL_ADDRESS  FdTop,
  IN UINT64                AppendedSize
  )
{
  dt_node_t      *Carveouts;
  dt_node_t      *MemoryMap;
  J804_M1_RANGE  Swap;
  UINTN          Index;
  UINTN          Inner;
  UINTN          Merged;
  UINT64         UefiTop;
  UINT64         Total;
  CHAR8          CpuNodeName[14];

  ZeroMem (&mJ804M1, sizeof (mJ804M1));
  mJ804M1.RamBase = PcdGet64 (PcdSystemMemoryBase);
  mJ804M1.RamTop  = mJ804M1.RamBase + PcdGet64 (PcdSystemMemorySize);
  mJ804M1.FdBase  = PcdGet64 (PcdFdBaseAddress);
  mJ804M1.FdTop   = mJ804M1.FdBase + PcdGet32 (PcdFdSize);

  Carveouts = dt_get ("/chosen/carveout-memory-map");
  MemoryMap = dt_get ("/chosen/memory-map");
  if ((Carveouts == NULL) || (MemoryMap == NULL)) {
    DEBUG ((DEBUG_ERROR, "J804_M1: no /chosen carve-out or memory map, stopping\n"));
    J804M1Stop (J804_M1_ERROR_NO_CARVEOUT_MAP);
  }

  dt_parse (Carveouts, -1, NULL, NULL, NULL, J804M1CarveoutCallback, NULL);
  if (!mJ804M1.SawLoaderRegion) {
    DEBUG ((DEBUG_WARN, "J804_M1: %a not in the carve-out map\n", J804_M1_LOADER_REGION));
  }

  dt_parse (MemoryMap, -1, NULL, NULL, NULL, J804M1MemoryMapCallback, NULL);
  J804M1AddRange (FdTop, AppendedSize, "appended ramdisk");
  J804M1AddChainloadBlock (MemoryMap, FdTop + AppendedSize);

  // Kept from the J873 library: launcher-placed boot_args and the ADT copy
  // (bounded to 2 MiB), and each cpu's cpm-impl-reg range.
  J804M1AddRange (FixedPcdGet64 (PcdBootArgsPointer), 0x4000, "boot_args copy");
  J804M1AddRange (FixedPcdGet64 (PcdAdtPointer), 0x200000, "ADT copy");
  for (Index = 0; Index < PcdGet32 (PcdCoreCount); Index++) {
    dt_node_t  *CpuNode;
    UINT32     *Carveout;
    size_t     CarveoutLength;

    AsciiSPrint (CpuNodeName, ARRAY_SIZE (CpuNodeName), "/cpus/cpu%d", (UINT32)Index);
    CpuNode = dt_get (CpuNodeName);
    if (CpuNode == NULL) {
      continue;
    }

    CarveoutLength = 0;
    Carveout       = (UINT32 *)dt_node_prop (CpuNode, "cpm-impl-reg", &CarveoutLength);
    if ((Carveout == NULL) || (CarveoutLength < 4 * sizeof (UINT32))) {
      continue;
    }

    J804M1AddRange (
      ((UINT64)Carveout[1] << 32) | Carveout[0],
      ((UINT64)Carveout[3] << 32) | Carveout[2],
      "cpm-impl-reg"
      );
  }

  if (mJ804M1.Overflow) {
    DEBUG ((DEBUG_ERROR, "J804_M1: more than %u reservations, stopping\n", J804_M1_MAX_RANGES));
    J804M1Stop (J804_M1_ERROR_TOO_MANY_RANGES);
  }

  // Sort by base, then merge overlapping and touching ranges.
  for (Index = 1; Index < mJ804M1.Count; Index++) {
    Swap  = mJ804M1Ranges[Index];
    Inner = Index;
    while ((Inner > 0) && (mJ804M1Ranges[Inner - 1].Base > Swap.Base)) {
      mJ804M1Ranges[Inner] = mJ804M1Ranges[Inner - 1];
      Inner--;
    }

    mJ804M1Ranges[Inner] = Swap;
  }

  Merged = 0;
  for (Index = 0; Index < mJ804M1.Count; Index++) {
    if ((Merged > 0) && (mJ804M1Ranges[Index].Base <= mJ804M1Ranges[Merged - 1].Top)) {
      if (mJ804M1Ranges[Index].Top > mJ804M1Ranges[Merged - 1].Top) {
        mJ804M1Ranges[Merged - 1].Top = mJ804M1Ranges[Index].Top;
      }
    } else {
      mJ804M1Ranges[Merged++] = mJ804M1Ranges[Index];
    }
  }

  //
  // PrePi already lives in the UEFI region at the RAM top (stack, HOBs,
  // early allocations). A reservation there means the RAM top was not
  // lowered far enough; continuing would hand that memory to both owners.
  //
  UefiTop = MAX (UefiMemoryBase + UefiMemorySize, mJ804M1.RamTop);
  for (Index = 0; Index < Merged; Index++) {
    if ((mJ804M1Ranges[Index].Base < UefiTop) && (mJ804M1Ranges[Index].Top > UefiMemoryBase)) {
      DEBUG ((
        DEBUG_ERROR,
        "J804_M1: reservation 0x%lx..0x%lx overlaps the UEFI region 0x%lx..0x%lx, stopping\n",
        mJ804M1Ranges[Index].Base,
        mJ804M1Ranges[Index].Top,
        UefiMemoryBase,
        UefiTop
        ));
      J804M1Stop (J804_M1_ERROR_UEFI_REGION_OVERLAP);
    }
  }

  Total = 0;
  for (Index = 0; Index < Merged; Index++) {
    J804M1PunchHole (mJ804M1Ranges[Index].Base, mJ804M1Ranges[Index].Top);
    Total += mJ804M1Ranges[Index].Top - mJ804M1Ranges[Index].Base;
  }

  DEBUG ((
    DEBUG_INFO,
    "J804_M1: reserved %u ranges (%u merged), 0x%lx bytes of RAM 0x%lx..0x%lx\n",
    (UINT32)mJ804M1.Count,
    (UINT32)Merged,
    Total,
    mJ804M1.RamBase,
    mJ804M1.RamTop
    ));
}
#endif

#if defined(J804_M1)
//
// Fail closed if the launcher-placed boot_args or ADT copy is not where the
// reservations below assume (J804BootHandoffIsSane, adapted from the
// windows-mu-handoff ReserveBootHandoff check). Kept outside the reservation
// block above, which the host test extracts on its own.
//
STATIC
VOID
J804M1CheckBootHandoff (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize
  )
{
  CONST struct boot_args  *BootArgs;
  UINT64                  RamBase;
  UINT64                  RamTop;
  UINT64                  FdBase;
  UINT64                  UefiTop;

  RamBase  = PcdGet64 (PcdSystemMemoryBase);
  RamTop   = RamBase + PcdGet64 (PcdSystemMemorySize);
  FdBase   = PcdGet64 (PcdFdBaseAddress);
  UefiTop  = MAX (UefiMemoryBase + UefiMemorySize, RamTop);
  BootArgs = (CONST struct boot_args *)(UINTN)FixedPcdGet64 (PcdBootArgsPointer);
  if (!J804BootHandoffIsSane (
         RamBase,
         RamTop,
         FdBase,
         FdBase + PcdGet32 (PcdFdSize),
         UefiMemoryBase,
         UefiTop,
         FixedPcdGet64 (PcdBootArgsPointer),
         FixedPcdGet64 (PcdAdtPointer),
         BootArgs->devtree_size
         ))
  {
    DEBUG ((
      DEBUG_ERROR,
      "J804_M1: boot hand-off copies 0x%lx/0x%lx (ADT 0x%x bytes) are not clear of the FD, the UEFI region or RAM bounds, stopping\n",
      FixedPcdGet64 (PcdBootArgsPointer),
      FixedPcdGet64 (PcdAdtPointer),
      BootArgs->devtree_size
      ));
    J804M1Stop (J804_M1_ERROR_BOOT_HANDOFF);
  }

  DEBUG ((DEBUG_INFO, "J804_M1: boot hand-off copies checked\n"));
}
#endif


//Borrowed from ArmPlatformPkg
EFI_STATUS EFIAPI MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{
  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable;
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  UINT64                        ResourceLength;
  EFI_PEI_HOB_POINTERS          NextHob;
  EFI_PHYSICAL_ADDRESS          FdTop;
  EFI_PHYSICAL_ADDRESS          SystemMemoryTop;
  EFI_PHYSICAL_ADDRESS          ResourceTop;
  BOOLEAN                       Found;
  CONST NTASI_APPENDED_RAMDISK_HEADER  *AppendedHeader;
  UINT64                              AppendedReservationSize;
  EFI_PHYSICAL_ADDRESS                AppendedTop;
  NTASI_APPENDED_RAMDISK_LOCATION     AppendedLocation;

  // build up virtual memory map
  BuildVirtualMemoryMap(&MemoryTable);

  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);

  //
  // Now, the permanent memory has been installed, we can call AllocatePages()
  //
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );

  //
  // Check if the resource for the main system memory has been declared
  //
  Found       = FALSE;
  NextHob.Raw = GetHobList ();
  while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (PcdGet64 (PcdSystemMemoryBase) >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength <= PcdGet64 (PcdSystemMemoryBase) + PcdGet64 (PcdSystemMemorySize)))
    {
      Found = TRUE;
      break;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  if (!Found) {
    // Reserved the memory space occupied by the firmware volume
    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttributes,
      PcdGet64 (PcdSystemMemoryBase),
      PcdGet64 (PcdSystemMemorySize)
      );
  }

  //
  // Reserved the memory space occupied by the firmware volume
  //

  SystemMemoryTop = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemoryBase) + (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemorySize);
  FdTop           = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdFdBaseAddress) + (EFI_PHYSICAL_ADDRESS)PcdGet32 (PcdFdSize);

  // EDK2 does not have the concept of boot firmware copied into DRAM. To avoid the DXE
  // core to overwrite this area we must create a memory allocation HOB for the region,
  // but this only works if we split off the underlying resource descriptor as well.
  if ((PcdGet64 (PcdFdBaseAddress) >= PcdGet64 (PcdSystemMemoryBase)) && (FdTop <= SystemMemoryTop)) {
    Found = FALSE;

    // Search for System Memory Hob that contains the firmware
    NextHob.Raw = GetHobList ();
    while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
      if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
          (PcdGet64 (PcdFdBaseAddress) >= NextHob.ResourceDescriptor->PhysicalStart) &&
          (FdTop <= NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength))
      {
        ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
        ResourceLength     = NextHob.ResourceDescriptor->ResourceLength;
        ResourceTop        = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

        if (PcdGet64 (PcdFdBaseAddress) == NextHob.ResourceDescriptor->PhysicalStart) {
          if (SystemMemoryTop != FdTop) {
            // Create the System Memory HOB for the firmware
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              PcdGet64 (PcdFdBaseAddress),
              PcdGet32 (PcdFdSize)
              );

            // Top of the FD is system memory available for UEFI
            NextHob.ResourceDescriptor->PhysicalStart  += PcdGet32 (PcdFdSize);
            NextHob.ResourceDescriptor->ResourceLength -= PcdGet32 (PcdFdSize);
          }
        } else {
          // Create the System Memory HOB for the firmware
          BuildResourceDescriptorHob (
            EFI_RESOURCE_SYSTEM_MEMORY,
            ResourceAttributes,
            PcdGet64 (PcdFdBaseAddress),
            PcdGet32 (PcdFdSize)
            );

          // Update the HOB
          NextHob.ResourceDescriptor->ResourceLength = PcdGet64 (PcdFdBaseAddress) - NextHob.ResourceDescriptor->PhysicalStart;

          // If there is some memory available on the top of the FD then create a HOB
          if (FdTop < NextHob.ResourceDescriptor->PhysicalStart + ResourceLength) {
            // Create the System Memory HOB for the remaining region (top of the FD)
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              FdTop,
              ResourceTop - FdTop
              );
          }
        }

        // Mark the memory covering the Firmware Device as boot services data
        BuildMemoryAllocationHob (
          PcdGet64 (PcdFdBaseAddress),
          PcdGet32 (PcdFdSize),
          EfiBootServicesData
          );

        Found = TRUE;
        break;
      }

      NextHob.Raw = GET_NEXT_HOB (NextHob);
    }

    ASSERT (Found);
  }

  //
  // m1n1 may place a page-sized NTASI header and a GPT/FAT ramdisk directly
  // after the declared FD region.  Keep that range out of the PEI/DXE memory
  // allocators and publish its physical location to BootRamdiskHelperDxe.
  //
  // This handoff already exists in the T602x MemoryInitPeiLib.  J813 uses the
  // separate T8142 library, so without the equivalent code the valid appended
  // disk is silently ignored and BootRamdiskHelperDxe falls back to the small
  // FV-embedded HelloWorld disk.
  //
  AppendedReservationSize = 0;
  AppendedHeader = (CONST NTASI_APPENDED_RAMDISK_HEADER *)(UINTN)FdTop;
  if (AppendedHeader->Signature == NTASI_APPENDED_RAMDISK_SIGNATURE) {
    if (!NtasiValidateAppendedRamdisk (
           AppendedHeader,
           NTASI_APPENDED_RAMDISK_MAX_MAPPED_SPAN,
           FALSE,
           NULL,
           NULL,
           &AppendedReservationSize
           ))
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: invalid appended ramdisk header at 0x%lx\n", FdTop));
      return EFI_COMPROMISED_DATA;
    }

    AppendedTop = FdTop + AppendedReservationSize;
    if ((AppendedTop < FdTop) ||
        (AppendedTop > SystemMemoryTop) ||
        (AppendedReservationSize > MAX_UINT32))
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: appended ramdisk lies outside J873 system RAM\n"));
      return EFI_COMPROMISED_DATA;
    }

#if defined(J804_M1)
    // Reserved together with the rest of the J804 list below.
#else
    ReserveMemoryRegion (FdTop, (UINT32)AppendedReservationSize);
#endif

    AppendedLocation.Signature             = NTASI_APPENDED_RAMDISK_LOCATION_SIGNATURE;
    AppendedLocation.Version               = NTASI_APPENDED_RAMDISK_LOCATION_VERSION;
    AppendedLocation.StructureSize         = sizeof (AppendedLocation);
    AppendedLocation.HeaderPhysicalAddress = FdTop;
    AppendedLocation.ReservationSize       = AppendedReservationSize;
    if (BuildGuidDataHob (
          &mNtasiAppendedRamdiskLocationHobGuid,
          &AppendedLocation,
          sizeof (AppendedLocation)
          ) == NULL)
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: cannot publish appended ramdisk location HOB\n"));
      return EFI_OUT_OF_RESOURCES;
    }

    DEBUG ((
      DEBUG_INFO,
      "MemoryInitPeiLib: reserved appended ramdisk and published location HOB at 0x%lx (0x%lx bytes)\n",
      FdTop,
      AppendedReservationSize
      ));
  } else {
    DEBUG ((DEBUG_INFO, "MemoryInitPeiLib: no appended ramdisk header at FdTop 0x%lx\n", FdTop));
  }

#if defined(J804_M1)
  //
  // The appended ramdisk (if any) is the first thing after the FD, then the
  // chainload firmware block. J804M1ReserveFromAdt also covers boot_args, the
  // ADT and cpm-impl-reg, so the J873 calls below are not used.
  //
  J804M1CheckBootHandoff (UefiMemoryBase, UefiMemorySize);
  J804M1ReserveFromAdt (UefiMemoryBase, UefiMemorySize, FdTop, AppendedReservationSize);
#else
  // Launcher bounds ADT to 2 MiB and checks these fixed destinations.
  ReserveMemoryRegion (FixedPcdGet64 (PcdBootArgsPointer), 0x4000);
  ReserveMemoryRegion (FixedPcdGet64 (PcdAdtPointer), 0x200000);

  //reserve secondary stacks carveouts passed into cpm-impl-reg
  for(int i = 0; i < PcdGet32(PcdCoreCount); i++){
    CHAR8 CpuNodeName[14];
    UINTN CarveoutLength = 0;

    AsciiSPrint(CpuNodeName, ARRAY_SIZE(CpuNodeName), "/cpus/cpu%d", i);
    dt_node_t *CpuNode = dt_get(CpuNodeName);

    //
    // A cpu node can legitimately be absent, so PcdCoreCount is an upper bound
    // and not a promise.
    //
    // m1n1's run_guest.py deletes /cpus/cpuN outright for every core not listed
    // in -C ("Disabled cpu1" .. "Disabled cpu9" in its log), so a single-core
    // hypervisor boot leaves only cpu0 while PcdCoreCount is still 10.
    //
    // dt_get() returns NULL for a missing node and dt_node_prop(NULL, ...) walks
    // straight into dt_parse(NULL, ...), which reads node->nprop at offset 0.
    // That aborted the guest with FAR = 0x0 in _dt_parse+0x9c.
    //
    if (CpuNode == NULL) {
      DEBUG((DEBUG_INFO, "MemoryPeim: no %a node, skipping its carveout\n", CpuNodeName));
      continue;
    }

    UINT32 *Carveout = (UINT32 *)dt_node_prop(CpuNode, "cpm-impl-reg", &CarveoutLength);

    // dt_node_prop() only logs a missing property; it still returns NULL, and
    // the four words below are read unconditionally.
    if (Carveout == NULL || CarveoutLength < 4 * sizeof(UINT32)) {
      DEBUG((DEBUG_WARN, "MemoryPeim: %a has no usable cpm-impl-reg (len 0x%x), skipping\n",
             CpuNodeName, (UINT32)CarveoutLength));
      continue;
    }

    ReserveMemoryRegion (
      ((UINT64)Carveout[1] << 32) | Carveout[0],
      ((UINT64)Carveout[3] << 32) | Carveout[2]
    );
  }
#endif



  // Build Memory Allocation Hob
  InitMmu (MemoryTable);

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}

/**
 * BuildVirtualMemoryMap
 *
 * This will build up the memory map of the platform used to initialize the MMU and page tables.
 *
 * @param VirtualMemoryMap - A pointer to a pointer to be used for page table setup.
 *
 * does not return anything as the value will be stored in a pointer accessible by MemoryPeim.
 *
 */
VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap)
{
  ARM_MEMORY_REGION_ATTRIBUTES CacheAttributes;
  UINTN Index = 0;
  ARM_MEMORY_REGION_DESCRIPTOR *VirtualMemoryTable;

  //ensure we actually have a valid memory map pointer
  ASSERT(VirtualMemoryMap != NULL);

  DEBUG((DEBUG_INFO, "Allocating virtual memory table pages\n"));
  VirtualMemoryTable = (ARM_MEMORY_REGION_DESCRIPTOR *)AllocatePages (EFI_SIZE_TO_PAGES (sizeof (ARM_MEMORY_REGION_DESCRIPTOR) * MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS));
  if (VirtualMemoryTable == NULL) {
    DEBUG((DEBUG_INFO, "Unexpected failure to allocate VirtualMemoryTable\n"));
    return;
  }

  CacheAttributes = DDR_ATTRIBUTES_CACHED;

  /**
   * NOTE - On Apple silicon platforms, non PCIe MMIO regions *must* use nGnRnE mappings,
   * while all PCIe regions *must* use nGnRE mappings.
   * by default EDK2 sets up the MMIO as nGnRnE, good for core system devices
   * though we will need to add an attribute for nGnRE mappings at some point.
   *
   * TODO: add ARM_MEMORY_REGION_ATTRIBUTE_DEVICE_POSTED_WRITE
   **/

  //
  // MMIO - PMGR/AIC/core system peripherals, then PCIe.
  //
  // Ranges are derived from the J704 ADT (see the memory map header). Range 4
  // (0x380000000) is the one that matters for early bringup: it contains PMGR,
  // AIC, the watchdog, GPIO and UART0.
  //
#define MAP_DEVICE_RANGE(Base, Size)                                       \
  do {                                                                     \
    VirtualMemoryTable[Index].PhysicalBase = (Base);                       \
    VirtualMemoryTable[Index].VirtualBase  = (Base);                       \
    VirtualMemoryTable[Index].Length       = (Size);                       \
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE; \
    Index++;                                                               \
  } while (FALSE)

  // AIC3 and DockChannel only. No unported PCIe/USB MMIO is exposed.
  MAP_DEVICE_RANGE (0x301000000ULL, 0x1cc000);
  MAP_DEVICE_RANGE (0x308128000ULL, 0x8000);
#if defined(J873_WINDOWS) || defined(J804_WINDOWS_VUART)
  // The Windows carrier emulates this UART; never access its physical block.
  MAP_DEVICE_RANGE (0x331200000ULL, 0x4000);
#endif
#if defined(J804_WINDOWS)
  // J804 WD1 (/arm-io/wdt reg[0], apple,t8152-wdt), used only by the J804
  // reset library when it runs at EL2 with no carrier above it. Under the
  // carrier (EL1) resets go through PSCI and this page is never touched.
  MAP_DEVICE_RANGE (0x3082b0000ULL, 0x4000);
#endif

#undef MAP_DEVICE_RANGE

  //
  // The loop above leaves Index one past the last written entry, whereas the
  // code below expects it to point *at* it. Step back to re-establish that.
  //
  Index--;

  //System DRAM
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdSystemMemorySize);
  VirtualMemoryTable[Index].Attributes     = CacheAttributes;


  DEBUG ((
    DEBUG_ERROR,
    "%a: Dumping System DRAM Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n"
    "\tTop of system RAM: 0x%lX\n",
    __FUNCTION__,
    VirtualMemoryTable[Index].PhysicalBase,
    VirtualMemoryTable[Index].VirtualBase,
    VirtualMemoryTable[Index].Length,
    VirtualMemoryTable[Index].PhysicalBase + VirtualMemoryTable[Index].Length
    ));

  //Framebuffer
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdFrameBufferSize);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;

  // End of Table
  VirtualMemoryTable[++Index].PhysicalBase  = 0;
  VirtualMemoryTable[Index].VirtualBase     = 0;
  VirtualMemoryTable[Index].Length          = 0;
  VirtualMemoryTable[Index].Attributes      = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT((Index + 1) <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  *VirtualMemoryMap = VirtualMemoryTable;
}

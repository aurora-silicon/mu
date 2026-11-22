/* SPDX-License-Identifier: MIT */
/*
 * J813 G17 GPU device (Platform/J813BoardPkg/AcpiTables/GPU.asl).
 *
 * The SSDT is admitted only in images built with PcdAppleG17GpuProbe, on
 * chip 0x8142 / board 0x24, when every one of the six iBoot GPU regions in
 * /arm-io/sgx is well formed and lies wholly outside UEFI system memory (so
 * Windows can never allocate it). Their base/size then replace the six
 * placeholder QWordMemory descriptors. Any doubt withholds the device.
 *
 * The same rules admit the unified GPU+panel SSDT ("AURA817F", AURA8171)
 * when the host selected it (AuraJ813UnifiedDisplay.c), which then withholds
 * AURA817G; AURA8171 also needs dcp/disp0 in the ADT and keeps ranges 3-6
 * within the unified driver's 16 MiB ceiling.
 *
 * Images built with PcdAppleJ813GpuLocalPool publish AURA8171 from "AURA817P"
 * (GPUP.asl) instead: GPUF's sixteen resources plus the G17 local-memory pool
 * MemoryPeim reserved, as the seventeenth.  The PCD picks the template, so at
 * most one AURA8171 is ever installed (FAILURE-MODES-J813-GPU-POOL.md).
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/AuraJ813GpuPool.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include "AuraG17GpuDevice.h"
#include "AuraJ813UnifiedDisplay.h"

#define G17_REGIONS          6
#define G17_UNIFIED_MAXIMUM  0x1000000ULL   /* agxw_j813_resources.c: ranges 3-6 */
#define G17_PAGE             0x4000ULL
#define G17_PHYS_LIMIT       (1ULL << 42)
#define G17_DRAM_BASE        0x10000000000ULL
#define G17_PLACEHOLDER(n)   (0xA170000000000000ULL | ((UINT64)(n) << 36))
#define QWORD_TAG            0x8A
#define QWORD_LENGTH         0x2B
#define QWORD_MIN_OFFSET     14
#define QWORD_MAX_OFFSET     22
#define QWORD_LEN_OFFSET     38

STATIC CONST CHAR8  *mNames[G17_REGIONS] = {
  "gfx-handoff",          "gpu-region", "gfx-shared-region",
  "gfx-shared-l2-region", "gfx-data",   "gfx1-data"
};
/* Linux m5: handoff >= 0x648, ttbs >= 0x400, pagetables >= 0x44000; DATA exact. */
STATIC CONST UINT64  mMinimum[G17_REGIONS] = { 0x648, 0x400, 0x44000, G17_PAGE, 0x134000, 0x130000 };
STATIC CONST BOOLEAN  mExact[G17_REGIONS]   = { FALSE, FALSE, FALSE, FALSE, TRUE, TRUE };

STATIC
BOOLEAN
ReadU32 (
  IN  CONST CHAR8  *Node,
  IN  CONST CHAR8  *Name,
  OUT UINT32       *Value
  )
{
  CONST VOID  *Property;
  size_t      Length;

  Property = dt_get_prop (Node, Name, &Length);
  if ((Property == NULL) || (Length != sizeof (*Value))) {
    return FALSE;
  }

  CopyMem (Value, Property, sizeof (*Value));
  return TRUE;
}

STATIC
BOOLEAN
ReadU64 (
  IN  CONST CHAR8  *Name,
  OUT UINT64       *Value
  )
{
  CONST VOID  *Property;
  size_t      Length;

  Property = dt_get_prop ("/arm-io/sgx", Name, &Length);
  if ((Property == NULL) || (Length != sizeof (*Value))) {
    return FALSE;
  }

  CopyMem (Value, Property, sizeof (*Value));
  return TRUE;
}

STATIC
BOOLEAN
OverlapsSystemMemory (
  IN UINT64  Base,
  IN UINT64  Size
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Map;
  UINTN                            Count;
  UINTN                            Index;
  BOOLEAN                          Overlap = FALSE;

  if (EFI_ERROR (gDS->GetMemorySpaceMap (&Count, &Map))) {
    return TRUE;
  }

  for (Index = 0; Index < Count; Index++) {
    if ((Map[Index].GcdMemoryType != EfiGcdMemoryTypeSystemMemory) &&
        (Map[Index].GcdMemoryType != EfiGcdMemoryTypeMoreReliable) &&
        (Map[Index].GcdMemoryType != EfiGcdMemoryTypePersistent))
    {
      continue;
    }

    if ((Base < Map[Index].BaseAddress + Map[Index].Length) &&
        (Map[Index].BaseAddress < Base + Size))
    {
      Overlap = TRUE;
      break;
    }
  }

  FreePool (Map);
  return Overlap;
}

/* Every byte of [Base, Base + Size) is GCD Reserved memory: what MemoryPeim's
 * EFI_RESOURCE_MEMORY_RESERVED HOB becomes, and what the memory map Windows
 * is given reports as EfiReservedMemoryType. */
STATIC
BOOLEAN
WhollyReservedMemory (
  IN UINT64  Base,
  IN UINT64  Size
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Map;
  UINTN                            Count;
  UINTN                            Index;
  UINT64                           Covered;
  UINT64                           End;
  BOOLEAN                          Progress;

  if (EFI_ERROR (gDS->GetMemorySpaceMap (&Count, &Map))) {
    return FALSE;
  }

  Covered = Base;
  do {
    Progress = FALSE;
    for (Index = 0; Index < Count; Index++) {
      End = Map[Index].BaseAddress + Map[Index].Length;
      if ((Map[Index].GcdMemoryType == EfiGcdMemoryTypeReserved) &&
          (Map[Index].BaseAddress <= Covered) && (Covered < End))
      {
        Covered  = End;
        Progress = TRUE;
      }
    }
  } while (Progress && (Covered < Base + Size));

  FreePool (Map);
  return (BOOLEAN)(Covered >= Base + Size);
}

/* Replace the single descriptor whose minimum is placeholder n. */
STATIC
BOOLEAN
PatchDescriptor (
  IN OUT UINT8  *Bytes,
  IN     UINTN  Size,
  IN     UINTN  Index,
  IN     UINT64 Base,
  IN     UINT64 Length
  )
{
  UINT64  Placeholder = G17_PLACEHOLDER (Index);
  UINT64  Value;
  UINT64  Max;
  UINTN   Offset;
  UINTN   Found = 0;
  UINTN   Descriptor = 0;

  for (Offset = sizeof (EFI_ACPI_DESCRIPTION_HEADER) + QWORD_MIN_OFFSET;
       Offset + sizeof (UINT64) <= Size; Offset++)
  {
    CopyMem (&Value, Bytes + Offset, sizeof (Value));
    if (Value == Placeholder) {
      Found++;
      Descriptor = Offset - QWORD_MIN_OFFSET;
    }
  }

  if ((Found != 1) || (Descriptor + 3 + QWORD_LENGTH > Size) ||
      (Bytes[Descriptor] != QWORD_TAG) || (Bytes[Descriptor + 1] != QWORD_LENGTH) ||
      (Bytes[Descriptor + 2] != 0) || (Bytes[Descriptor + 3] != 0))
  {
    return FALSE;
  }

  Max = Base + Length - 1;
  CopyMem (Bytes + Descriptor + QWORD_MIN_OFFSET, &Base, sizeof (Base));
  CopyMem (Bytes + Descriptor + QWORD_MAX_OFFSET, &Max, sizeof (Max));
  CopyMem (Bytes + Descriptor + QWORD_LEN_OFFSET, &Length, sizeof (Length));
  return TRUE;
}

/* The G17 local-memory pool MemoryPeim reserved: exactly one HOB, of the v1
 * shape, 1 GiB with both ends on AURA_J813_GPU_POOL_ALIGNMENT, inside the
 * guest RAM MemoryPeim was given (the range Aura maps identity in its stage
 * 2) and inside 42-bit DRAM.  (The record is a multiple of 8 bytes, so
 * HobLib's rounding leaves its size exact.) */
STATIC
BOOLEAN
ReadGpuPool (
  OUT UINT64  *Base,
  OUT UINT64  *Size
  )
{
  STATIC CONST EFI_GUID  PoolGuid = AURA_J813_GPU_POOL_HOB_GUID;
  VOID                   *Hob;
  AURA_J813_GPU_POOL     Pool;

  Hob = GetFirstGuidHob (&PoolGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (Pool)) ||
      (GetNextGuidHob (&PoolGuid, GET_NEXT_HOB (Hob)) != NULL))
  {
    return FALSE;
  }

  CopyMem (&Pool, GET_GUID_HOB_DATA (Hob), sizeof (Pool));
  if ((Pool.Signature != AURA_J813_GPU_POOL_SIGNATURE) ||
      (Pool.Version != AURA_J813_GPU_POOL_VERSION) ||
      (Pool.StructureSize != sizeof (Pool)) || (Pool.Reserved != 0) ||
      (Pool.Size != AURA_J813_GPU_POOL_SIZE) ||
      ((Pool.Base & (AURA_J813_GPU_POOL_ALIGNMENT - 1)) != 0) ||
      (Pool.Base < G17_DRAM_BASE) || (Pool.Base >= G17_PHYS_LIMIT) ||
      (Pool.Size > G17_PHYS_LIMIT - Pool.Base) ||
      (Pool.GuestMemoryTop < Pool.GuestMemoryBase) ||
      (Pool.Base < Pool.GuestMemoryBase) ||
      (Pool.Size > Pool.GuestMemoryTop - Pool.GuestMemoryBase) ||
      (Pool.Base > Pool.GuestMemoryTop - Pool.Size))
  {
    return FALSE;
  }

  *Base = Pool.Base;
  *Size = Pool.Size;
  return TRUE;
}

BOOLEAN
AppleG17GpuTableAdmitted (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN     UINTN                        Size
  )
{
  UINT64  Base[G17_REGIONS];
  UINT64  Length[G17_REGIONS];
  UINT64  PoolBase;
  UINT64  PoolSize;
  UINT32  Value;
  UINTN   Index;
  UINTN   Other;
  BOOLEAN       Unified;
  BOOLEAN       Pooled;
  CONST CHAR8   *Hid;
  size_t        PropertyLength;

  if ((Size < sizeof (*Table)) || (Table->Length > Size)) {
    return FALSE;
  }

  if (Table->Signature != SIGNATURE_32 ('S', 'S', 'D', 'T')) {
    return TRUE;
  }

  Pooled  = CompareMem (&Table->OemTableId, "AURA817P", 8) == 0;
  Unified = Pooled || (CompareMem (&Table->OemTableId, "AURA817F", 8) == 0);
  if (!Unified && (CompareMem (&Table->OemTableId, "AURA817G", 8) != 0)) {
    return TRUE;
  }
  Hid = Unified ? "AURA8171" : "AURA8170";
  /* A unified selection never publishes either legacy device, even when a
   * subsequent new-table/ADT/region admission fails.  Without the host's
   * marker the unified table is withheld on every boot. */
  if (Unified != AppleJ813UnifiedDisplaySelected () ||
      (!Unified && !FixedPcdGetBool (PcdAppleG17GpuProbe))) {
    return FALSE;
  }
  /* The pool PCD picks the one unified template: GPUF without it, GPUP with
   * it.  The other is withheld without a word, so AURA8171 is installed at
   * most once, and a pool image never falls back to sixteen resources. */
  if (Unified && (Pooled != FixedPcdGetBool (PcdAppleJ813GpuLocalPool))) {
    return FALSE;
  }
  if (Unified &&
      ((dt_get_prop ("/arm-io/dcp", "compatible", &PropertyLength) == NULL) ||
       (PropertyLength == 0) ||
       (dt_get_prop ("/arm-io/disp0", "compatible", &PropertyLength) == NULL) ||
       (PropertyLength == 0))) {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: ADT has no dcp/disp0; AURA8171 withheld\n"));
    return FALSE;
  }

  if (!ReadU32 ("/chosen", "chip-id", &Value) || (Value != 0x8142) ||
      !ReadU32 ("/chosen", "board-id", &Value) || (Value != 0x24))
  {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: not J813; %a withheld\n", Hid));
    return FALSE;
  }

  for (Index = 0; Index < G17_REGIONS; Index++) {
    CHAR8  Name[48];

    AsciiSPrint (Name, sizeof (Name), "%a-base", mNames[Index]);
    if (!ReadU64 (Name, &Base[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: %a missing; %a withheld\n", Name, Hid));
      return FALSE;
    }

    AsciiSPrint (Name, sizeof (Name), "%a-size", mNames[Index]);
    if (!ReadU64 (Name, &Length[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: %a missing; %a withheld\n", Name, Hid));
      return FALSE;
    }

    if ((Length[Index] < mMinimum[Index]) ||
        (mExact[Index] && (Length[Index] != mMinimum[Index])) ||
        (Unified && !mExact[Index] && (Length[Index] > G17_UNIFIED_MAXIMUM)) ||
        (((Base[Index] | Length[Index]) & (G17_PAGE - 1)) != 0) ||
        (Base[Index] < G17_DRAM_BASE) || (Base[Index] >= G17_PHYS_LIMIT) ||
        (Length[Index] > G17_PHYS_LIMIT - Base[Index]) ||
        OverlapsSystemMemory (Base[Index], Length[Index]))
    {
      DEBUG ((
        DEBUG_ERROR,
        "Aura G17 GPU: %a 0x%lx+0x%lx rejected; %a withheld\n",
        mNames[Index],
        Base[Index],
        Length[Index],
        Hid
        ));
      return FALSE;
    }

    for (Other = 0; Other < Index; Other++) {
      if ((Base[Index] < Base[Other] + Length[Other]) &&
          (Base[Other] < Base[Index] + Length[Index]))
      {
        DEBUG ((DEBUG_ERROR, "Aura G17 GPU: %a overlaps %a\n", mNames[Index], mNames[Other]));
        return FALSE;
      }
    }
  }

  PoolBase = 0;
  PoolSize = 0;
  if (Pooled && !ReadGpuPool (&PoolBase, &PoolSize)) {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: no valid local-memory pool HOB; %a withheld\n", Hid));
    return FALSE;
  }

  /* Trust the HOB only as far as the GCD agrees: none of it may be memory
   * Windows is given, all of it must be Reserved.  The six regions lie
   * outside system memory too, so the pool is also compared with them. */
  if (Pooled &&
      (OverlapsSystemMemory (PoolBase, PoolSize) || !WhollyReservedMemory (PoolBase, PoolSize)))
  {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: pool 0x%lx+0x%lx is not wholly reserved memory; %a withheld\n",
            PoolBase, PoolSize, Hid));
    return FALSE;
  }

  for (Index = 0; Pooled && (Index < G17_REGIONS); Index++) {
    if ((PoolBase < Base[Index] + Length[Index]) && (Base[Index] < PoolBase + PoolSize)) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: pool overlaps %a; %a withheld\n", mNames[Index], Hid));
      return FALSE;
    }
  }

  for (Index = 0; Index < G17_REGIONS; Index++) {
    if (!PatchDescriptor ((UINT8 *)Table, Table->Length, Index, Base[Index], Length[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: template descriptor %u malformed\n", (UINT32)Index));
      return FALSE;
    }
  }

  /* The pool is the template's seventeenth descriptor, placeholder 6. */
  if (Pooled && !PatchDescriptor ((UINT8 *)Table, Table->Length, G17_REGIONS, PoolBase, PoolSize)) {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: pool descriptor malformed; %a withheld\n", Hid));
    return FALSE;
  }

  DEBUG ((DEBUG_INIT | DEBUG_INFO, "Aura G17 GPU: J813 %a resource owner\n",
          Unified ? "unified AURA8171" : "render AURA8170"));
  if (Pooled) {
    DEBUG ((DEBUG_INIT | DEBUG_INFO, "Aura G17 GPU: AURA8171 local-memory pool 0x%lx+0x%lx\n",
            PoolBase, PoolSize));
  }
  return TRUE;
}

/* SPDX-License-Identifier: MIT */
/*
 * J813 G17 GPU device (Platform/MacBookAir2026Pkg/AcpiTables/GPU.asl).
 *
 * The SSDT is admitted only in images built with PcdAppleG17GpuProbe, on
 * chip 0x8142 / board 0x24, when every one of the six iBoot GPU regions in
 * /arm-io/sgx is well formed and lies wholly outside UEFI system memory (so
 * Windows can never allocate it). Their base/size then replace the six
 * placeholder QWordMemory descriptors. Any doubt withholds the device.
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include "AuraG17GpuDevice.h"

#define G17_REGIONS          6
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

BOOLEAN
AppleG17GpuTableAdmitted (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN     UINTN                        Size
  )
{
  UINT64  Base[G17_REGIONS];
  UINT64  Length[G17_REGIONS];
  UINT32  Value;
  UINTN   Index;
  UINTN   Other;
  BOOLEAN Unified;
  size_t  PropertyLength;

  if ((Size < sizeof (*Table)) || (Table->Length > Size)) {
    return FALSE;
  }

  if (Table->Signature != SIGNATURE_32 ('S', 'S', 'D', 'T')) {
    return TRUE;
  }

  Unified = CompareMem (&Table->OemTableId, "AURA817F", 8) == 0;
  if (!Unified && (CompareMem (&Table->OemTableId, "AURA817G", 8) != 0)) {
    return TRUE;
  }
  /* A requested unified image never publishes either legacy device, even
   * when a subsequent new-table/ADT/region admission fails. */
  if (Unified != FixedPcdGetBool (PcdAppleJ813UnifiedDisplay) ||
      (!Unified && !FixedPcdGetBool (PcdAppleG17GpuProbe))) {
    return FALSE;
  }
  if (Unified &&
      ((dt_get_prop ("/arm-io/dcp", "compatible", &PropertyLength) == NULL) ||
       (PropertyLength == 0) ||
       (dt_get_prop ("/arm-io/disp0", "compatible", &PropertyLength) == NULL) ||
       (PropertyLength == 0))) {
    return FALSE;
  }

  if (!ReadU32 ("/chosen", "chip-id", &Value) || (Value != 0x8142) ||
      !ReadU32 ("/chosen", "board-id", &Value) || (Value != 0x24))
  {
    DEBUG ((DEBUG_ERROR, "Aura G17 GPU: not J813; AURA8170 withheld\n"));
    return FALSE;
  }

  for (Index = 0; Index < G17_REGIONS; Index++) {
    CHAR8  Name[48];

    AsciiSPrint (Name, sizeof (Name), "%a-base", mNames[Index]);
    if (!ReadU64 (Name, &Base[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: %a missing; AURA8170 withheld\n", Name));
      return FALSE;
    }

    AsciiSPrint (Name, sizeof (Name), "%a-size", mNames[Index]);
    if (!ReadU64 (Name, &Length[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: %a missing; AURA8170 withheld\n", Name));
      return FALSE;
    }

    if ((Length[Index] < mMinimum[Index]) ||
        (mExact[Index] && (Length[Index] != mMinimum[Index])) ||
        (((Base[Index] | Length[Index]) & (G17_PAGE - 1)) != 0) ||
        (Base[Index] < G17_DRAM_BASE) || (Base[Index] >= G17_PHYS_LIMIT) ||
        (Length[Index] > G17_PHYS_LIMIT - Base[Index]) ||
        OverlapsSystemMemory (Base[Index], Length[Index]))
    {
      DEBUG ((
        DEBUG_ERROR,
        "Aura G17 GPU: %a 0x%lx+0x%lx rejected; AURA8170 withheld\n",
        mNames[Index],
        Base[Index],
        Length[Index]
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

  for (Index = 0; Index < G17_REGIONS; Index++) {
    if (!PatchDescriptor ((UINT8 *)Table, Table->Length, Index, Base[Index], Length[Index])) {
      DEBUG ((DEBUG_ERROR, "Aura G17 GPU: template descriptor %u malformed\n", (UINT32)Index));
      return FALSE;
    }
  }

  DEBUG ((DEBUG_INFO, "Aura G17 GPU: J813 %a resource owner\n",
          Unified ? "unified AURA8171" : "render AURA8170"));
  return TRUE;
}

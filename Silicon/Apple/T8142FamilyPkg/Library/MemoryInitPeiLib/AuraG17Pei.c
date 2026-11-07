/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <PiPei.h>
#include <Library/AuraG17PreservationLib.h>
#include <Library/HobLib.h>
#include <Library/DebugLib.h>
#include "MemoryReservation.h"

// This reservation owns only EFI exclusion metadata, never firmware contents.
typedef struct {
  UINT64 UefiBase;
  UINT64 UefiSize;
} AURA_G17_PEI_CONTEXT;

STATIC int
AuraG17CheckHobs (void *Opaque, CONST struct aura_g17_region *Region)
{
  AURA_G17_PEI_CONTEXT *Context = Opaque;
  EFI_PEI_HOB_POINTERS Hob;
  UINT64 Base, Size, Top = Region->base + Region->size;
  UINTN Covers = 0;
  if (Region->size > MAX_UINT32 ||
      Context->UefiSize > MAX_UINT64 - Context->UefiBase ||
      (Region->base < Context->UefiBase + Context->UefiSize && Context->UefiBase < Top))
    return 0;
  for (Hob.Raw = GetHobList (); !END_OF_HOB_LIST (Hob); Hob.Raw = GET_NEXT_HOB (Hob)) {
    if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      Base = Hob.ResourceDescriptor->PhysicalStart;
      Size = Hob.ResourceDescriptor->ResourceLength;
      if (Size > MAX_UINT64 - Base) return 0;
      if (Region->base >= Base + Size || Base >= Top) continue;
      if (Region->classification != 1 ||
          Hob.ResourceDescriptor->ResourceType != EFI_RESOURCE_SYSTEM_MEMORY ||
          Base > Region->base || Base + Size < Top) return 0;
      ++Covers;
    } else if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
      Base = Hob.MemoryAllocation->AllocDescriptor.MemoryBaseAddress;
      Size = Hob.MemoryAllocation->AllocDescriptor.MemoryLength;
      if (Size > MAX_UINT64 - Base ||
          (Region->base < Base + Size && Base < Top)) return 0;
    }
  }
  return Region->classification == 1 ? Covers == 1 : Covers == 0;
}

STATIC int
AuraG17ReserveHob (void *Opaque, CONST struct aura_g17_region *Region)
{
  (VOID)Opaque;
  return ReserveMemoryRegion (Region->base, (UINT32)Region->size);
}

EFI_STATUS
ReserveG17Handoff (UINT64 UefiMemoryBase, UINT64 UefiMemorySize)
{
  STATIC EFI_GUID Guid = AURA_G17_HOB_GUID;
  CONST VOID *Record;
  UINTN Length;
  struct aura_g17_observation Actual;
  AURA_G17_PEI_CONTEXT Context = { UefiMemoryBase, UefiMemorySize };
  struct aura_g17_ops Ops = { &Context, AuraG17CheckHobs, AuraG17ReserveHob };
  Record = AuraG17Record (&Length);
  if (Record == NULL) return EFI_SUCCESS;
  if (!AuraG17Observe (&Actual) || !aura_g17_preserve (Record, Length, &Actual, &Ops)) {
    DEBUG ((DEBUG_ERROR, "Aura G17: preservation admission/reservation failed; stopping boot\n"));
    return EFI_COMPROMISED_DATA;
  }
  if (BuildGuidDataHob (&Guid, (VOID *)Record, Length) == NULL) return EFI_OUT_OF_RESOURCES;
  DEBUG ((DEBUG_INFO, "Aura G17: eight firmware spans excluded for whole boot; reservation-only\n"));
  return EFI_SUCCESS;
}


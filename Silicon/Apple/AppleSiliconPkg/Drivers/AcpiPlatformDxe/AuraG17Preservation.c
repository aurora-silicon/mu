/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <PiDxe.h>
#include <Library/AuraG17PreservationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC EFI_GUID mG17Guid = AURA_G17_HOB_GUID;
EFI_STATUS AuraG17VerifyPublishedReport (VOID);

STATIC EFI_STATUS
VerifyMap (CONST VOID *Record)
{
  EFI_MEMORY_DESCRIPTOR *Map = NULL, *Entry, *Previous;
  EFI_STATUS Status;
  UINTN Size = 0, Key, Stride = 0, Offset, PriorOffset, Attempt;
  UINT32 Version;
  UINT64 Covered[AURA_G17_REGIONS] = { 0 };
  UINT64 Base, End, Start, Stop;
  struct aura_g17_region Region;
  UINTN Index;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL || Stride < sizeof (*Map)) return EFI_COMPROMISED_DATA;
  for (Attempt = 0; Attempt < 4; ++Attempt) {
    if (Stride > MAX_UINTN / 16 || Size > MAX_UINTN - 16 * Stride) return EFI_OUT_OF_RESOURCES;
    Size += 16 * Stride;
    Map = AllocatePool (Size);
    if (Map == NULL) return EFI_OUT_OF_RESOURCES;
    Status = gBS->GetMemoryMap (&Size, Map, &Key, &Stride, &Version);
    if (Status != EFI_BUFFER_TOO_SMALL) break;
    FreePool (Map);
    Map = NULL;
  }
  if (EFI_ERROR (Status)) {
    if (Map != NULL) FreePool (Map);
    return Status;
  }
  if (Stride < sizeof (*Map) || Size % Stride != 0) {
    FreePool (Map);
    return EFI_COMPROMISED_DATA;
  }
  Status = EFI_SUCCESS;
  for (Offset = 0; Offset < Size; Offset += Stride) {
    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    Base = Entry->PhysicalStart;
    if ((Base & EFI_PAGE_MASK) != 0 || Entry->NumberOfPages == 0 ||
        Entry->NumberOfPages > (MAX_UINT64 - Base) / EFI_PAGE_SIZE) {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    End = Base + Entry->NumberOfPages * EFI_PAGE_SIZE;
    // Byte totals prove coverage only for disjoint intervals. EFI ordering is
    // not assumed; reject overlap even when duplicate coverage masks a hole.
    for (PriorOffset = 0; PriorOffset < Offset; PriorOffset += Stride) {
      Previous = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + PriorOffset);
      if (Base < Previous->PhysicalStart + Previous->NumberOfPages * EFI_PAGE_SIZE &&
          Previous->PhysicalStart < End) {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
    }
    if (EFI_ERROR (Status)) break;
    for (Index = 0; Index < AURA_G17_REGIONS; ++Index) {
      aura_g17_region (Record, Index, &Region);
      Start = MAX (Base, Region.base);
      Stop = MIN (End, Region.base + Region.size);
      if (Start >= Stop) continue;
      if (Entry->Type != EfiReservedMemoryType || Stop - Start > Region.size - Covered[Index]) {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
      Covered[Index] += Stop - Start;
    }
    if (EFI_ERROR (Status)) break;
  }
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < AURA_G17_REGIONS; ++Index) {
      aura_g17_region (Record, Index, &Region);
      if (Region.classification == 1 && Covered[Index] != Region.size) {
        Status = EFI_COMPROMISED_DATA;
        break;
      }
    }
  }
  FreePool (Map);
  return Status;
}

EFI_STATUS
AuraG17VerifyCurrentPreservation (VOID)
{
  CONST VOID *Record;
  VOID *Hob, *Next;
  UINTN Length;
  struct aura_g17_observation Actual;
  Record = AuraG17Record (&Length);
  Hob = GetFirstGuidHob (&mG17Guid);
  if (Record == NULL) return Hob == NULL ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  if (Hob == NULL || GET_GUID_HOB_DATA_SIZE (Hob) != AURA_G17_BYTES ||
      !AuraG17Observe (&Actual) || !aura_g17_validate (Record, Length, &Actual) ||
      CompareMem (Record, GET_GUID_HOB_DATA (Hob), AURA_G17_BYTES) != 0)
    return EFI_COMPROMISED_DATA;
  Next = GET_NEXT_HOB (Hob);
  if (GetNextGuidHob (&mG17Guid, Next) != NULL) return EFI_COMPROMISED_DATA;
  return VerifyMap (Record);
}

STATIC VOID EFIAPI
VerifyReadyToBoot (EFI_EVENT Event, VOID *Context)
{
  EFI_STATUS Status;
  (VOID)Event;
  (VOID)Context;
  Status = AuraG17VerifyCurrentPreservation ();
  if (!EFI_ERROR (Status)) Status = AuraG17VerifyPublishedReport ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Aura G17: ReadyToBoot exclusion verification failed: %r; stopping\n", Status));
    CpuDeadLoop ();
  }
  DEBUG ((DEBUG_INFO, "Aura G17: ReadyToBoot reserved/absent coverage verified for all eight spans\n"));
}

EFI_STATUS
AuraG17VerifyPreservation (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT Event;
  UINTN Length;
  Status = AuraG17VerifyCurrentPreservation ();
  if (EFI_ERROR (Status) || AuraG17Record (&Length) == NULL) return Status;
  Status = EfiCreateEventReadyToBootEx (TPL_CALLBACK, VerifyReadyToBoot, NULL, &Event);
  if (!EFI_ERROR (Status))
    DEBUG ((DEBUG_INFO, "Aura G17: DXE exclusion verification passed; retention remains whole-boot\n"));
  return Status;
}

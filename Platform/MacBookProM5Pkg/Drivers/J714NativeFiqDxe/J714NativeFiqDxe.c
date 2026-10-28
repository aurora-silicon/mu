// SPDX-License-Identifier: MIT
// Guarded pre-kernel FIQ patch, using the QEMU-tested instruction resolver.
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include "hv_windows_fiq_resolve.h"
#include "hv_windows_patch_enc.h"
#include "windows-native-wfi.h"
#include "windows-native-kernel-profile.h"

// Walk executable PE sections of the loaded kernel. Mode 0 counts DAIF #2/#3
// sites, 1 pairs every #2 site to #3, 2 restores #3 sites to #2.
STATIC BOOLEAN DaifPairWalk(UINT64 ImageBase, UINT32 ImageSize, UINTN Mode,
                            UINTN *Set2, UINTN *Clr2, UINTN *Any3,
                            CONST struct win_native_kernel_profile *Profile)
{
  CONST UINT8 *Image = (CONST UINT8 *)(UINTN)ImageBase;
  UINT32 Pe = *(CONST UINT32 *)(Image + 0x3c);
  if (Pe < 0x40 || Pe > 0xf00 || *(CONST UINT32 *)(Image + Pe) != 0x4550) return FALSE;
  UINT16 Sections = *(CONST UINT16 *)(Image + Pe + 6);
  UINT16 OptSize = *(CONST UINT16 *)(Image + Pe + 20);
  UINT32 Table = Pe + 24 + OptSize;
  if (Sections > 96 || Table + 40u * Sections > 0x1000) return FALSE;
  *Set2 = *Clr2 = *Any3 = 0;
  for (UINT16 Index = 0; Index < Sections; Index++) {
    CONST UINT8 *Header = Image + Table + 40u * Index;
    UINT32 VirtualSize = *(CONST UINT32 *)(Header + 8);
    UINT32 Rva = *(CONST UINT32 *)(Header + 12);
    UINT32 Flags = *(CONST UINT32 *)(Header + 36);
    if (!(Flags & 0x20000000)) continue;
    if (Rva >= ImageSize || VirtualSize > ImageSize - Rva) return FALSE;
    for (UINT32 Offset = Rva & ~3u; Offset + 4 <= Rva + VirtualSize; Offset += 4) {
      UINT32 *Word = (UINT32 *)(UINTN)(ImageBase + Offset);
      UINT32 Value = *Word;
      // The HV fingerprints the stock idle profile helper; it only runs
      // inside the paired idle boundary, so its two sites stay unchanged.
      BOOLEAN Keep = Offset >= Profile->prepare_rva &&
                     Offset < Profile->prepare_rva + WIN_NATIVE_WFI_PREPARE_SIZE;
      if (Value == WIN_DAIF_SET2 || Value == WIN_DAIF_CLR2) {
        if (Value == WIN_DAIF_SET2) (*Set2)++; else (*Clr2)++;
        if (Mode == 1 && !Keep) *Word = Value | 0x100; else continue;
      } else if (Value == WIN_DAIF_SET3 || Value == WIN_DAIF_CLR3) {
        (*Any3)++;
        if (Mode == 2) *Word = Value & ~0x100u;
      } else continue;
      if (Mode) {
        WriteBackDataCacheRange(Word, sizeof(*Word));
        InvalidateInstructionCacheRange(Word, sizeof(*Word));
      }
    }
  }
  return TRUE;
}

STATIC VOID DaifEditApply(UINT64 ImageBase, BOOLEAN Restore,
                          CONST struct win_native_kernel_profile *Profile)
{
  for (UINTN Index = 0; Index < Profile->edit_count; Index++) {
    CONST struct win_native_mask_edit *E = &Profile->edits[Index];
    VOID *At = (VOID *)(UINTN)(ImageBase + E->rva);
    UINT64 Value = Restore ? E->old : E->replacement;
    if (E->size == 8) *(UINT64 *)At = Value; else *(UINT32 *)At = (UINT32)Value;
    WriteBackDataCacheRange(At, E->size);
    InvalidateInstructionCacheRange(At, E->size);
  }
}

STATIC BOOLEAN DaifEditsMatch(UINT64 ImageBase,
                              CONST struct win_native_kernel_profile *Profile)
{
  for (UINTN Index = 0; Index < Profile->edit_count; Index++) {
    CONST struct win_native_mask_edit *E = &Profile->edits[Index];
    CONST VOID *At = (CONST VOID *)(UINTN)(ImageBase + E->rva);
    UINT64 Value = E->size == 8 ? *(CONST UINT64 *)At : *(CONST UINT32 *)At;
    if (Value != E->old) return FALSE;
  }
  return TRUE;
}

#define MAP_BUFFER_SIZE (128 * 1024)
STATIC VOID *mMap;
STATIC BOOLEAN mApplied;

STATIC UINT64 RegisterPatch(CONST struct hv_wp_resolved *T)
{
  register UINT64 X0 __asm__("x0") = 0x46514948;
  register UINT64 X1 __asm__("x1") = T->image_base;
  register UINT64 X2 __asm__("x2") = T->image_size;
  register UINT64 X3 __asm__("x3") = T->func - T->image_base;
  register UINT64 X4 __asm__("x4") = T->slot - T->image_base;
  register UINT64 X5 __asm__("x5") = T->vectors_rva;
  register UINT64 X6 __asm__("x6") = WIN_NATIVE_WFI_POLICY;
  __asm__ volatile("hvc #0xa519" : "+r"(X0), "+r"(X1), "+r"(X2),
                   "+r"(X3), "+r"(X4), "+r"(X5), "+r"(X6) :: "memory");
  return X0;
}

// Only read a candidate if every byte belongs to loader RAM. MMIO and
// reserved firmware memory are never candidates, even if they contain MZ.
STATIC BOOLEAN LoaderRange(UINT64 Start, UINT64 Size, UINTN MapSize, UINTN Step)
{
  UINT64 End, Cursor;
  UINTN Offset;
  EFI_MEMORY_DESCRIPTOR *D;
  if (!Size || Start > MAX_UINT64 - Size) return FALSE;
  End = Start + Size; Cursor = Start;
  while (Cursor < End) {
    BOOLEAN Found = FALSE;
    for (Offset = 0; Offset < MapSize; Offset += Step) {
      D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMap + Offset);
      if ((D->Type != EfiLoaderCode && D->Type != EfiLoaderData) ||
          D->NumberOfPages > (MAX_UINT64 - D->PhysicalStart) / EFI_PAGE_SIZE)
        continue;
      UINT64 Top = D->PhysicalStart + EFI_PAGES_TO_SIZE(D->NumberOfPages);
      if (D->PhysicalStart <= Cursor && Cursor < Top) {
        Cursor = MIN(Top, End); Found = TRUE; break;
      }
    }
    if (!Found) return FALSE;
  }
  return TRUE;
}

STATIC VOID EFIAPI PatchAtExit(EFI_EVENT Event, VOID *Context)
{
  UINTN Size = MAP_BUFFER_SIZE, Key, Step, Offset;
  UINT32 Version;
  EFI_STATUS Status;
  struct hv_wp_resolved Target = {0};
  BOOLEAN Found = FALSE;
  if (mApplied) return;
  Status = gBS->GetMemoryMap(&Size, mMap, &Key, &Step, &Version);
  if (EFI_ERROR(Status) || Step < sizeof(EFI_MEMORY_DESCRIPTOR)) {
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: memory map refused %r\n", Status));
    return;
  }
  for (Offset = 0; Offset < Size; Offset += Step) {
    EFI_MEMORY_DESCRIPTOR *D = (VOID *)((UINT8 *)mMap + Offset);
    if (D->Type != EfiLoaderCode && D->Type != EfiLoaderData) continue;
    // Loader descriptors may reside in high RAM banks. The EFI map and
    // LoaderRange remain the authority; a low-bootstrap upload boundary is
    // not an upper bound on Windows' kernel placement. Retain the bounded
    // descriptor scan and reject overflow / unsupported IPA widths.
    if (D->NumberOfPages > 0x60000 ||
        D->PhysicalStart >= 0x100000000000ULL ||
        EFI_PAGES_TO_SIZE(D->NumberOfPages) >
          0x100000000000ULL - D->PhysicalStart) continue;
    UINT64 Top = D->PhysicalStart + EFI_PAGES_TO_SIZE(D->NumberOfPages);
    for (UINT64 Base = D->PhysicalStart; Base + EFI_PAGE_SIZE <= Top; Base += EFI_PAGE_SIZE) {
      struct hv_wp_view Header = {(CONST UINT8 *)(UINTN)Base, Base, EFI_PAGE_SIZE};
      UINT32 ImageSize;
      UINT64 ImageBase;
      if (!hv_wp_find_image_base(&Header, Base, 0, &ImageBase, &ImageSize) ||
          ImageSize > 64 * 1024 * 1024 || !LoaderRange(Base, ImageSize, Size, Step))
        continue;
      struct hv_wp_view View = {(CONST UINT8 *)(UINTN)Base, Base, ImageSize};
      struct hv_wp_resolved Candidate;
      if (!hv_wp_resolve(&View, TRUE, &Candidate)) continue;
      if (Found) {
        DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: ambiguous kernel; no patch\n")); return;
      }
      Target = Candidate; Found = TRUE;
    }
  }
  if (!Found) {
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: no verified kernel; no patch\n")); return;
  }
  // A vector callback alone does not make stock idle unmask DAIF.F. Pair
  // the nine IRQ-only mask operations in this exact, fingerprinted loop.
  // All guards pass before any text write; unknown kernels keep the bridge.
  CONST struct win_native_kernel_profile *Profile = win_native_profile_find(
      (CONST UINT8 *)(UINTN)Target.image_base, Target.image_size,
      (UINT32)(Target.func - Target.image_base), (UINT32)(Target.slot - Target.image_base),
      Target.vectors_rva);
  if (!Profile ||
      win_native_wfi_hash((CONST VOID *)(UINTN)(Target.image_base + Profile->idle_rva),
                          Profile->idle_size) != Profile->idle_original_hash) {
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: kernel/idle mask fingerprint refused; no patch\n"));
    return;
  }
  if (*(CONST UINT64 *)(UINTN)(Target.image_base + Profile->idle_slot_rva) != 0 ||
      win_native_wfi_hash((CONST VOID *)(UINTN)(Target.image_base + Profile->wfi_rva),
                          WIN_NATIVE_WFI_SIZE) != Profile->wfi_hash ||
      win_native_wfi_hash((CONST VOID *)(UINTN)(Target.image_base + Profile->prepare_rva),
                          WIN_NATIVE_WFI_PREPARE_SIZE) != Profile->prepare_hash) {
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: native idle preservation fingerprint refused; no patch\n"));
    return;
  }
  UINTN Set2, Clr2, Any3;
  if (!DaifPairWalk(Target.image_base, Target.image_size, 0, &Set2, &Clr2, &Any3, Profile) ||
      Set2 != Profile->set2_count || Clr2 != Profile->clr2_count || Any3 != 0 ||
      !DaifEditsMatch(Target.image_base, Profile)) {
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: DAIF pairing fingerprint refused (%u/%u/%u); no patch\n",
           (UINT32)Set2, (UINT32)Clr2, (UINT32)Any3));
    return;
  }
  UINT32 IdleOriginal[WIN_NATIVE_WFI_SIZE / 4], IdleReplacement[WIN_NATIVE_WFI_SIZE / 4];
  CopyMem(IdleOriginal, (CONST VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), sizeof(IdleOriginal));
  win_native_wfi_build(IdleReplacement, Target.image_base + Profile->wfi_rva,
                       Target.image_base + Profile->idle_slot_rva, Target.func + 36);
  UINT32 Replacement[HV_WP_BODY_DWORDS];
  UINT32 Original[HV_WP_BODY_DWORDS + 1];
  CopyMem(Original, (VOID *)(UINTN)Target.func, sizeof(Original));
  hv_wp_build_replacement_into(Replacement, Target.func + 4, Target.slot, Target.bugcheck);
  // Pair every IRQ mask operation with FIQ (includes the nine KiIdleLoop
  // sites the HV fingerprints), then the computed exception-handler masks.
  DaifPairWalk(Target.image_base, Target.image_size, 1, &Set2, &Clr2, &Any3, Profile);
  DaifEditApply(Target.image_base, FALSE, Profile);
  CopyMem((VOID *)(UINTN)(Target.func + 4), Replacement, sizeof(Replacement));
  CopyMem((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), IdleReplacement, sizeof(IdleReplacement));
  WriteBackDataCacheRange((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), sizeof(IdleReplacement));
  InvalidateInstructionCacheRange((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), sizeof(IdleReplacement));
  WriteBackDataCacheRange((VOID *)(UINTN)(Target.func + 4), sizeof(Replacement));
  InvalidateInstructionCacheRange((VOID *)(UINTN)(Target.func + 4), sizeof(Replacement));
  UINT64 Result = RegisterPatch(&Target);
  if (Result != 0) {
    // Rejected handoff keeps the ordinary bridge and restores stock bytes.
    CopyMem((VOID *)(UINTN)Target.func, Original, sizeof(Original));
    CopyMem((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), IdleOriginal, sizeof(IdleOriginal));
    WriteBackDataCacheRange((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), sizeof(IdleOriginal));
    InvalidateInstructionCacheRange((VOID *)(UINTN)(Target.image_base + Profile->wfi_rva), sizeof(IdleOriginal));
    DaifPairWalk(Target.image_base, Target.image_size, 2, &Set2, &Clr2, &Any3, Profile);
    DaifEditApply(Target.image_base, TRUE, Profile);
    WriteBackDataCacheRange((VOID *)(UINTN)Target.func, sizeof(Original));
    InvalidateInstructionCacheRange((VOID *)(UINTN)Target.func, sizeof(Original));
    DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: HV rejected patch metadata %lx\n", Result));
    return;
  }
  mApplied = TRUE;
  DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: paired IRQ/FIQ masks at %u+%u sites and %u exception masks; guarded native WFI callback\n",
         (UINT32)Set2, (UINT32)Clr2, Profile->edit_count));
  DEBUG((DEBUG_ERROR, "NATIVE_FIQ_EBS: Mu patched kernel=%lx function=%lx slot=%lx vectors=%x\n",
         Target.image_base, Target.func, Target.slot, Target.vectors_rva));
}

EFI_STATUS EFIAPI J714NativeFiqEntry(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_EVENT Event;
  mMap = AllocatePool(MAP_BUFFER_SIZE);
  if (!mMap) return EFI_OUT_OF_RESOURCES;
  return gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, PatchAtExit, NULL,
                          &gEfiEventExitBootServicesGuid, &Event);
}

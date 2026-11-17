/** @file
  J804 (MacBook Pro 14-inch, M6) first-milestone ("M1") firmware parameters.

  The M1 image is the J873 native profile started on J804 by aura's
  chainload.py --raw at EL2 on the boot CPU, with a launcher that lowers
  boot_args.mem_size so that Mu's RAM ends below the carve-outs at the top of
  DRAM. These definitions are only used when the build sets J804_M1.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_M1_H_
#define J804_M1_H_

//
// Highest RAM top (phys_base + mem_size) M1 accepts on J804. It is the base of
// the /chosen/carveout-memory-map entry region-id-218 in the J804 device tree.
// PrePi places its stack and UEFI region directly below the RAM top and writes
// there before any reservation exists, so only a lower top (set by the
// launcher) keeps that carve-out intact. An untrimmed launch must stop here.
//
// No integer suffix: PrePi's ModuleEntryPoint.S uses it too.
//
#define J804_M1_RAM_TOP_LIMIT  0x1059d600000

//
// /chosen/carveout-memory-map entries with special handling: region-id-24 is
// the usable DRAM range itself; region-id-353 starts at phys_base and holds
// the loader image, its heap (where the relocated FD and its firmware block
// live) and the boot blobs.
//
#define J804_M1_USABLE_RANGE_REGION   "region-id-24"
#define J804_M1_LOADER_REGION         "region-id-353"

//
// Fatal stop codes, shown as the position of a striped block in the panel
// error row (PanelCheckpointLib).
//
#define J804_M1_ERROR_RAM_TOP_UNTRIMMED    0  // RAM top above J804_M1_RAM_TOP_LIMIT
#define J804_M1_ERROR_UEFI_REGION_OVERLAP  1  // a reservation reaches the UEFI region/stack
#define J804_M1_ERROR_TOO_MANY_RANGES      2  // reservation table full
#define J804_M1_ERROR_NO_CARVEOUT_MAP      3  // ADT has no /chosen/carveout-memory-map
#define J804_M1_ERROR_NOT_J804             4  // ADT does not describe a J804
#define J804_M1_ERROR_BAD_ADT              5  // ADT size unusable or ADT fails dt_check
#define J804_M1_ERROR_BOOT_HANDOFF         6  // boot_args/ADT copy fails J804BootHandoffIsSane
//
// Codes 0, 1, 3, 4 and 5 can come from PrePi EarlySetup (before the stack and
// UEFI region exist); 1, 2, 3 and 6 from MemoryInitPeiLib. When the FD has no
// Linux header, or EarlySetup somehow returns, ModuleEntryPoint.S halts in a
// branch-to-self loop without a block (gMuCheckpoint stage 0xEE).
//

//
// Largest device tree PrePi copies to PcdAdtPointer: the size MemoryInitPeiLib
// reserves there.
//
#define J804_M1_ADT_COPY_MAX  0x200000

#if !defined (__ASSEMBLER__)

#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>

/**
  Return TRUE if the device tree at PcdAdtPointer describes a J804: its root
  "target-type" is "J804", or its root "compatible" list starts with "J804".
  Uses only 32-bit-or-narrower reads, so it is safe with the MMU off.
**/
STATIC inline
BOOLEAN
J804M1AdtIsJ804 (
  VOID
  )
{
  dt_node_t   *Root;
  CONST CHAR8 *Value;
  size_t      Length;

  Root   = (dt_node_t *)(UINTN)FixedPcdGet64 (PcdAdtPointer);
  Length = 0;
  Value  = dt_prop (Root, "target-type", &Length);
  if ((Value != NULL) && (Length >= 5) &&
      (Value[0] == 'J') && (Value[1] == '8') && (Value[2] == '0') &&
      (Value[3] == '4') && (Value[4] == '\0'))
  {
    return TRUE;
  }

  Length = 0;
  Value  = dt_prop (Root, "compatible", &Length);
  if ((Value != NULL) && (Length >= 5) &&
      (Value[0] == 'J') && (Value[1] == '8') && (Value[2] == '0') &&
      (Value[3] == '4'))
  {
    return TRUE;
  }

  return FALSE;
}

//
// Device tree values are only 4-byte aligned and PrePi runs with the MMU off,
// so read 64-bit cells as two 32-bit halves.
//
STATIC inline
UINT64
J804M1DtCell64 (
  IN CONST VOID  *Value
  )
{
  CONST volatile UINT32  *Words;

  Words = (CONST volatile UINT32 *)Value;
  return (UINT64)Words[0] | ((UINT64)Words[1] << 32);
}

/**
  The UEFI region PrePi's ModuleEntryPoint.S places its stack, HOB list and
  early allocations in, for a given RAM range and FD position: the
  UefiRegionSize bytes below the RAM top, or below the FD base when the gap
  between the FD top and the RAM top is smaller than that.
**/
STATIC inline
VOID
J804M1UefiRegion (
  IN  UINT64  RamBase,
  IN  UINT64  RamTop,
  IN  UINT64  FdBase,
  IN  UINT64  FdSize,
  IN  UINT64  UefiRegionSize,
  OUT UINT64  *RegionBase,
  OUT UINT64  *RegionTop
  )
{
  UINT64  Top;

  Top = RamTop;
  if ((FdBase + FdSize <= RamTop) && (RamTop - (FdBase + FdSize) < UefiRegionSize)) {
    Top = FdBase + 1;   // as ModuleEntryPoint.S computes it
  }

  *RegionTop  = Top;
  *RegionBase = (Top > UefiRegionSize) ? Top - UefiRegionSize : 0;
}

typedef struct {
  UINT64         RamBase;
  UINT64         RamTop;
  UINT64         RegionBase;
  UINT64         RegionTop;
  BOOLEAN        Hit;
  UINT64         HitBase;
  UINT64         HitTop;
  CONST CHAR8    *HitKey;
} J804_M1_REGION_CHECK;

//
// dt_parse property callback: record the first /chosen/carveout-memory-map or
// /chosen/memory-map entry that overlaps [RegionBase, RegionTop). The usable
// range (region-id-24, or any carve-out spanning all of RAM) is not a hole,
// matching the MemoryInitPeiLib rules.
//
STATIC inline
int
J804M1RegionCheckCallback (
  IN VOID         *Context,
  IN dt_node_t    *Node,
  IN int          Depth,
  IN CONST CHAR8  *Key,
  IN VOID         *Value,
  IN size_t       Length
  )
{
  J804_M1_REGION_CHECK  *Check;
  UINT64                Base;
  UINT64                Size;
  UINT64                Top;

  Check = Context;
  if (Check->Hit || (Length < 2 * sizeof (UINT64))) {
    return 0;
  }

  if (AsciiStrnCmp (Key, J804_M1_USABLE_RANGE_REGION, DT_KEY_LEN) == 0) {
    return 0;
  }

  Base = J804M1DtCell64 (Value);
  Size = J804M1DtCell64 ((CONST UINT8 *)Value + sizeof (UINT64));
  if ((Size == 0) || (Base == MAX_UINT64) || (Size == MAX_UINT64)) {
    return 0;
  }

  Top = Base + Size;
  if (Top < Base) {
    Top = MAX_UINT64;
  }

  if ((AsciiStrnCmp (Key, "region-id-", 10) == 0) &&
      (Base <= Check->RamBase) && (Top >= Check->RamTop))
  {
    return 0;
  }

  if ((Base < Check->RegionTop) && (Top > Check->RegionBase)) {
    Check->Hit     = TRUE;
    Check->HitBase = Base;
    Check->HitTop  = Top;
    Check->HitKey  = Key;
  }

  return 0;
}

STATIC inline
VOID
J804M1RegionCheckFixed (
  IN OUT J804_M1_REGION_CHECK  *Check,
  IN     UINT64                Base,
  IN     UINT64                Size,
  IN     CONST CHAR8           *Key
  )
{
  if (!Check->Hit && (Base < Check->RegionTop) && (Base + Size > Check->RegionBase)) {
    Check->Hit     = TRUE;
    Check->HitBase = Base;
    Check->HitTop  = Base + Size;
    Check->HitKey  = Key;
  }
}

/**
  Return TRUE if [RegionBase, RegionTop) overlaps none of: the FD, the
  boot_args copy (16 KiB) and device tree copy (2 MiB) PrePi has just made,
  any /chosen/carveout-memory-map entry other than the usable range, and any
  /chosen/memory-map entry. FALSE also when the carve-out map is missing. On
  FALSE, Check says what was hit (HitKey is NULL when the map is missing).
**/
STATIC inline
BOOLEAN
J804M1RegionIsClear (
  IN  UINT64                RamBase,
  IN  UINT64                RamTop,
  IN  UINT64                RegionBase,
  IN  UINT64                RegionTop,
  IN  UINT64                FdBase,
  IN  UINT64                FdSize,
  IN  UINT64                BootArgsCopy,
  IN  UINT64                AdtCopy,
  OUT J804_M1_REGION_CHECK  *Check
  )
{
  dt_node_t  *Carveouts;
  dt_node_t  *MemoryMap;

  Check->RamBase    = RamBase;
  Check->RamTop     = RamTop;
  Check->RegionBase = RegionBase;
  Check->RegionTop  = RegionTop;
  Check->Hit        = FALSE;
  Check->HitBase    = 0;
  Check->HitTop     = 0;
  Check->HitKey     = NULL;

  Carveouts = dt_get ("/chosen/carveout-memory-map");
  MemoryMap = dt_get ("/chosen/memory-map");
  if (Carveouts == NULL) {
    return FALSE;
  }

  J804M1RegionCheckFixed (Check, FdBase, FdSize, "FD");
  J804M1RegionCheckFixed (Check, BootArgsCopy, 0x4000, "boot_args copy");
  J804M1RegionCheckFixed (Check, AdtCopy, J804_M1_ADT_COPY_MAX, "ADT copy");
  dt_parse (Carveouts, -1, NULL, NULL, NULL, J804M1RegionCheckCallback, Check);
  if (MemoryMap != NULL) {
    dt_parse (MemoryMap, -1, NULL, NULL, NULL, J804M1RegionCheckCallback, Check);
  }

  return !Check->Hit;
}

/**
  Validate the launcher-placed boot hand-off copies before MemoryInitPeiLib
  reserves them: the boot_args copy at BootArgsCopy (one 16 KiB page) and the
  ADT copy at AdtCopy (J804_M1_ADT_COPY_MAX bytes, holding DevtreeSize bytes).

  Adapted from ReserveBootHandoff in aurora-silicon/mu codex/windows-mu-handoff
  340f15e (T8142FamilyPkg MemoryInitPeiLib, BSD-2-Clause-Patent), rewritten as
  a pure check for the J804 reservation model, where the reservations
  themselves are added to the merged J804 range table. It fails closed on
  every condition that ReserveBootHandoff rejects:
    - a copy that is not page aligned, wraps, or is not wholly inside RAM;
    - an ADT copy that starts inside the boot_args copy;
    - an ADT size of zero or above the reserved copy size;
    - a copy that overlaps the FD (the J804 range table would otherwise cut
      the FD out of the reservation silently) or the UEFI region at the top
      of RAM.

  Returns TRUE when both copies are sane.
**/
STATIC inline
BOOLEAN
J804BootHandoffIsSane (
  IN UINT64  RamBase,
  IN UINT64  RamTop,
  IN UINT64  FdBase,
  IN UINT64  FdTop,
  IN UINT64  UefiBase,
  IN UINT64  UefiTop,
  IN UINT64  BootArgsCopy,
  IN UINT64  AdtCopy,
  IN UINT64  DevtreeSize
  )
{
  CONST UINT64  BootArgsSize = 0x4000;
  CONST UINT64  AdtSize      = J804_M1_ADT_COPY_MAX;
  CONST UINT64  PageMask     = 0x3FFF;
  UINT64        BootArgsTop;
  UINT64        AdtTop;

  if ((RamTop <= RamBase) || (FdTop < FdBase) || (UefiTop < UefiBase)) {
    return FALSE;
  }

  if (((BootArgsCopy & PageMask) != 0) || ((AdtCopy & PageMask) != 0)) {
    return FALSE;
  }

  if ((BootArgsCopy > MAX_UINT64 - BootArgsSize) || (AdtCopy > MAX_UINT64 - AdtSize)) {
    return FALSE;
  }

  BootArgsTop = BootArgsCopy + BootArgsSize;
  AdtTop      = AdtCopy + AdtSize;
  if ((BootArgsCopy < RamBase) || (BootArgsTop > RamTop) ||
      (AdtCopy < RamBase) || (AdtTop > RamTop))
  {
    return FALSE;
  }

  if ((AdtCopy < BootArgsTop) && (BootArgsCopy < AdtTop)) {
    return FALSE;
  }

  if ((DevtreeSize == 0) || (DevtreeSize > AdtSize)) {
    return FALSE;
  }

  if (((BootArgsCopy < FdTop) && (FdBase < BootArgsTop)) ||
      ((AdtCopy < FdTop) && (FdBase < AdtTop)))
  {
    return FALSE;
  }

  if (((BootArgsCopy < UefiTop) && (UefiBase < BootArgsTop)) ||
      ((AdtCopy < UefiTop) && (UefiBase < AdtTop)))
  {
    return FALSE;
  }

  return TRUE;
}

#endif // !__ASSEMBLER__

#endif

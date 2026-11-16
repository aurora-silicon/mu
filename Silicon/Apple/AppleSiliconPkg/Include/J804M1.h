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

#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>

//
// Highest RAM top (phys_base + mem_size) M1 accepts on J804. It is the base of
// the /chosen/carveout-memory-map entry region-id-218 in the J804 device tree.
// PrePi places its stack and UEFI region directly below the RAM top and writes
// there before any reservation exists, so only a lower top (set by the
// launcher) keeps that carve-out intact. An untrimmed launch must stop here.
//
#define J804_M1_RAM_TOP_LIMIT  0x1059d600000ULL

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

#endif

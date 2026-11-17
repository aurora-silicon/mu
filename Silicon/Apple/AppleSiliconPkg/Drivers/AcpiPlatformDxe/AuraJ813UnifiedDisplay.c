/* SPDX-License-Identifier: MIT */
/*
 * J813 display ownership selection.
 *
 * The legacy selection (AURA8170 render GPU + AURA8180 DCP) is the default.
 * The unified selection (AURA8171, both resource sets in one device) needs an
 * image built with PcdAppleJ813UnifiedDisplay and the host's
 * aura,windows-unified-display-v1 marker in /chosen holding exactly a 4-byte
 * little-endian 1.  Anything else keeps the legacy selection.  The ADT is
 * read once and the answer latched, so the display table (stored first) and
 * the two GPU tables can never disagree.
 */
#include <Uefi.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>

#include "AuraJ813UnifiedDisplay.h"

STATIC BOOLEAN  mSelectionLatched;
STATIC BOOLEAN  mUnifiedSelected;

STATIC
BOOLEAN
ReadSelection (
  VOID
  )
{
  dt_node_t   *Chosen;
  CONST VOID  *Property;
  size_t      Length;
  UINT32      Value;

  /* dt_prop, not dt_get_prop: an absent marker is the normal default and
   * must not print a "Missing DeviceTree prop" warning on every boot. */
  Chosen   = dt_get ("/chosen");
  Length   = 0;
  Property = (Chosen != NULL) ?
             dt_prop (Chosen, AURA_J813_UNIFIED_DISPLAY_MARKER, &Length) : NULL;
  if (Property == NULL) {
    DEBUG ((DEBUG_INIT | DEBUG_INFO, "Aura display: no unified marker; legacy AURA8170/AURA8180 selection\n"));
    return FALSE;
  }

  if (Length != sizeof (Value)) {
    DEBUG ((DEBUG_WARN, "Aura display: unified marker is %u bytes, not 4; legacy selection\n", (UINT32)Length));
    return FALSE;
  }

  CopyMem (&Value, Property, sizeof (Value));
  if (Value != 1) {
    DEBUG ((DEBUG_WARN, "Aura display: unified marker value %u, not 1; legacy selection\n", Value));
    return FALSE;
  }

  if (!FixedPcdGetBool (PcdAppleJ813UnifiedDisplay)) {
    DEBUG ((DEBUG_WARN, "Aura display: unified marker ignored, image not built for it; legacy selection\n"));
    return FALSE;
  }

  DEBUG ((DEBUG_INIT | DEBUG_INFO, "Aura display: unified marker set; AURA8170/AURA8180 withheld, AURA8171 selected\n"));
  return TRUE;
}

BOOLEAN
AppleJ813UnifiedDisplaySelected (
  VOID
  )
{
  if (!mSelectionLatched) {
    mUnifiedSelected  = ReadSelection ();
    mSelectionLatched = TRUE;
  }

  return mUnifiedSelected;
}

/* SPDX-License-Identifier: MIT */
#ifndef AURA_J813_UNIFIED_DISPLAY_H_
#define AURA_J813_UNIFIED_DISPLAY_H_

#include <Uefi.h>

/* Host-written /chosen property that selects the unified GPU+panel device
 * (AURA8171, SSDT AURA817F) instead of AURA8170 + AURA8180.  Exactly four
 * bytes, little-endian 1.  See Docs/j813-unified-display-marker.md. */
#define AURA_J813_UNIFIED_DISPLAY_MARKER  "aura,windows-unified-display-v1"

/* Read once and latched: every later table sees the same selection. */
BOOLEAN
AppleJ813UnifiedDisplaySelected (
  VOID
  );

#endif

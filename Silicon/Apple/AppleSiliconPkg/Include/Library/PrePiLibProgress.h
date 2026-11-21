/** @file
  Progress callback for the PrePiLib override.

  The override at AppleSiliconPkg/Override/EmbeddedPkg/Library/PrePiLib
  reports stages 0x20..0x26 while it locates, loads and enters the DXE core.
  These declarations used to be added to EmbeddedPkg/Include/Library/PrePiLib.h
  in a fork of mu_basecore; an override cannot replace a public header, so
  they live here and both the override and AppleSiliconPkg/PrePi include it.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef PRE_PI_LIB_PROGRESS_H_
#define PRE_PI_LIB_PROGRESS_H_

#include <Base.h>

typedef
VOID
(EFIAPI *PRE_PI_PROGRESS_CALLBACK)(
  IN UINT64  Stage
  );

VOID
EFIAPI
SetPrePiProgressCallback (
  IN PRE_PI_PROGRESS_CALLBACK  ProgressCallback OPTIONAL
  );

#endif

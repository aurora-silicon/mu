/** @file
  J804 windows profile, G6-S: parse the launcher's /chosen scanout handshake
  property (J804_SCANOUT_ADT_PROPERTY, layout in J804Windows.h).

  Header-only and free of UEFI services so the same code runs in
  J804GpuAcpiDxe and in the host test (Tools/HostTests/J804ScanoutHandshake).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_SCANOUT_HANDSHAKE_H_
#define J804_SCANOUT_HANDSHAKE_H_

#include <J804Windows.h>

typedef struct {
  UINT64    Base;
  UINT32    Size;
  UINT32    Gsiv;
  UINT32    Features;
  UINT32    RefreshMhz;
  UINT32    Version;
} J804_SCANOUT_HANDSHAKE;

typedef enum {
  J804ScanoutHandshakeOk = 0,
  J804ScanoutHandshakeBadLength,
  J804ScanoutHandshakeBadPage,
  J804ScanoutHandshakeBadGsiv,
  J804ScanoutHandshakeBadVersion,
  J804ScanoutHandshakeBadFeatures,
  J804ScanoutHandshakeBadRefresh,
  J804ScanoutHandshakeBadReserved
} J804_SCANOUT_HANDSHAKE_STATUS;

#define J804_SCANOUT_HANDSHAKE_STATUS_TEXT \
  "ok", "bad length", "page differs from the contract", "GSIV differs from the contract", \
  "unknown version", "features lack the timer vblank or the copy flip, or carry unknown bits", \
  "refresh outside 1..240 Hz", "reserved word not zero"

STATIC
UINT64
J804ScanoutLe (
  IN CONST UINT8  *Bytes,
  IN UINTN        Count
  )
{
  UINT64  Value;

  Value = 0;
  while (Count > 0) {
    Count--;
    Value = (Value << 8) | Bytes[Count];
  }

  return Value;
}

/**
  Decode and check the property value. The page, size and GSIV must equal the
  contract in J804Windows.h, the version must be J804_SCANOUT_VERSION, the
  features must carry the required bits and nothing unknown, the refresh must
  be in range and the reserved word zero. On any failure Out is zeroed and GPU0
  must not carry the scanout resources.
**/
STATIC
J804_SCANOUT_HANDSHAKE_STATUS
J804ScanoutParseHandshake (
  IN  CONST VOID              *Value,
  IN  UINTN                   Length,
  OUT J804_SCANOUT_HANDSHAKE  *Out
  )
{
  CONST UINT8             *Bytes;
  J804_SCANOUT_HANDSHAKE  Parsed;
  UINTN                   Index;

  for (Index = 0; Index < sizeof (*Out); Index++) {
    ((UINT8 *)Out)[Index] = 0;
  }

  if ((Value == NULL) || (Length != J804_SCANOUT_ADT_PROPERTY_SIZE)) {
    return J804ScanoutHandshakeBadLength;
  }

  Bytes             = (CONST UINT8 *)Value;
  Parsed.Base       = J804ScanoutLe (Bytes + 0x00, 8);
  Parsed.Size       = (UINT32)J804ScanoutLe (Bytes + 0x08, 4);
  Parsed.Gsiv       = (UINT32)J804ScanoutLe (Bytes + 0x0C, 4);
  Parsed.Features   = (UINT32)J804ScanoutLe (Bytes + 0x10, 4);
  Parsed.RefreshMhz = (UINT32)J804ScanoutLe (Bytes + 0x14, 4);
  Parsed.Version    = (UINT32)J804ScanoutLe (Bytes + 0x18, 4);

  if ((Parsed.Base != J804_SCANOUT_BASE) || (Parsed.Size != J804_SCANOUT_SIZE)) {
    return J804ScanoutHandshakeBadPage;
  }

  if (Parsed.Gsiv != J804_GSIV_SCANOUT) {
    return J804ScanoutHandshakeBadGsiv;
  }

  if (Parsed.Version != J804_SCANOUT_VERSION) {
    return J804ScanoutHandshakeBadVersion;
  }

  if (((Parsed.Features & J804_SCANOUT_FEATURES_REQUIRED) != J804_SCANOUT_FEATURES_REQUIRED) ||
      ((Parsed.Features & ~(UINT32)J804_SCANOUT_FEATURES_KNOWN) != 0))
  {
    return J804ScanoutHandshakeBadFeatures;
  }

  if ((Parsed.RefreshMhz < J804_SCANOUT_MIN_MHZ) || (Parsed.RefreshMhz > J804_SCANOUT_MAX_MHZ)) {
    return J804ScanoutHandshakeBadRefresh;
  }

  if (J804ScanoutLe (Bytes + 0x1C, 4) != 0) {
    return J804ScanoutHandshakeBadReserved;
  }

  *Out = Parsed;
  return J804ScanoutHandshakeOk;
}

#endif

/** @file
  J804 windows profile, I0: parse the launcher's /chosen input handshake
  property (J804_INPUT_ADT_PROPERTY, layout in J804Windows.h).

  Header-only and free of UEFI services so the same code runs in
  J804InputAcpiDxe and in the host test (Tools/HostTests/J804InputHandshake).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_INPUT_HANDSHAKE_H_
#define J804_INPUT_HANDSHAKE_H_

#include <J804Windows.h>

typedef struct {
  UINT64    Base;
  UINT32    Size;
  UINT32    Gsiv;
  UINT32    Features;
  UINT32    Version;
} J804_INPUT_HANDSHAKE;

typedef enum {
  J804InputHandshakeOk = 0,
  J804InputHandshakeBadLength,
  J804InputHandshakeBadPage,
  J804InputHandshakeBadGsiv,
  J804InputHandshakeBadVersion,
  J804InputHandshakeBadReports,
  J804InputHandshakeBadReserved,
  J804InputHandshakeNoSource
} J804_INPUT_HANDSHAKE_STATUS;

//
// Feature bits that must be clear: 4-15 (PTP and later collections) and
// 24-31.
//
#define J804_INPUT_FEATURES_RESERVED  \
  (~(UINT32)(J804_INPUT_FEATURES_REPORTS | J804_INPUT_FEATURES_SOURCES))

STATIC
UINT64
J804InputLe (
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
  contract in J804Windows.h, the version must be J804_INPUT_VERSION, all four
  report bits must be set, the reserved feature bits clear, and at least one
  source bit set. On any failure Out is zeroed and no INP0 may be published.
**/
STATIC
J804_INPUT_HANDSHAKE_STATUS
J804InputParseHandshake (
  IN  CONST VOID            *Value,
  IN  UINTN                 Length,
  OUT J804_INPUT_HANDSHAKE  *Out
  )
{
  CONST UINT8           *Bytes;
  J804_INPUT_HANDSHAKE  Parsed;
  UINTN                 Index;

  for (Index = 0; Index < sizeof (*Out); Index++) {
    ((UINT8 *)Out)[Index] = 0;
  }

  if ((Value == NULL) || (Length != J804_INPUT_ADT_PROPERTY_SIZE)) {
    return J804InputHandshakeBadLength;
  }

  Bytes           = (CONST UINT8 *)Value;
  Parsed.Base     = J804InputLe (Bytes + 0x00, 8);
  Parsed.Size     = (UINT32)J804InputLe (Bytes + 0x08, 4);
  Parsed.Gsiv     = (UINT32)J804InputLe (Bytes + 0x0C, 4);
  Parsed.Features = (UINT32)J804InputLe (Bytes + 0x10, 4);
  Parsed.Version  = (UINT32)J804InputLe (Bytes + 0x14, 4);

  if ((Parsed.Base != J804_INPUT_BASE) || (Parsed.Size != J804_INPUT_SIZE)) {
    return J804InputHandshakeBadPage;
  }

  if (Parsed.Gsiv != J804_GSIV_INPUT) {
    return J804InputHandshakeBadGsiv;
  }

  if (Parsed.Version != J804_INPUT_VERSION) {
    return J804InputHandshakeBadVersion;
  }

  if ((Parsed.Features & J804_INPUT_FEATURES_REPORTS) != J804_INPUT_FEATURES_REPORTS) {
    return J804InputHandshakeBadReports;
  }

  if ((Parsed.Features & J804_INPUT_FEATURES_RESERVED) != 0) {
    return J804InputHandshakeBadReserved;
  }

  if ((Parsed.Features & J804_INPUT_FEATURES_SOURCES) == 0) {
    return J804InputHandshakeNoSource;
  }

  *Out = Parsed;
  return J804InputHandshakeOk;
}

#endif

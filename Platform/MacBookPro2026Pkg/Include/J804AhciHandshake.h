/** @file
  J804 windows profile, D1: parse the launcher's /chosen AHCI handshake
  property (J804_AHCI_ADT_PROPERTY, layout in J804Windows.h).

  Header-only and free of UEFI services so the same code runs in
  J804AhciDxe and in the host test (Tools/HostTests/J804AhciHandshake).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_AHCI_HANDSHAKE_H_
#define J804_AHCI_HANDSHAKE_H_

#include <J804Windows.h>

typedef struct {
  UINT64     AbarBase;
  UINT64     AbarSize;
  UINT32     Gsiv;
  UINT32     Boot;
  UINT64     StoreBase;
  UINT64     StoreTop;
  BOOLEAN    HasStore;
} J804_AHCI_HANDSHAKE;

typedef enum {
  J804AhciHandshakeOk = 0,
  J804AhciHandshakeBadLength,
  J804AhciHandshakeBadAbar,
  J804AhciHandshakeBadGsiv,
  J804AhciHandshakeBadBoot,
  J804AhciHandshakeBadStore
} J804_AHCI_HANDSHAKE_STATUS;

#define J804_AHCI_STORE_ALIGN  0x4000ULL

STATIC
UINT64
J804AhciLe (
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
  TRUE when [Base0, Top0) and [Base1, Top1) share a byte. Both ranges are
  half-open; an empty range overlaps nothing.
**/
STATIC
BOOLEAN
J804AhciRangesOverlap (
  IN UINT64  Base0,
  IN UINT64  Top0,
  IN UINT64  Base1,
  IN UINT64  Top1
  )
{
  return (BOOLEAN)((Base0 < Top0) && (Base1 < Top1) && (Base0 < Top1) && (Base1 < Top0));
}

/**
  Decode and check the property value. Every value must equal the contract
  in J804Windows.h; the store range (40-byte form) must be non-empty,
  16 KiB aligned at both ends and clear of the ABAR. On any failure Out is
  zeroed and nothing may be registered.
**/
STATIC
J804_AHCI_HANDSHAKE_STATUS
J804AhciParseHandshake (
  IN  CONST VOID           *Value,
  IN  UINTN                Length,
  OUT J804_AHCI_HANDSHAKE  *Out
  )
{
  CONST UINT8                 *Bytes;
  J804_AHCI_HANDSHAKE         Parsed;
  J804_AHCI_HANDSHAKE_STATUS  Status;
  UINTN                       Index;

  for (Index = 0; Index < sizeof (*Out); Index++) {
    ((UINT8 *)Out)[Index] = 0;
  }

  if ((Value == NULL) ||
      ((Length != J804_AHCI_ADT_PROPERTY_SIZE) && (Length != J804_AHCI_ADT_PROPERTY_SHORT)))
  {
    return J804AhciHandshakeBadLength;
  }

  Bytes              = (CONST UINT8 *)Value;
  Parsed.AbarBase    = J804AhciLe (Bytes + 0x00, 8);
  Parsed.AbarSize    = J804AhciLe (Bytes + 0x08, 8);
  Parsed.Gsiv        = (UINT32)J804AhciLe (Bytes + 0x10, 4);
  Parsed.Boot        = (UINT32)J804AhciLe (Bytes + 0x14, 4);
  Parsed.HasStore    = (BOOLEAN)(Length == J804_AHCI_ADT_PROPERTY_SIZE);
  Parsed.StoreBase   = Parsed.HasStore ? J804AhciLe (Bytes + 0x18, 8) : 0;
  Parsed.StoreTop    = Parsed.HasStore ? J804AhciLe (Bytes + 0x20, 8) : 0;

  if ((Parsed.AbarBase != J804_AHCI_BASE) || (Parsed.AbarSize != J804_AHCI_SIZE)) {
    Status = J804AhciHandshakeBadAbar;
  } else if (Parsed.Gsiv != J804_GSIV_AHCI) {
    Status = J804AhciHandshakeBadGsiv;
  } else if ((Parsed.Boot != J804_AHCI_BOOT_DISK) && (Parsed.Boot != J804_AHCI_BOOT_RAMDISK)) {
    Status = J804AhciHandshakeBadBoot;
  } else if (Parsed.HasStore &&
             ((Parsed.StoreBase >= Parsed.StoreTop) ||
              ((Parsed.StoreBase & (J804_AHCI_STORE_ALIGN - 1)) != 0) ||
              ((Parsed.StoreTop & (J804_AHCI_STORE_ALIGN - 1)) != 0) ||
              J804AhciRangesOverlap (
                Parsed.StoreBase,
                Parsed.StoreTop,
                J804_AHCI_BASE,
                J804_AHCI_BASE + J804_AHCI_SIZE
                )))
  {
    Status = J804AhciHandshakeBadStore;
  } else {
    *Out = Parsed;
    return J804AhciHandshakeOk;
  }

  return Status;
}

#endif

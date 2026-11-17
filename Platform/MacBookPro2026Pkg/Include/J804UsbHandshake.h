/** @file
  J804 windows profile, D4: parse the launcher's /chosen USB handshake
  property (J804_USB_ADT_PROPERTY, layout in J804Windows.h).

  Header-only and free of UEFI services so the same code runs in
  J804XhciAcpiDxe and in the host test (Tools/HostTests/J804UsbHandshake).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_USB_HANDSHAKE_H_
#define J804_USB_HANDSHAKE_H_

#include <J804Windows.h>

typedef struct {
  UINT32    PortMask;
  UINT32    Gsiv;
  UINT64    XhciBase;
  UINT64    XhciSize;
  UINT64    DmaBase;
  UINT64    DmaTop;
} J804_USB_HANDSHAKE;

typedef enum {
  J804UsbHandshakeOk = 0,
  J804UsbHandshakeBadLength,
  J804UsbHandshakeBadPorts,
  J804UsbHandshakeBadGsiv,
  J804UsbHandshakeBadXhci,
  J804UsbHandshakeBadDma
} J804_USB_HANDSHAKE_STATUS;

#define J804_USB_DMA_ALIGN  0x4000ULL

STATIC
UINT64
J804UsbLe (
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
J804UsbRangesOverlap (
  IN UINT64  Base0,
  IN UINT64  Top0,
  IN UINT64  Base1,
  IN UINT64  Top1
  )
{
  return (BOOLEAN)((Base0 < Top0) && (Base1 < Top1) && (Base0 < Top1) && (Base1 < Top0));
}

/**
  TRUE when [Base, Base + Length) lies inside [WindowBase, WindowTop). A range
  whose end overflows is never inside; an empty range is.
**/
STATIC
BOOLEAN
J804UsbRangeInside (
  IN UINT64  Base,
  IN UINT64  Length,
  IN UINT64  WindowBase,
  IN UINT64  WindowTop
  )
{
  if (Length == 0) {
    return TRUE;
  }

  if (Base + Length < Base) {
    return FALSE;
  }

  return (BOOLEAN)((WindowBase <= Base) && (Base + Length <= WindowTop));
}

/**
  Decode and check the property value. Every value must equal the contract
  in J804Windows.h; the DMA window must be non-empty, 16 KiB aligned at both
  ends and clear of the xHCI window. On any failure Out is zeroed and no XHC1
  may be published.
**/
STATIC
J804_USB_HANDSHAKE_STATUS
J804UsbParseHandshake (
  IN  CONST VOID          *Value,
  IN  UINTN               Length,
  OUT J804_USB_HANDSHAKE  *Out
  )
{
  CONST UINT8         *Bytes;
  J804_USB_HANDSHAKE  Parsed;
  UINTN               Index;

  for (Index = 0; Index < sizeof (*Out); Index++) {
    ((UINT8 *)Out)[Index] = 0;
  }

  if ((Value == NULL) || (Length != J804_USB_ADT_PROPERTY_SIZE)) {
    return J804UsbHandshakeBadLength;
  }

  Bytes           = (CONST UINT8 *)Value;
  Parsed.PortMask = (UINT32)J804UsbLe (Bytes + 0x00, 4);
  Parsed.Gsiv     = (UINT32)J804UsbLe (Bytes + 0x04, 4);
  Parsed.XhciBase = J804UsbLe (Bytes + 0x08, 8);
  Parsed.XhciSize = J804UsbLe (Bytes + 0x10, 8);
  Parsed.DmaBase  = J804UsbLe (Bytes + 0x18, 8);
  Parsed.DmaTop   = J804UsbLe (Bytes + 0x20, 8);

  if (Parsed.PortMask != J804_USB_PORT_DRD1) {
    return J804UsbHandshakeBadPorts;
  }

  if (Parsed.Gsiv != J804_GSIV_XHC1) {
    return J804UsbHandshakeBadGsiv;
  }

  if ((Parsed.XhciBase != J804_XHC1_BASE) || (Parsed.XhciSize != J804_XHC_SIZE)) {
    return J804UsbHandshakeBadXhci;
  }

  if ((Parsed.DmaBase >= Parsed.DmaTop) ||
      ((Parsed.DmaBase & (J804_USB_DMA_ALIGN - 1)) != 0) ||
      ((Parsed.DmaTop & (J804_USB_DMA_ALIGN - 1)) != 0) ||
      J804UsbRangesOverlap (
        Parsed.DmaBase,
        Parsed.DmaTop,
        J804_XHC1_BASE,
        J804_XHC1_BASE + J804_XHC_SIZE
        ))
  {
    return J804UsbHandshakeBadDma;
  }

  *Out = Parsed;
  return J804UsbHandshakeOk;
}

#endif

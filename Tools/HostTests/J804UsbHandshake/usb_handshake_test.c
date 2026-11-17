// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test of J804UsbParseHandshake and the range helpers
// (J804UsbHandshake.h). The property bytes are built here from the layout
// written in J804Windows.h, field by field, not from the parser's own code.
//
#include <Base.h>
#include <stdio.h>
#include <string.h>
#include <J804UsbHandshake.h>

static int  failures;

static void
put (UINT8 *p, unsigned off, UINT64 v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    p[off + i] = (UINT8)(v >> (8 * i));
  }
}

static void
build (UINT8 *p, UINT32 ports, UINT32 gsiv, UINT64 base, UINT64 size, UINT64 db, UINT64 dt)
{
  memset (p, 0xA5, 64);
  put (p, 0x00, ports, 4);
  put (p, 0x04, gsiv, 4);
  put (p, 0x08, base, 8);
  put (p, 0x10, size, 8);
  put (p, 0x18, db, 8);
  put (p, 0x20, dt, 8);
}

static void
expect (const char *name, const UINT8 *p, UINTN len, J804_USB_HANDSHAKE_STATUS want)
{
  J804_USB_HANDSHAKE         out;
  J804_USB_HANDSHAKE         zero;
  J804_USB_HANDSHAKE_STATUS  got = J804UsbParseHandshake (p, len, &out);

  memset (&zero, 0, sizeof zero);
  if (got != want) {
    printf ("FAIL %s: status %d, want %d\n", name, got, want);
    failures++;
  } else if ((got != J804UsbHandshakeOk) && (memcmp (&out, &zero, sizeof out) != 0)) {
    printf ("FAIL %s: output not cleared on refusal\n", name);
    failures++;
  } else {
    printf ("ok   %s\n", name);
  }
}

static void
check (const char *name, int cond)
{
  if (!cond) {
    printf ("FAIL %s\n", name);
    failures++;
  } else {
    printf ("ok   %s\n", name);
  }
}

int
main (void)
{
  // The guest RAM window with the AHCI store attached (D4-USB-DESIGN.md).
  const UINT64        db = 0x1013c000000ULL, dt = 0x1025c000000ULL;
  const UINT64        xb = 0x40A430000ULL, xs = 0x8000;
  UINT8               p[64];
  J804_USB_HANDSHAKE  out;

  check ("contract constants", (J804_XHC1_BASE == xb) && (J804_XHC_SIZE == xs) &&
         (J804_GSIV_XHC1 == 995) && (J804_USB_PORT_DRD1 == 0x2) &&
         (J804_USB_ADT_PROPERTY_SIZE == 40) && (J804_USB_FD_MARKER_OFFSET == 0x120));

  build (p, 0x2, 995, xb, xs, db, dt);
  expect ("40-byte drd1", p, 40, J804UsbHandshakeOk);
  check ("40-byte fields",
         (J804UsbParseHandshake (p, 40, &out) == J804UsbHandshakeOk) && (out.PortMask == 0x2) &&
         (out.Gsiv == 995) && (out.XhciBase == xb) && (out.XhciSize == xs) &&
         (out.DmaBase == db) && (out.DmaTop == dt));

  expect ("NULL value", NULL, 40, J804UsbHandshakeBadLength);
  for (UINTN len = 0; len <= 64; len++) {
    if ((len != 40) && (J804UsbParseHandshake (p, len, &out) != J804UsbHandshakeBadLength)) {
      printf ("FAIL length %u accepted\n", (unsigned)len);
      failures++;
    }
  }

  build (p, 0x0, 995, xb, xs, db, dt);
  expect ("no port", p, 40, J804UsbHandshakeBadPorts);
  build (p, 0x1, 995, xb, xs, db, dt);
  expect ("drd0 bit", p, 40, J804UsbHandshakeBadPorts);
  build (p, 0x3, 995, xb, xs, db, dt);
  expect ("drd0 + drd1", p, 40, J804UsbHandshakeBadPorts);
  build (p, 0x8, 995, xb, xs, db, dt);
  expect ("drd3 only", p, 40, J804UsbHandshakeBadPorts);
  build (p, 0xA, 995, xb, xs, db, dt);
  expect ("drd1 + drd3 (no XHC2 in this build)", p, 40, J804UsbHandshakeBadPorts);
  build (p, 0x80000002u, 995, xb, xs, db, dt);
  expect ("drd1 + high bit", p, 40, J804UsbHandshakeBadPorts);

  build (p, 0x2, 997, xb, xs, db, dt);
  expect ("GSIV 997", p, 40, J804UsbHandshakeBadGsiv);
  build (p, 0x2, 1692, xb, xs, db, dt);
  expect ("physical AIC number as GSIV", p, 40, J804UsbHandshakeBadGsiv);

  build (p, 0x2, 995, 0x412430000ULL, xs, db, dt);
  expect ("drd3 base", p, 40, J804UsbHandshakeBadXhci);
  build (p, 0x2, 995, 0x402430000ULL, xs, db, dt);
  expect ("drd0 base", p, 40, J804UsbHandshakeBadXhci);
  build (p, 0x2, 995, xb, 0xC000, db, dt);
  expect ("three pages", p, 40, J804UsbHandshakeBadXhci);
  build (p, 0x2, 995, xb, 0x10000, db, dt);
  expect ("whole dwc3 incl. globals", p, 40, J804UsbHandshakeBadXhci);

  build (p, 0x2, 995, xb, xs, dt, db);
  expect ("DMA reversed", p, 40, J804UsbHandshakeBadDma);
  build (p, 0x2, 995, xb, xs, db, db);
  expect ("DMA empty", p, 40, J804UsbHandshakeBadDma);
  build (p, 0x2, 995, xb, xs, db + 0x1000, dt);
  expect ("DMA base unaligned", p, 40, J804UsbHandshakeBadDma);
  build (p, 0x2, 995, xb, xs, db, dt - 0x200);
  expect ("DMA top unaligned", p, 40, J804UsbHandshakeBadDma);
  build (p, 0x2, 995, xb, xs, 0x400000000ULL, 0x40A434000ULL);
  expect ("DMA over the xHCI window", p, 40, J804UsbHandshakeBadDma);
  build (p, 0x2, 995, xb, xs, 0x40A438000ULL, 0x40A43C000ULL);
  expect ("DMA right after the xHCI window", p, 40, J804UsbHandshakeOk);

  // Unaligned property value (ADT values are only 4-byte aligned).
  UINT8  shifted[72];
  build (p, 0x2, 995, xb, xs, db, dt);
  memcpy (shifted + 4, p, 40);
  expect ("value at +4", shifted + 4, 40, J804UsbHandshakeOk);

  check ("overlap helper",
         J804UsbRangesOverlap (0, 2, 1, 3) && !J804UsbRangesOverlap (0, 2, 2, 4) &&
         !J804UsbRangesOverlap (2, 4, 0, 2) && !J804UsbRangesOverlap (0, 0, 0, 4) &&
         J804UsbRangesOverlap (0, 10, 3, 4));

  // RAM containment (the GCD check): inside, touching both ends, straddling,
  // overflowing, empty.
  check ("inside helper",
         J804UsbRangeInside (db, dt - db, db, dt) &&
         J804UsbRangeInside (db + 0x4000, 0x4000, db, dt) &&
         !J804UsbRangeInside (db - 0x4000, 0x8000, db, dt) &&
         !J804UsbRangeInside (dt - 0x4000, 0x8000, db, dt) &&
         !J804UsbRangeInside (dt, 0x4000, db, dt) &&
         !J804UsbRangeInside (0xFFFFFFFFFFFFC000ULL, 0x8000, 0, 0xFFFFFFFFFFFFC000ULL) &&
         J804UsbRangeInside (0, 0, db, dt));

  // The AHCI store in the D1 worked example sits right above the window: no
  // overlap at the shared boundary, overlap one page lower.
  check ("store boundary",
         !J804UsbRangesOverlap (db, dt, dt, 0x1059d600000ULL) &&
         J804UsbRangesOverlap (db, dt, dt - 0x4000, 0x1059d600000ULL));

  printf (failures ? "J804_USB_HANDSHAKE_TEST FAIL %d\n" : "J804_USB_HANDSHAKE_TEST PASS\n", failures);
  return failures != 0;
}

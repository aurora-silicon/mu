// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test of J804AhciParseHandshake (J804AhciHandshake.h). The property
// bytes are built here from the layout written in J804Windows.h, field by
// field, not from the parser's own code.
//
#include <Base.h>
#include <stdio.h>
#include <string.h>
#include <J804AhciHandshake.h>

static int  failures;

static void
put (UINT8 *p, unsigned off, UINT64 v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    p[off + i] = (UINT8)(v >> (8 * i));
  }
}

static void
build (UINT8 *p, UINT64 abar, UINT64 size, UINT32 gsiv, UINT32 boot, UINT64 sb, UINT64 st)
{
  memset (p, 0xA5, 64);
  put (p, 0x00, abar, 8);
  put (p, 0x08, size, 8);
  put (p, 0x10, gsiv, 4);
  put (p, 0x14, boot, 4);
  put (p, 0x18, sb, 8);
  put (p, 0x20, st, 8);
}

static void
expect (const char *name, const UINT8 *p, UINTN len, J804_AHCI_HANDSHAKE_STATUS want)
{
  J804_AHCI_HANDSHAKE  out;
  J804_AHCI_HANDSHAKE  zero;
  J804_AHCI_HANDSHAKE_STATUS got = J804AhciParseHandshake (p, len, &out);

  memset (&zero, 0, sizeof zero);
  if (got != want) {
    printf ("FAIL %s: status %d, want %d\n", name, got, want);
    failures++;
  } else if ((got != J804AhciHandshakeOk) && (memcmp (&out, &zero, sizeof out) != 0)) {
    printf ("FAIL %s: output not cleared on refusal\n", name);
    failures++;
  } else {
    printf ("ok   %s\n", name);
  }
}

int
main (void)
{
  // The 12 GiB worked example of D1-AHCI-DESIGN.md 4.1.
  const UINT64          sb = 0x1029c000000ULL, st = 0x1059d600000ULL;
  UINT8                 p[64];
  J804_AHCI_HANDSHAKE   out;

  build (p, 0xF40000000ULL, 0x4000, 990, 0, sb, st);
  expect ("40-byte disk", p, 40, J804AhciHandshakeOk);
  if ((J804AhciParseHandshake (p, 40, &out) != J804AhciHandshakeOk) || !out.HasStore ||
      (out.StoreBase != sb) || (out.StoreTop != st) || (out.Boot != 0) || (out.Gsiv != 990) ||
      (out.AbarBase != 0xF40000000ULL) || (out.AbarSize != 0x4000))
  {
    printf ("FAIL 40-byte fields\n");
    failures++;
  }

  build (p, 0xF40000000ULL, 0x4000, 990, 1, sb, st);
  expect ("40-byte ramdisk", p, 40, J804AhciHandshakeOk);
  expect ("24-byte short form", p, 24, J804AhciHandshakeOk);
  if ((J804AhciParseHandshake (p, 24, &out) != J804AhciHandshakeOk) || out.HasStore ||
      (out.StoreBase != 0) || (out.StoreTop != 0) || (out.Boot != 1))
  {
    printf ("FAIL 24-byte fields\n");
    failures++;
  }

  expect ("NULL value", NULL, 40, J804AhciHandshakeBadLength);
  for (UINTN len = 0; len <= 64; len++) {
    if ((len != 24) && (len != 40) &&
        (J804AhciParseHandshake (p, len, &out) != J804AhciHandshakeBadLength))
    {
      printf ("FAIL length %u accepted\n", (unsigned)len);
      failures++;
    }
  }

  build (p, 0xF40004000ULL, 0x4000, 990, 0, sb, st);
  expect ("other ABAR", p, 40, J804AhciHandshakeBadAbar);
  build (p, 0xF40000000ULL, 0x8000, 990, 0, sb, st);
  expect ("other ABAR size", p, 40, J804AhciHandshakeBadAbar);
  build (p, 0xF40000000ULL, 0x4000, 996, 0, sb, st);
  expect ("other GSIV", p, 40, J804AhciHandshakeBadGsiv);
  build (p, 0xF40000000ULL, 0x4000, 990, 2, sb, st);
  expect ("boot 2", p, 40, J804AhciHandshakeBadBoot);
  build (p, 0xF40000000ULL, 0x4000, 990, 0xFFFFFFFF, sb, st);
  expect ("boot ~0", p, 40, J804AhciHandshakeBadBoot);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, st, sb);
  expect ("store reversed", p, 40, J804AhciHandshakeBadStore);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, sb, sb);
  expect ("store empty", p, 40, J804AhciHandshakeBadStore);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, sb + 0x200, st);
  expect ("store base unaligned", p, 40, J804AhciHandshakeBadStore);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, sb, st - 0x1000);
  expect ("store top unaligned", p, 40, J804AhciHandshakeBadStore);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, 0xF00000000ULL, 0xF40004000ULL);
  expect ("store over ABAR", p, 40, J804AhciHandshakeBadStore);
  build (p, 0xF40000000ULL, 0x4000, 990, 0, 0xF40004000ULL, 0xF40008000ULL);
  expect ("store right after ABAR", p, 40, J804AhciHandshakeOk);

  // Unaligned property value (ADT values are only 4-byte aligned).
  UINT8  shifted[72];
  build (p, 0xF40000000ULL, 0x4000, 990, 0, sb, st);
  memcpy (shifted + 4, p, 40);
  expect ("value at +4", shifted + 4, 40, J804AhciHandshakeOk);

  // Overlap helper: half-open ranges.
  if (!J804AhciRangesOverlap (0, 2, 1, 3) || J804AhciRangesOverlap (0, 2, 2, 4) ||
      J804AhciRangesOverlap (2, 4, 0, 2) || J804AhciRangesOverlap (0, 0, 0, 4) ||
      !J804AhciRangesOverlap (0, 10, 3, 4))
  {
    printf ("FAIL overlap helper\n");
    failures++;
  }

  printf (failures ? "J804_AHCI_HANDSHAKE_TEST FAIL %d\n" : "J804_AHCI_HANDSHAKE_TEST PASS\n", failures);
  return failures != 0;
}

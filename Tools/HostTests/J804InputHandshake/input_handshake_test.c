// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test of J804InputParseHandshake (J804InputHandshake.h). The property
// bytes are built here from the layout written in J804Windows.h (and the
// launcher's struct format "<QIIII": base, size, GSIV, feature mask,
// version), field by field, not from the parser's own code.
//
#include <Base.h>
#include <stdio.h>
#include <string.h>
#include <J804InputHandshake.h>

static int  failures;

static void
put (UINT8 *p, unsigned off, UINT64 v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    p[off + i] = (UINT8)(v >> (8 * i));
  }
}

static void
build (UINT8 *p, UINT64 base, UINT32 size, UINT32 gsiv, UINT32 features, UINT32 version)
{
  memset (p, 0xA5, 64);
  put (p, 0x00, base, 8);
  put (p, 0x08, size, 4);
  put (p, 0x0C, gsiv, 4);
  put (p, 0x10, features, 4);
  put (p, 0x14, version, 4);
}

static void
expect (const char *name, const UINT8 *p, UINTN len, J804_INPUT_HANDSHAKE_STATUS want)
{
  J804_INPUT_HANDSHAKE         out;
  J804_INPUT_HANDSHAKE         zero;
  J804_INPUT_HANDSHAKE_STATUS  got = J804InputParseHandshake (p, len, &out);

  memset (&zero, 0, sizeof zero);
  if (got != want) {
    printf ("FAIL %s: status %d, want %d\n", name, got, want);
    failures++;
  } else if ((got != J804InputHandshakeOk) && (memcmp (&out, &zero, sizeof out) != 0)) {
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
  // The carrier's contract (aura-win docs/windows-t8152.md, I0): page, size,
  // GSIV, and the feature mask the launcher writes for --windows-input host.
  const UINT64          pb = 0xF50000000ULL;
  const UINT32          ps = 0x4000, gsiv = 991, host = 0x1000F;
  UINT8                 p[64];
  J804_INPUT_HANDSHAKE  out;

  check ("contract constants", (J804_INPUT_BASE == pb) && (J804_INPUT_SIZE == ps) &&
         (J804_GSIV_INPUT == gsiv) && (J804_INPUT_ADT_PROPERTY_SIZE == 24) &&
         (J804_INPUT_VERSION == 1) && (J804_INPUT_FD_MARKER_OFFSET == 0x140) &&
         (J804_INPUT_FEATURES_REPORTS == 0xF) && (J804_INPUT_FEATURES_SOURCES == 0xFF0000));
  check ("property name", strcmp (J804_INPUT_ADT_PROPERTY, "aura,windows-input-v1") == 0);
  check ("HID", strcmp (J804_INPUT_HID, "NTAS2040") == 0);

  build (p, pb, ps, gsiv, host, 1);
  expect ("24-byte host feed", p, 24, J804InputHandshakeOk);
  check ("24-byte fields",
         (J804InputParseHandshake (p, 24, &out) == J804InputHandshakeOk) && (out.Base == pb) &&
         (out.Size == ps) && (out.Gsiv == gsiv) && (out.Features == host) && (out.Version == 1));

  // Later sources (I1, I2) are accepted by the parser: the carrier decides.
  build (p, pb, ps, gsiv, 0x7000F, 1);
  expect ("host + keyboard + trackpad", p, 24, J804InputHandshakeOk);
  build (p, pb, ps, gsiv, 0x2000F, 1);
  expect ("internal keyboard only", p, 24, J804InputHandshakeOk);
  build (p, pb, ps, gsiv, 0xFF000F, 1);
  expect ("all source bits", p, 24, J804InputHandshakeOk);

  expect ("NULL value", NULL, 24, J804InputHandshakeBadLength);
  build (p, pb, ps, gsiv, host, 1);
  for (UINTN len = 0; len <= 64; len++) {
    if ((len != 24) && (J804InputParseHandshake (p, len, &out) != J804InputHandshakeBadLength)) {
      printf ("FAIL length %u accepted\n", (unsigned)len);
      failures++;
    }
  }

  build (p, 0xF40000000ULL, ps, gsiv, host, 1);
  expect ("AHCI ABAR as page", p, 24, J804InputHandshakeBadPage);
  build (p, pb + 0x4000, ps, gsiv, host, 1);
  expect ("next page", p, 24, J804InputHandshakeBadPage);
  build (p, pb | (1ULL << 40), ps, gsiv, host, 1);
  expect ("high bit in base", p, 24, J804InputHandshakeBadPage);
  build (p, pb, 0x1000, gsiv, host, 1);
  expect ("4 KiB size", p, 24, J804InputHandshakeBadPage);
  build (p, pb, 0x8000, gsiv, host, 1);
  expect ("two pages", p, 24, J804InputHandshakeBadPage);

  build (p, pb, ps, 990, host, 1);
  expect ("GSIV 990 (AHCI)", p, 24, J804InputHandshakeBadGsiv);
  build (p, pb, ps, 995, host, 1);
  expect ("GSIV 995 (XHC1)", p, 24, J804InputHandshakeBadGsiv);
  build (p, pb, ps, gsiv | 0x10000u, host, 1);
  expect ("GSIV high bits", p, 24, J804InputHandshakeBadGsiv);

  build (p, pb, ps, gsiv, host, 0);
  expect ("version 0", p, 24, J804InputHandshakeBadVersion);
  build (p, pb, ps, gsiv, host, 2);
  expect ("version 2", p, 24, J804InputHandshakeBadVersion);
  build (p, pb, ps, gsiv, host, 0x10001);
  expect ("version 1 with high bits", p, 24, J804InputHandshakeBadVersion);

  for (UINT32 bit = 0; bit < 4; bit++) {
    char  name[48];

    snprintf (name, sizeof name, "report bit %u missing", (unsigned)bit);
    build (p, pb, ps, gsiv, host & ~(1u << bit), 1);
    expect (name, p, 24, J804InputHandshakeBadReports);
  }

  build (p, pb, ps, gsiv, 0x10000, 1);
  expect ("no report bits", p, 24, J804InputHandshakeBadReports);

  for (UINT32 bit = 4; bit < 32; bit++) {
    if ((bit >= 16) && (bit < 24)) {
      continue;
    }

    char  name[48];
    snprintf (name, sizeof name, "reserved bit %u", (unsigned)bit);
    build (p, pb, ps, gsiv, host | (1u << bit), 1);
    expect (name, p, 24, J804InputHandshakeBadReserved);
  }

  build (p, pb, ps, gsiv, 0xF, 1);
  expect ("no source", p, 24, J804InputHandshakeNoSource);

  // Unaligned property value (ADT values are only 4-byte aligned).
  UINT8  shifted[72];
  build (p, pb, ps, gsiv, host, 1);
  memcpy (shifted + 4, p, 24);
  expect ("value at +4", shifted + 4, 24, J804InputHandshakeOk);
  memcpy (shifted + 1, p, 24);
  expect ("value at +1", shifted + 1, 24, J804InputHandshakeOk);

  // The exact bytes Python's struct.pack("<QIIII", 0xF50000000, 0x4000, 991,
  // 0x1000F, 1) produces (the launcher's encoder), written out literally.
  static const UINT8  launcher[24] = {
    0x00, 0x00, 0x00, 0x50, 0x0F, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x00, 0xDF, 0x03, 0x00, 0x00,
    0x0F, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00
  };
  expect ("launcher bytes", launcher, 24, J804InputHandshakeOk);

  printf (failures ? "J804_INPUT_HANDSHAKE_TEST FAIL %d\n" : "J804_INPUT_HANDSHAKE_TEST PASS\n", failures);
  return failures != 0;
}

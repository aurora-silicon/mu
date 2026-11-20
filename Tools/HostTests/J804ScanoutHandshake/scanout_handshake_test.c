// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test of J804ScanoutParseHandshake (J804ScanoutHandshake.h). The
// property bytes are built here from the layout written in J804Windows.h (and
// the launcher's struct format "<QIIIII4x": base, size, GSIV, features,
// refresh mHz, version, zero), field by field, not from the parser's own code.
//
#include <Base.h>
#include <stdio.h>
#include <string.h>
#include <J804ScanoutHandshake.h>

static int  failures;

static void
put (UINT8 *p, unsigned off, UINT64 v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    p[off + i] = (UINT8)(v >> (8 * i));
  }
}

static void
build (UINT8 *p, UINT64 base, UINT32 size, UINT32 gsiv, UINT32 features, UINT32 mhz, UINT32 version)
{
  memset (p, 0xA5, 64);
  put (p, 0x00, base, 8);
  put (p, 0x08, size, 4);
  put (p, 0x0C, gsiv, 4);
  put (p, 0x10, features, 4);
  put (p, 0x14, mhz, 4);
  put (p, 0x18, version, 4);
  put (p, 0x1C, 0, 4);
}

static void
expect (const char *name, const UINT8 *p, UINTN len, J804_SCANOUT_HANDSHAKE_STATUS want)
{
  J804_SCANOUT_HANDSHAKE         out;
  J804_SCANOUT_HANDSHAKE         zero;
  J804_SCANOUT_HANDSHAKE_STATUS  got = J804ScanoutParseHandshake (p, len, &out);

  memset (&zero, 0, sizeof zero);
  if (got != want) {
    printf ("FAIL %s: status %d, want %d\n", name, got, want);
    failures++;
  } else if ((got != J804ScanoutHandshakeOk) && (memcmp (&out, &zero, sizeof out) != 0)) {
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
  UINT8                   p[64];
  J804_SCANOUT_HANDSHAKE  out;
  static const char       *text[] = { J804_SCANOUT_HANDSHAKE_STATUS_TEXT };

  check ("contract values", J804_SCANOUT_BASE == 0xF60000000ULL && J804_SCANOUT_SIZE == 0x4000 &&
         J804_GSIV_SCANOUT == 992 && J804_SCANOUT_ADT_PROPERTY_SIZE == 32 &&
         J804_SCANOUT_FD_MARKER_OFFSET == 0x150 && J804_SCANOUT_VERSION == 1);
  check ("one text per status", ARRAY_SIZE (text) == J804ScanoutHandshakeBadReserved + 1);

  // The launcher's value for 60 Hz (aura windows.scanout_handshake(60000)).
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 60000, 1);
  expect ("60 Hz accepted", p, 32, J804ScanoutHandshakeOk);
  J804ScanoutParseHandshake (p, 32, &out);
  check ("fields decoded", out.Base == 0xF60000000ULL && out.Size == 0x4000 && out.Gsiv == 992 &&
         out.Features == 0x1F && out.RefreshMhz == 60000 && out.Version == 1);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x03, 120000, 1);
  expect ("120 Hz, minimum features", p, 32, J804ScanoutHandshakeOk);

  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 60000, 1);
  expect ("NULL", NULL, 32, J804ScanoutHandshakeBadLength);
  expect ("short", p, 31, J804ScanoutHandshakeBadLength);
  expect ("long", p, 33, J804ScanoutHandshakeBadLength);
  expect ("the input length", p, 24, J804ScanoutHandshakeBadLength);

  build (p, 0xF50000000ULL, 0x4000, 992, 0x1F, 60000, 1);
  expect ("the input page", p, 32, J804ScanoutHandshakeBadPage);
  build (p, 0xF60000000ULL, 0x8000, 992, 0x1F, 60000, 1);
  expect ("size", p, 32, J804ScanoutHandshakeBadPage);
  build (p, 0xF60000000ULL, 0x4000, 991, 0x1F, 60000, 1);
  expect ("the input SPI", p, 32, J804ScanoutHandshakeBadGsiv);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 60000, 2);
  expect ("version 2", p, 32, J804ScanoutHandshakeBadVersion);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1D, 60000, 1);
  expect ("no copy flip", p, 32, J804ScanoutHandshakeBadFeatures);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1E, 60000, 1);
  expect ("no timer vblank", p, 32, J804ScanoutHandshakeBadFeatures);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x9F, 60000, 1);
  expect ("unknown feature", p, 32, J804ScanoutHandshakeBadFeatures);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 999, 1);
  expect ("refresh below 1 Hz", p, 32, J804ScanoutHandshakeBadRefresh);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 240001, 1);
  expect ("refresh above 240 Hz", p, 32, J804ScanoutHandshakeBadRefresh);
  build (p, 0xF60000000ULL, 0x4000, 992, 0x1F, 60000, 1);
  p[0x1F] = 1;
  expect ("reserved word", p, 32, J804ScanoutHandshakeBadReserved);

  printf ("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}

/*
 * Host test for the J804 M1 reservation code in T8152FamilyPkg
 * MemoryInitPeiLib (the J804_M1 block, compiled unchanged against small
 * shims), for J804M1AdtIsJ804 and for the PrePi UEFI-region check
 * (J804M1UefiRegion/J804M1RegionIsClear), run against a saved J804 device tree.
 *
 * The device tree is read into memory from the path given on the command
 * line; the file is never written. chainload.py --raw packing is emulated in
 * memory only, the way proxyclient/tools/chainload.py rewrites the
 * /chosen/memory-map entries.
 *
 * usage: j804m1_reserve_test <adt.bin> [-v]
 */
#include <Base.h>
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#define __asm__(x)
#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/PanelCheckpointLib.h>
#define J804_M1 1
#include <J804M1.h>

jmp_buf gHostStop;
int gHostStopCount;
int gHostLastError = -1;
int gHostVerbose;
UINT64 gHostAdt, gHostRamBase, gHostRamSize, gHostFdBase;

void HostDebugPrint (UINTN Level, const char *Format, ...)
{
  char Fixed[512];
  size_t i, j = 0;
  va_list Args;

  if (!gHostVerbose) return;
  for (i = 0; Format[i] && j < sizeof (Fixed) - 2; i++) {
    Fixed[j++] = Format[i];
    if (Format[i] == '%' && Format[i + 1] == 'a') { Fixed[j++] = 's'; i++; }
  }
  Fixed[j] = 0;
  va_start (Args, Format);
  vprintf (Fixed, Args);
  va_end (Args);
}

/* ---- minimal HOB list ---- */
#define HOB_MAX 4096
static EFI_HOB_RESOURCE_DESCRIPTOR mHobs[HOB_MAX + 1];
static UINTN mHobCount;

static void HobReset (void) { mHobCount = 0; mHobs[0].Header.HobType = 0xFFFF; mHobs[0].Header.HobLength = sizeof (mHobs[0]); }
VOID *GetHobList (VOID) { return mHobs; }
VOID *GetNextHob (UINT16 Type, CONST VOID *Start)
{
  const EFI_HOB_RESOURCE_DESCRIPTOR *H = Start;
  while (H < &mHobs[mHobCount]) { if (H->Header.HobType == Type) return (VOID *)H; H++; }
  return NULL;
}
VOID BuildResourceDescriptorHob (EFI_RESOURCE_TYPE Type, EFI_RESOURCE_ATTRIBUTE_TYPE Attr, EFI_PHYSICAL_ADDRESS Start, UINT64 Length)
{
  if (mHobCount >= HOB_MAX) { fprintf (stderr, "HOB overflow\n"); exit (2); }
  mHobs[mHobCount].Header.HobType = EFI_HOB_TYPE_RESOURCE_DESCRIPTOR;
  mHobs[mHobCount].Header.HobLength = sizeof (mHobs[0]);
  mHobs[mHobCount].ResourceType = Type;
  mHobs[mHobCount].ResourceAttribute = Attr;
  mHobs[mHobCount].PhysicalStart = Start;
  mHobs[mHobCount].ResourceLength = Length;
  mHobCount++;
  mHobs[mHobCount].Header.HobType = 0xFFFF;
  mHobs[mHobCount].Header.HobLength = sizeof (mHobs[0]);
}

/* The J804_M1 block of MemoryInitPeiLib.c, extracted verbatim by the Makefile. */
#include "gen/j804_block.c"

/* ---- helpers ---- */
static int gFailures;
#define CHECK(c, ...) do { if (!(c)) { gFailures++; printf ("FAIL: " __VA_ARGS__); printf ("\n"); } } while (0)

static UINT64 rd64 (const void *p) { UINT64 v; memcpy (&v, p, 8); return v; }
static void wr64 (void *p, UINT64 v) { memcpy (p, &v, 8); }
static UINT64 AlignUp (UINT64 v, UINT64 a) { return (v + a - 1) & ~(a - 1); }

/* Is [b, t) entirely covered by reserved HOBs? */
static int CoveredByReserved (UINT64 b, UINT64 t)
{
  while (b < t) {
    UINTN i; int moved = 0;
    for (i = 0; i < mHobCount; i++) {
      EFI_HOB_RESOURCE_DESCRIPTOR *h = &mHobs[i];
      if (h->ResourceType != EFI_RESOURCE_MEMORY_RESERVED) continue;
      if (h->PhysicalStart <= b && b < h->PhysicalStart + h->ResourceLength) { b = h->PhysicalStart + h->ResourceLength; moved = 1; break; }
    }
    if (!moved) return 0;
  }
  return 1;
}

static int IsSystem (UINT64 b, UINT64 t)
{
  UINTN i;
  for (i = 0; i < mHobCount; i++) {
    EFI_HOB_RESOURCE_DESCRIPTOR *h = &mHobs[i];
    if (h->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY && h->PhysicalStart <= b && t <= h->PhysicalStart + h->ResourceLength) return 1;
  }
  return 0;
}

/* Check a range (clipped to RAM, rounded to pages, FD cut out) is reserved. */
static void ExpectReserved (const char *what, UINT64 b, UINT64 s)
{
  UINT64 t = b + s, rb = gHostRamBase, rt = gHostRamBase + gHostRamSize;
  UINT64 fb = gHostFdBase, ft = gHostFdBase + 0x1E00000;
  if (s == 0 || t <= rb || b >= rt) return;
  if (b < rb) b = rb;
  if (t > rt) t = rt;
  b &= ~0xFFFULL; t = AlignUp (t, 0x1000);
  if (b < ft && t > fb) {
    if (b < fb) CHECK (CoveredByReserved (b, fb), "%s below FD 0x%llx..0x%llx not reserved", what, b, fb);
    if (t > ft) CHECK (CoveredByReserved (ft, t), "%s above FD 0x%llx..0x%llx not reserved", what, ft, t);
  } else {
    CHECK (CoveredByReserved (b, t), "%s 0x%llx..0x%llx not reserved", what, b, t);
  }
}

static const char *kBlobs[] = { "SEPFW", "SEPPatches", "uStuff", "preoslog", "RTBuddySeg", "RAMDisk",
  "SPTMDebug", "ExclaveOSIntegrityCatalog", "ExclaveOSTrustCache", "TrustCache" };

/* Rewrite memory-map entries the way chainload.py --raw --relocate does. Returns fw block end. */
static UINT64 EmulateChainload (UINT64 FdBase)
{
  dt_node_t *mm = dt_get ("/chosen/memory-map");
  UINT64 image_only = AlignUp (0x1E00000 + 4, 0x4000), fw = FdBase + image_only, end = 0;
  size_t i, len;
  for (i = 0; i < ARRAY_SIZE (kBlobs); i++) {
    void *v = dt_prop (mm, kBlobs[i], &len);
    if (!v || len < 16) continue;
    UINT64 src = rd64 (v), size = rd64 ((char *)v + 8);
    if ((src == MAX_UINT64 && size == MAX_UINT64) || size == 0) continue;
    wr64 (v, fw + end);
    end = AlignUp (end + size, 0x4000);
  }
  void *ba = dt_prop (mm, "BootArgs", &len);
  if (ba && len >= 16) { wr64 (ba, fw + end); wr64 ((char *)ba + 8, 0x4000); }
  return fw + end + 0x4000;
}

typedef struct { int n; } CarveCtx;
static int CarveCheck (void *a, dt_node_t *n, int d, const char *key, void *val, size_t len)
{
  if (strncmp (key, "region-id-", 10) || len < 16 || !strcmp (key, "region-id-24")) return 0;
  UINT64 b = rd64 (val), s = rd64 ((char *)val + 8);
  if (b < gHostRamBase + gHostRamSize && b + s > gHostRamBase) ((CarveCtx *)a)->n++;
  ExpectReserved (key, b, s);
  return 0;
}
static int MmCheck (void *a, dt_node_t *n, int d, const char *key, void *val, size_t len)
{
  if (len < 16) return 0;
  UINT64 b = rd64 (val), s = rd64 ((char *)val + 8);
  if (b == MAX_UINT64 || s == MAX_UINT64 || s == 0) return 0;
  if (b < gHostRamBase + gHostRamSize && b + s > gHostRamBase) ((CarveCtx *)a)->n++;
  ExpectReserved (key, b, s);
  return 0;
}

static void SetupHobs (UINT64 RamBase, UINT64 RamTop, UINT64 FdBase)
{
  HobReset ();
  BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, 7, RamBase, FdBase - RamBase);
  BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, 7, FdBase, 0x1E00000);
  BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, 7, FdBase + 0x1E00000, RamTop - FdBase - 0x1E00000);
}

static int RunReserve (UINT64 RamTop)
{
  gHostRamSize = RamTop - gHostRamBase;
  SetupHobs (gHostRamBase, RamTop, gHostFdBase);
  gHostLastError = -1;
  if (setjmp (gHostStop) == 0) {
    J804M1ReserveFromAdt (RamTop - 0x8000000, 0x8000000, gHostFdBase + 0x1E00000, 0);
    return -1;
  }
  return gHostLastError;
}

/* PrePi EarlySetup's UEFI-region check (J804M1.h), with the J804 M1 FD
   size, UEFI region size and boot_args/ADT copy addresses. */
static BOOLEAN PrePiRegionClear (UINT64 RamTop, J804_M1_REGION_CHECK *c)
{
  UINT64 rb, rt;
  J804M1UefiRegion (gHostRamBase, RamTop, gHostFdBase, 0x1E00000, 0x8000000, &rb, &rt);
  return J804M1RegionIsClear (gHostRamBase, RamTop, rb, rt, gHostFdBase, 0x1E00000,
                              0x10220000000ULL, 0x10220004000ULL, c);
}

int main (int argc, char **argv)
{
  FILE *f; long size; void *adt; size_t len; UINTN i;
  if (argc < 2) { fprintf (stderr, "usage: %s <adt.bin> [-v]\n", argv[0]); return 2; }
  gHostVerbose = argc > 2;
  f = fopen (argv[1], "rb");
  if (!f) { perror ("adt"); return 2; }
  fseek (f, 0, SEEK_END); size = ftell (f); fseek (f, 0, SEEK_SET);
  adt = malloc (size + 16);
  if (fread (adt, 1, size, f) != (size_t)size) return 2;
  fclose (f);
  gHostAdt = (UINT64)(UINTN)adt;
  CHECK (dt_check (adt, size, NULL) == 0, "dt_check");

  /* 1. J804 detection on the saved tree, and its negative. */
  CHECK (J804M1AdtIsJ804 (), "saved tree not detected as J804");
  {
    char *tt = dt_prop ((dt_node_t *)adt, "target-type", &len), *co = dt_prop ((dt_node_t *)adt, "compatible", &len);
    char s1 = tt[3], s2 = co[3];
    tt[3] = '3'; co[3] = '3';
    CHECK (!J804M1AdtIsJ804 (), "J873-like tree detected as J804");
    tt[3] = s1; co[3] = s2;
  }
  printf ("J804 detection: ok\n");

  /* 2. Trimmed launch: RAM top at the region-id-218 base, FD 64 KiB aligned in the loader heap. */
  gHostRamBase = 0x10005d2c000ULL;
  gHostFdBase = 0x10012d40000ULL;
  UINT64 fwEnd = EmulateChainload (gHostFdBase);
  int stop = RunReserve (J804_M1_RAM_TOP_LIMIT);
  CHECK (stop == -1, "trimmed launch stopped with code %d", stop);
  {
    CarveCtx c = { 0 }, m = { 0 };
    dt_parse (dt_get ("/chosen/carveout-memory-map"), -1, NULL, NULL, NULL, CarveCheck, &c);
    dt_parse (dt_get ("/chosen/memory-map"), -1, NULL, NULL, NULL, MmCheck, &m);
    ExpectReserved ("chainload firmware block", gHostFdBase + 0x1E00000, fwEnd - gHostFdBase - 0x1E00000);
    ExpectReserved ("boot_args copy", 0x10220000000ULL, 0x4000);
    /* The ADT copy reservation uses PcdAdtPointer, which this harness points
       at its host buffer (outside the emulated RAM), so it is not checked. */
    CHECK (IsSystem (gHostFdBase, gHostFdBase + 0x1E00000), "FD is no longer one system memory HOB");
    CHECK (IsSystem (J804_M1_RAM_TOP_LIMIT - 0x8000000, J804_M1_RAM_TOP_LIMIT), "UEFI region not system memory");
    /* Reserved and system HOBs must be disjoint and tile RAM exactly. */
    UINT64 sys = 0, res = 0;
    for (i = 0; i < mHobCount; i++) {
      UINTN j;
      if (mHobs[i].ResourceLength == 0) { CHECK (0, "zero-length HOB at 0x%llx", mHobs[i].PhysicalStart); continue; }
      if (mHobs[i].ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) sys += mHobs[i].ResourceLength; else res += mHobs[i].ResourceLength;
      for (j = i + 1; j < mHobCount; j++) {
        UINT64 a = mHobs[i].PhysicalStart, at = a + mHobs[i].ResourceLength, b = mHobs[j].PhysicalStart, bt = b + mHobs[j].ResourceLength;
        if (mHobs[j].ResourceLength && a < bt && b < at) CHECK (0, "HOBs overlap 0x%llx..0x%llx / 0x%llx..0x%llx", a, at, b, bt);
      }
    }
    CHECK (sys + res == gHostRamSize, "HOBs cover 0x%llx of 0x%llx RAM bytes", sys + res, gHostRamSize);
    printf ("trimmed launch: %d carve-outs and %d memory-map entries in RAM checked; %zu HOBs; reserved 0x%llx (%.2f GiB), free 0x%llx (%.2f GiB)\n",
            c.n, m.n, (size_t)mHobCount, res, res / 1073741824.0, sys, sys / 1073741824.0);
    printf ("chainload firmware block 0x%llx..0x%llx\n", gHostFdBase + 0x1E00000, fwEnd);
  }

  /* 3. Untrimmed RAM top: region-id-218 reaches the UEFI region -> stop code 1. */
  stop = RunReserve (0x105cd600000ULL);
  CHECK (stop == J804_M1_ERROR_UEFI_REGION_OVERLAP, "untrimmed launch: stop code %d, want %d", stop, J804_M1_ERROR_UEFI_REGION_OVERLAP);
  printf ("untrimmed RAM top: stop code %d (UEFI region overlap)\n", stop);
  CHECK (0x105cd600000ULL > J804_M1_RAM_TOP_LIMIT, "EarlySetup limit would not catch the untrimmed top");

  /* 3b. PrePi UEFI-region check: the trimmed top passes, lower tops whose
     UEFI region lies on carve-outs and the untrimmed top are refused, and
     every top PrePi accepts also passes MemoryInitPeiLib (no later stop). */
  {
    J804_M1_REGION_CHECK c;
    static const UINT64 bad[] = { 0x10140000000ULL, 0x105cd600000ULL, 0x10220100000ULL };
    CHECK (PrePiRegionClear (J804_M1_RAM_TOP_LIMIT, &c), "PrePi refuses the trimmed top (%s 0x%llx..0x%llx)",
           c.HitKey ? c.HitKey : "no map", c.HitBase, c.HitTop);
    for (i = 0; i < ARRAY_SIZE (bad); i++) {
      BOOLEAN ok = PrePiRegionClear (bad[i], &c);
      CHECK (!ok && c.HitKey != NULL, "PrePi accepts RAM top 0x%llx", bad[i]);
      printf ("PrePi RAM top 0x%llx: refused, UEFI region 0x%llx..0x%llx hits %s 0x%llx..0x%llx\n",
              bad[i], bad[i] - 0x8000000, bad[i], c.HitKey ? c.HitKey : "?", c.HitBase, c.HitTop);
    }
    UINT64 top, lo = gHostFdBase + 0x1E00000 + 0x8000000;
    unsigned accepted = 0, refused = 0, laterStop = 0, stricter = 0;
    for (top = J804_M1_RAM_TOP_LIMIT; top >= lo; top -= 0x200000) {
      if (PrePiRegionClear (top, &c)) {
        accepted++;
        stop = RunReserve (top);
        if (stop != -1) { laterStop++; CHECK (0, "PrePi accepts top 0x%llx but MemoryInitPeiLib stops with %d", top, stop); }
      } else {
        refused++;
        if (RunReserve (top) == -1) {
          stricter++;
          /* Expected only for the ADT copy, which this harness's
             MemoryInitPeiLib run sees at its host buffer instead. */
          CHECK (c.HitKey && !strcmp (c.HitKey, "ADT copy"), "PrePi refuses top 0x%llx for %s, MemoryInitPeiLib does not",
                 top, c.HitKey ? c.HitKey : "no map");
        }
      }
    }
    printf ("PrePi RAM top sweep 0x%llx..0x%llx (2 MiB steps): %u accepted, %u refused; "
            "accepted-then-stopped %u; refused-but-MemoryInitPeiLib-ok %u\n",
            lo, (UINT64)J804_M1_RAM_TOP_LIMIT, accepted, refused, laterStop, stricter);
  }

  /* 4. No carve-out map -> stop code 3. */
  {
    dt_node_t *cm = dt_get ("/chosen/carveout-memory-map");
    char *nm = dt_prop (cm, "name", &len);
    char save = nm[0];
    nm[0] = 'X';
    stop = RunReserve (J804_M1_RAM_TOP_LIMIT);
    {
      J804_M1_REGION_CHECK c;
      CHECK (!PrePiRegionClear (J804_M1_RAM_TOP_LIMIT, &c) && c.HitKey == NULL, "PrePi accepts a tree without a carve-out map");
    }
    nm[0] = save;
    CHECK (stop == J804_M1_ERROR_NO_CARVEOUT_MAP, "missing carve-out map: stop code %d", stop);
    printf ("missing carve-out map: stop code %d\n", stop);
  }

  /* 5. Boot hand-off check (J804BootHandoffIsSane, the windows-mu-handoff
     ReserveBootHandoff port): the real layout passes, every rejection case
     of the original fails closed. RAM and FD as in the trimmed launch, UEFI
     region the top 128 MiB, ADT size of the saved tree. */
  {
    const UINT64 rb = 0x10005d2c000ULL, rt = J804_M1_RAM_TOP_LIMIT;
    const UINT64 fb = gHostFdBase, ft = gHostFdBase + 0x1E00000;
    const UINT64 ub = rt - 0x8000000, ut = rt;
    const UINT64 ba = 0x10220000000ULL, ad = 0x10220004000ULL, sz = (UINT64)size;
    unsigned cases = 0;
#define HANDOFF(expect, b, a, s, fbase, ftop, ubase, utop, rbase, rtop, what) do { \
      cases++; \
      CHECK (J804BootHandoffIsSane (rbase, rtop, fbase, ftop, ubase, utop, b, a, s) == (expect), \
             "boot hand-off: %s", what); } while (0)
    HANDOFF (TRUE,  ba, ad, sz, fb, ft, ub, ut, rb, rt, "real J804 layout refused");
    HANDOFF (TRUE,  ba, ad, J804_M1_ADT_COPY_MAX, fb, ft, ub, ut, rb, rt, "2 MiB ADT refused");
    HANDOFF (FALSE, ba, ad, 0, fb, ft, ub, ut, rb, rt, "zero ADT size accepted");
    HANDOFF (FALSE, ba, ad, J804_M1_ADT_COPY_MAX + 1, fb, ft, ub, ut, rb, rt, "ADT above the copy size accepted");
    HANDOFF (FALSE, ba + 0x1000, ad, sz, fb, ft, ub, ut, rb, rt, "unaligned boot_args copy accepted");
    HANDOFF (FALSE, ba, ad + 0x1000, sz, fb, ft, ub, ut, rb, rt, "unaligned ADT copy accepted");
    HANDOFF (FALSE, ba, ba, sz, fb, ft, ub, ut, rb, rt, "ADT copy on the boot_args copy accepted");
    HANDOFF (FALSE, ba, ad, sz, ba, ba + 0x1E00000, ub, ut, rb, rt, "copies inside the FD accepted");
    HANDOFF (FALSE, ba, ad, sz, ad + 0x100000, ad + 0x1F00000, ub, ut, rb, rt, "ADT copy reaching the FD accepted");
    HANDOFF (FALSE, ba, ad, sz, fb, ft, ba, ba + 0x8000000, rb, rt, "copies in the UEFI region accepted");
    HANDOFF (FALSE, ba, ad, sz, fb, ft, ub, ut, ba + 0x4000, rt, "boot_args copy below RAM accepted");
    HANDOFF (FALSE, ba, ad, sz, fb, ft, ub, ut, rb, ad + 0x100000, "ADT copy past the RAM top accepted");
    HANDOFF (FALSE, MAX_UINT64 & ~0x3FFFULL, ad, sz, fb, ft, ub, ut, rb, rt, "wrapping boot_args copy accepted");
    HANDOFF (FALSE, ba, ad, sz, fb, ft, ub, ut, rt, rb, "empty RAM accepted");
#undef HANDOFF
    printf ("boot hand-off check: %u cases, ADT 0x%llx bytes\n", cases, sz);
  }

  /* 6. Pre-copy source check (J804BootHandoffSourcesClear, J804_WINDOWS
     PrePi): the aura load_raw layout for a guest RAM window that holds the
     fixed copies passes; a source, the FD or a RAM edge on the copy range
     fails closed. Window: base 0x10200000000, top J804_M1_RAM_TOP_LIMIT;
     ADT at base + 16 MiB, FD at 64 KiB alignment after ADT and TrustCache,
     boot_args after the FD and blobs. */
  {
    const UINT64 rb = 0x10200000000ULL, rt = J804_M1_RAM_TOP_LIMIT;
    const UINT64 ba = 0x10220000000ULL, ad = 0x10220004000ULL;
    const UINT64 adtsrc = rb + 0x1000000ULL, adtsz = (UINT64)size;
    const UINT64 fdb = (adtsrc + 0x200000ULL + 0x100000ULL + 0xFFFFULL) & ~0xFFFFULL, fdsz = 0x1E00000ULL;
    const UINT64 basrc = fdb + fdsz + 0x400000ULL, basz = 0x400;
    unsigned cases = 0;
#define SOURCES(expect, rbase, rtop, fbase, fsize, bs, bsz, as, asz, bc, ac, what) do { \
      cases++; \
      CHECK (J804BootHandoffSourcesClear (rbase, rtop, fbase, fsize, bs, bsz, as, asz, bc, ac) == (expect), \
             "hand-off sources: %s", what); } while (0)
    SOURCES (TRUE,  rb, rt, fdb, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "load_raw layout refused");
    SOURCES (TRUE,  rb, rt, fdb, fdsz, basrc, basz, adtsrc, J804_M1_ADT_COPY_MAX, ba, ad, "2 MiB ADT source refused");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, adtsrc, J804_M1_ADT_COPY_MAX + 1, ba, ad, "ADT source above the copy size accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, adtsrc, 0, ba, ad, "zero ADT size accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, 0, adtsrc, adtsz, ba, ad, "zero boot_args size accepted");
    SOURCES (FALSE, rb, rt, fdb, 0, basrc, basz, adtsrc, adtsz, ba, ad, "zero FD size accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, ad + 0x1000, adtsz, ba, ad, "ADT source inside the ADT copy accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, ba - 0x1000, adtsz, ba, ad, "ADT source over the boot_args copy accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, ba, basz, adtsrc, adtsz, ba, ad, "boot_args source on its own copy accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, ad + 0x1FFC00, basz, adtsrc, adtsz, ba, ad, "boot_args source at the ADT copy end accepted");
    SOURCES (TRUE,  rb, rt, fdb, fdsz, ad + 0x200000, basz, adtsrc, adtsz, ba, ad, "boot_args source just past the ADT copy refused");
    SOURCES (FALSE, rb, rt, ba - 0x1000000, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "FD across the copies accepted");
    SOURCES (FALSE, rb, rt, ad + 0x1FC000, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "FD on the ADT copy tail accepted");
    SOURCES (TRUE,  rb, rt, ad + 0x200000, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "FD just past the ADT copy refused");
    SOURCES (FALSE, ba + 0x4000, rt, fdb, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "boot_args copy below RAM accepted");
    SOURCES (FALSE, rb, ad + 0x100000, fdb, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "ADT copy past the RAM top accepted");
    SOURCES (FALSE, rb, rb, fdb, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "empty RAM accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, adtsrc, adtsz, ba, ba + 0x2000, "overlapping copies accepted");
    SOURCES (FALSE, rb, rt, MAX_UINT64 - 0x1000, fdsz, basrc, basz, adtsrc, adtsz, ba, ad, "wrapping FD accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, MAX_UINT64 - 0x10, basz, adtsrc, adtsz, ba, ad, "wrapping boot_args source accepted");
    SOURCES (FALSE, rb, rt, fdb, fdsz, basrc, basz, adtsrc, adtsz, MAX_UINT64 - 0x1000, ad, "wrapping boot_args copy accepted");
#undef SOURCES
    printf ("hand-off source check: %u cases\n", cases);
  }

  printf ("%s (%d failures)\n", gFailures ? "FAILED" : "PASSED", gFailures);
  free (adt);
  return gFailures != 0;
}

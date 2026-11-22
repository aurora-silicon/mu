// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host test of J804GpuParseProperty, J804GpuParseHandoff and
// J804GpuPatchSsdt (AppleSiliconPkg/Include/J804GpuHandoff.h).
//
// The pages are built here field by field from the documented layout
// (the carrier's G0 contract and its
// gpu_t8152_handoff_build), not from the parser's code, and their CRC comes
// from zlib. Every address is synthetic (a made-up machine), none is a J804
// value.
//
// With two arguments, also patches a compiled SsdtGpu0.aml (argv[1]) with
// the synthetic handoff and writes it to argv[2], for the iasl round trip in
// Tests/test_j804_windows_acpi_contract.py.
//
#include <Base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <J804GpuHandoff.h>

static int  failures;

#define CHECK(cond, ...)                                   \
  do {                                                     \
    if (!(cond)) {                                         \
      failures++;                                          \
      printf ("FAIL %s:%d: ", __FILE__, __LINE__);         \
      printf (__VA_ARGS__);                                \
      printf ("\n");                                       \
    }                                                      \
  } while (0)

//
// The synthetic machine.
//
#define DRAM_BASE   0x800000000ULL
#define DRAM_SIZE   0x200000000ULL
#define RAM_BASE    0x810000000ULL     // guest RAM window = boot_args window
#define RAM_TOP     0x910000000ULL
#define POOL_BASE   0x900000000ULL     // top 256 MiB
#define POOL_SIZE   0x010000000ULL
#define HANDOFF     0x806000000ULL     // carrier heap: in DRAM, not guest RAM
#define SGX         0x440000000ULL
#define SGX_SIZE    0x004000000ULL
#define MBOX        0x360080000ULL
#define VM_BASE     0xffffff8000000000ULL
#define RECEIPT     "G18G-RTKit-0000.0.0.synthetic"

typedef struct {
  UINT64    Base, Size;
  UINT32    Class, Tag, Flags, Pad;
} RANGE;

typedef struct {
  UINT32    Magic, State, Chip, Board, Reserved0;
  UINT16    Version, Header;
  UINT32    Lines[4], Mbox[2];
  UINT64    PoolBase, PoolSize, HandoffIpa, VmBase, DramBase, DramSize;
  char      Receipt[64];
  RANGE     Ranges[J804_GPU_MAX_RANGES + 4];
  UINT32    RangeCount, PerfCount;
  // Overrides; 0 = computed.
  UINT32    RangeOffset, RangeStride, PerfOffset, PerfStride, Total;
  UINT32    PerfPad;
  UINT32    G1[11];        // 0xc0..0xec: ID words, CPU_CONTROL, CPU_STATUS
  UINT8     G1Tail;        // written at 0xec (over the low byte of Boot)
  UINT32    Boot;          // G2: 0xec, the boot-control value
  UINT8     G2Tail;        // written at 0xf0
  UINT8     HeaderTail;    // written at 0xff
} MODEL;

static void
add (MODEL *m, UINT64 base, UINT64 size, UINT32 cls, UINT32 tag)
{
  RANGE  *r = &m->Ranges[m->RangeCount++];

  memset (r, 0, sizeof (*r));
  r->Base  = base;
  r->Size  = size;
  r->Class = cls;
  r->Tag   = tag;
  r->Flags = 0x11;
}

static void
model (MODEL *m)
{
  memset (m, 0, sizeof (*m));
  m->Magic    = 0x31484741;
  m->Version  = 1;
  m->Header   = 0x100;
  m->State    = 0x3;
  m->Chip     = 0x8152;
  m->Board    = 0x22;
  m->Lines[0] = 700;
  m->Lines[1] = 600;
  m->Lines[2] = 701;
  m->Lines[3] = 601;
  m->Mbox[0]  = 1;
  m->Mbox[1]  = 2;
  m->PoolBase = POOL_BASE;
  m->PoolSize = POOL_SIZE;
  m->HandoffIpa = HANDOFF;
  m->VmBase   = VM_BASE;
  m->DramBase = DRAM_BASE;
  m->DramSize = DRAM_SIZE;
  strcpy (m->Receipt, RECEIPT);
  // The sgx window, tiled by classes 1..5, not in address order.
  add (m, SGX, 0xd10000, J804_GPU_C_SGX, 0);
  add (m, SGX + 0xd10000, 0x8000, J804_GPU_C_SGX_HOLE, 0);
  add (m, SGX + 0xd18000, 0x1338000, J804_GPU_C_SGX, 0);
  add (m, SGX + 0x2050000, 0x4000, J804_GPU_C_IOP_VBAR, 0);
  add (m, SGX + 0x2054000, 0x5ac000, J804_GPU_C_SGX, 0);
  add (m, SGX + 0x2604000, 0x70000, J804_GPU_C_ASC, 0);
  add (m, SGX + 0x2600000, 0x4000, J804_GPU_C_ASC_CTRL, 0);
  add (m, SGX + 0x2674000, 0x198c000, J804_GPU_C_SGX, 0);
  add (m, MBOX, 0x4000, J804_GPU_C_MAILBOX, 0);
  add (m, 0x9ff800000ULL, 0x4000, J804_GPU_C_FW_RW, 0);
  add (m, 0x9ff400000ULL, 0x80000, J804_GPU_C_FW_RW, 1);
  add (m, 0x802000000ULL, 0xa0000, J804_GPU_C_FW_RO, 4);
  add (m, 0x801000000ULL, 0x48000, J804_GPU_C_FW_TEXT, 0);
  add (m, POOL_BASE, POOL_SIZE, J804_GPU_C_POOL, 0);
  add (m, HANDOFF, 0x4000, J804_GPU_C_HANDOFF, 0);
  m->PerfCount = 3;
}

static void
put (UINT8 *p, UINT32 off, UINT64 v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    p[off + i] = (UINT8)(v >> (8 * i));
  }
}

static void
crc (UINT8 *p)
{
  UINT32  total = (UINT32)(p[8] | (p[9] << 8) | (p[10] << 16) | ((UINT32)p[11] << 24));

  if (total > 0x4000) {
    total = 0x4000;
  }

  put (p, 0x0c, 0, 4);
  put (p, 0x0c, crc32 (0, p, total), 4);
}

static void
build (const MODEL *m, UINT8 *p)
{
  UINT32  roff   = m->RangeOffset ? m->RangeOffset : 0x100;
  UINT32  rstr   = m->RangeStride ? m->RangeStride : 32;
  UINT32  pstr   = m->PerfStride ? m->PerfStride : 16;
  UINT32  poff   = m->PerfOffset ? m->PerfOffset : roff + m->RangeCount * 32;
  UINT32  total  = m->Total ? m->Total : poff + m->PerfCount * 16;

  memset (p, 0, 0x4000);
  put (p, 0x00, m->Magic, 4);
  put (p, 0x04, m->Version, 2);
  put (p, 0x06, m->Header, 2);
  put (p, 0x08, total, 4);
  put (p, 0x10, m->State, 4);
  put (p, 0x14, m->Chip, 4);
  put (p, 0x18, m->Board, 4);
  put (p, 0x1c, m->RangeCount, 4);
  put (p, 0x20, roff, 4);
  put (p, 0x24, rstr, 4);
  put (p, 0x28, m->PerfCount, 4);
  put (p, 0x2c, poff, 4);
  put (p, 0x30, pstr, 4);
  for (int i = 0; i < 4; i++) {
    put (p, 0x34 + 4 * i, m->Lines[i], 4);
  }

  put (p, 0x44, m->Mbox[0], 4);
  put (p, 0x48, m->Mbox[1], 4);
  put (p, 0x4c, m->Reserved0, 4);
  put (p, 0x50, m->PoolBase, 8);
  put (p, 0x58, m->PoolSize, 8);
  put (p, 0x60, m->HandoffIpa, 8);
  put (p, 0x68, m->VmBase, 8);
  put (p, 0x70, m->DramBase, 8);
  put (p, 0x78, m->DramSize, 8);
  memcpy (p + 0x80, m->Receipt, 64);
  for (int i = 0; i < 11; i++) {
    put (p, 0xc0 + 4 * i, m->G1[i], 4);
  }

  put (p, 0xec, m->Boot, 4);
  if (m->G1Tail != 0) {
    p[0xec] = m->G1Tail;
  }

  p[0xf0] = m->G2Tail;
  p[0xff] = m->HeaderTail;
  for (UINT32 i = 0; i < m->RangeCount && roff + (i + 1) * 32 <= 0x4000; i++) {
    UINT32  o = roff + i * 32;

    put (p, o, m->Ranges[i].Base, 8);
    put (p, o + 8, m->Ranges[i].Size, 8);
    put (p, o + 16, m->Ranges[i].Class, 4);
    put (p, o + 20, m->Ranges[i].Tag, 4);
    put (p, o + 24, m->Ranges[i].Flags, 4);
    put (p, o + 28, m->Ranges[i].Pad, 4);
  }

  for (UINT32 i = 0; i < m->PerfCount && poff + (i + 1) * 16 <= 0x4000; i++) {
    UINT32  o = poff + i * 16;

    put (p, o, 400000000 + 50000000 * i, 4);
    put (p, o + 4, 650 + 10 * i, 4);
    put (p, o + 8, 760, 4);
    put (p, o + 12, i == 0 ? m->PerfPad : 0, 4);
  }

  crc (p);
}

static UINT8  page[0x4000];

static J804_GPU_STATUS
parse (const MODEL *m, J804_GPU_HANDOFF *out)
{
  build (m, page);
  return J804GpuParseHandoff (page, HANDOFF, RAM_BASE, RAM_TOP, out);
}

static void
expect (const char *name, const MODEL *m, J804_GPU_STATUS want)
{
  J804_GPU_HANDOFF  out;
  J804_GPU_HANDOFF  zero;
  J804_GPU_STATUS   got = parse (m, &out);

  memset (&zero, 0, sizeof (zero));
  CHECK (got == want, "%s: status %d, want %d", name, got, want);
  if (want != J804GpuOk) {
    CHECK (memcmp (&out, &zero, sizeof (out)) == 0, "%s: output not zeroed", name);
  }
}

#define MUTATE(name, want, stmt)  \
  do {                            \
    MODEL  m;                     \
    model (&m);                   \
    stmt;                         \
    expect (name, &m, want);      \
  } while (0)

static void
test_valid (void)
{
  MODEL             m;
  J804_GPU_HANDOFF  h;

  model (&m);
  CHECK (parse (&m, &h) == J804GpuOk, "valid page refused");
  CHECK (h.HandoffIpa == HANDOFF && h.HandoffSize == 0x4000, "handoff");
  CHECK (h.SgxBase == SGX && h.SgxSize == SGX_SIZE, "sgx 0x%llx+0x%llx", (unsigned long long)h.SgxBase,
         (unsigned long long)h.SgxSize);
  CHECK (h.MailboxBase == MBOX && h.MailboxSize == 0x4000, "mailbox");
  CHECK (h.PoolBase == POOL_BASE && h.PoolSize == POOL_SIZE, "pool");
  CHECK (h.DramBase == DRAM_BASE && h.DramSize == DRAM_SIZE, "dram");
  CHECK (h.State == 3 && h.RangeCount == 15 && h.PerfCount == 3, "counts");
  CHECK (h.Lines[0] == 700 && h.Lines[1] == 600 && h.Lines[2] == 701 && h.Lines[3] == 601, "lines");
  CHECK (h.MailboxNum[0] == 1 && h.MailboxNum[1] == 2, "mailbox numbers");
  CHECK (strcmp (h.Receipt, RECEIPT) == 0, "receipt");

  // The page is read only through its pointer: a CRC over [0, total) with
  // the bytes past the total ignored.
  page[0x3fff] = 0xAA;
  CHECK (J804GpuParseHandoff (page, HANDOFF, RAM_BASE, RAM_TOP, &h) == J804GpuOk, "bytes past the total");

  // Every state the carrier may set later is accepted (GPU0 still lists no
  // interrupt); bit 7 and above are not.
  MUTATE ("state 0x7f", J804GpuOk, m.State = 0x7f);
  // G1 (carrier probe mode): powered, RUN guarded, MMIO forwarded,
  // firmware mapped, with the power-on readings at 0xc0..0xec.
  MUTATE ("G1 readings when powered", J804GpuOk, (m.State = 0x75, m.G1[0] = 0x12345678, m.G1[6] = 0xfff,
                                                  m.G1[7] = 0x10, m.G1[8] = 0x10, m.G1[9] = 0xe, m.G1[10] = 0xe));
  MUTATE ("state admitted only", J804GpuOk, m.State = 0x1);
  // G2 (carrier g2 mode): the boot-control bit with its value, every state bit.
  MUTATE ("G2 boot control when powered", J804GpuOk, (m.State = 0xf5, m.Boot = 0x701));
  MUTATE ("state 0xff", J804GpuOk, (m.State = 0xff, m.Boot = 0x701));
  MUTATE ("G2 boot control bit, value zero", J804GpuOk, m.State = 0xf5);
  MUTATE ("pool of half of RAM", J804GpuOk, (m.PoolBase = RAM_TOP - 0x80000000ULL, m.PoolSize = 0x80000000ULL,
                                             m.Ranges[13].Base = m.PoolBase, m.Ranges[13].Size = m.PoolSize));
  MUTATE ("32 MiB pool", J804GpuOk, (m.PoolBase = RAM_TOP - 0x2000000, m.PoolSize = 0x2000000,
                                     m.Ranges[13].Base = m.PoolBase, m.Ranges[13].Size = m.PoolSize));
}

static void
test_refusals (void)
{
  MUTATE ("magic", J804GpuBadMagic, m.Magic = 0x32484741);
  MUTATE ("version", J804GpuBadVersion, m.Version = 2);
  MUTATE ("header size", J804GpuBadVersion, m.Header = 0x80);
  MUTATE ("total too big", J804GpuBadTotal, m.Total = 0x4004);
  MUTATE ("total too small", J804GpuBadTotal, m.Total = 0xfc);
  MUTATE ("total short of the tables", J804GpuBadLayout, m.Total = 0x100 + 15 * 32 + 3 * 16 - 16);
  MUTATE ("total past the tables", J804GpuBadLayout, m.Total = 0x100 + 15 * 32 + 3 * 16 + 16);
  MUTATE ("chip", J804GpuBadIdentity, m.Chip = 0x8142);
  MUTATE ("board", J804GpuBadIdentity, m.Board = 0x24);
  MUTATE ("not admitted", J804GpuBadState, m.State = 0x2);
  MUTATE ("unknown state bit", J804GpuBadState, m.State = 0x103);
  MUTATE ("boot control without power", J804GpuBadState, (m.State = 0x83, m.Boot = 0x701));
  MUTATE ("boot control value without the bit", J804GpuBadReserved, (m.State = 0x75, m.Boot = 0x701));
  MUTATE ("byte after the G2 fields", J804GpuBadReserved, (m.State = 0xf5, m.Boot = 0x701, m.G2Tail = 1));
  MUTATE ("reserved word", J804GpuBadReserved, m.Reserved0 = 1);
  MUTATE ("header tail", J804GpuBadReserved, m.HeaderTail = 1);
  MUTATE ("G1 readings before power-on", J804GpuBadReserved, (m.State = 0x3, m.G1[0] = 1));
  MUTATE ("G1 CPU_STATUS before power-on", J804GpuBadReserved, (m.State = 0x3, m.G1[10] = 0xe));
  MUTATE ("byte after the G1 fields", J804GpuBadReserved, (m.State = 0x75, m.G1[0] = 1, m.G1Tail = 1));
  MUTATE ("perf pad", J804GpuBadReserved, m.PerfPad = 1);
  MUTATE ("range pad", J804GpuBadReserved, m.Ranges[3].Pad = 1);
  MUTATE ("range offset", J804GpuBadLayout, m.RangeOffset = 0x120);
  MUTATE ("range stride", J804GpuBadLayout, m.RangeStride = 24);
  MUTATE ("perf stride", J804GpuBadLayout, m.PerfStride = 8);
  MUTATE ("perf offset", J804GpuBadLayout, m.PerfOffset = 0x100 + 14 * 32);
  MUTATE ("no ranges", J804GpuBadLayout, (m.RangeCount = 0, m.PerfOffset = 0x100));
  MUTATE ("no perf states", J804GpuBadLayout, m.PerfCount = 0);
  MUTATE ("33 perf states", J804GpuBadLayout, m.PerfCount = 33);
  MUTATE ("129 ranges", J804GpuBadLayout, m.RangeCount = 129);
  MUTATE ("unaligned base", J804GpuBadRange, m.Ranges[9].Base += 0x1000);
  MUTATE ("unaligned size", J804GpuBadRange, m.Ranges[9].Size = 0x5000);
  MUTATE ("empty range", J804GpuBadRange, m.Ranges[9].Size = 0);
  MUTATE ("overflowing range", J804GpuBadRange, (m.Ranges[9].Base = 0xffffffffffffc000ULL, m.Ranges[9].Size = 0x8000));
  MUTATE ("class 0", J804GpuBadRange, m.Ranges[10].Class = 0);
  MUTATE ("class 13", J804GpuBadRange, m.Ranges[10].Class = 13);
  MUTATE ("class 12 before table ranges", J804GpuBadFwMmio, m.Ranges[10].Class = 12);
  MUTATE ("overlap", J804GpuBadOverlap, m.Ranges[10].Base = 0x9ff800000ULL);
  MUTATE ("fw outside DRAM", J804GpuBadPlacement, m.Ranges[11].Base = 0x7f0000000ULL);
  MUTATE ("text in guest RAM", J804GpuBadPlacement, m.Ranges[12].Base = RAM_BASE + 0x100000);
  MUTATE ("mailbox in DRAM", J804GpuBadPlacement, m.Ranges[8].Base = 0x808000000ULL);
  MUTATE ("sgx in guest RAM", J804GpuBadPlacement, (m.Ranges[7].Base = RAM_BASE, m.Ranges[7].Size = 0x4000));
  MUTATE ("handoff in guest RAM", J804GpuBadPlacement, m.Ranges[14].Base = RAM_BASE);
  MUTATE ("guest RAM outside DRAM", J804GpuBadPlacement, m.DramSize = 0x100000000ULL);
  MUTATE ("no DRAM", J804GpuBadPlacement, m.DramSize = 0);
  MUTATE ("sgx hole moved away", J804GpuBadSgx, m.Ranges[1].Base = 0x500000000ULL);
  MUTATE ("sgx range moved away", J804GpuBadSgx, m.Ranges[4].Base = 0x500000000ULL);
  MUTATE ("two mailboxes", J804GpuBadMailbox, add (&m, MBOX + 0x4000, 0x4000, J804_GPU_C_MAILBOX, 0));
  MUTATE ("no mailbox", J804GpuBadMailbox, (m.Ranges[8] = m.Ranges[14], m.RangeCount = 14));
  MUTATE ("mailbox of two pages", J804GpuBadMailbox, m.Ranges[8].Size = 0x8000);
  MUTATE ("pool header differs", J804GpuBadPool, m.PoolSize = POOL_SIZE / 2);
  MUTATE ("pool range differs", J804GpuBadPool, (m.Ranges[13].Base += 0x2000000, m.Ranges[13].Size -= 0x2000000));
  MUTATE ("pool below the top", J804GpuBadPool, (m.PoolBase -= 0x2000000, m.Ranges[13].Base = m.PoolBase));
  MUTATE ("pool not 32 MiB aligned", J804GpuBadPool, (m.PoolBase += 0x1000000, m.PoolSize -= 0x1000000,
                                                       m.Ranges[13].Base = m.PoolBase, m.Ranges[13].Size = m.PoolSize));
  MUTATE ("pool over half", J804GpuBadPool, (m.PoolBase = RAM_TOP - 0x82000000ULL, m.PoolSize = 0x82000000ULL,
                                             m.Ranges[13].Base = m.PoolBase, m.Ranges[13].Size = m.PoolSize));
  MUTATE ("no pool", J804GpuBadPool, m.RangeCount = 13);
  MUTATE ("pool past guest RAM", J804GpuBadPlacement, (m.PoolBase = RAM_TOP, m.Ranges[13].Base = RAM_TOP));
  MUTATE ("handoff range elsewhere", J804GpuBadHandoff, m.Ranges[14].Base = HANDOFF + 0x4000);
  MUTATE ("handoff header elsewhere", J804GpuBadHandoff, m.HandoffIpa = HANDOFF + 0x4000);
  MUTATE ("no handoff range", J804GpuBadHandoff, m.Ranges[14].Class = J804_GPU_C_FW_RO);
  MUTATE ("line below 32", J804GpuBadLines, m.Lines[0] = 31);
  MUTATE ("line at the GSIV limit", J804GpuBadLines, m.Lines[1] = 1020);
  MUTATE ("line in the carrier block", J804GpuBadLines, m.Lines[2] = 990);
  MUTATE ("repeated line", J804GpuBadLines, m.Lines[3] = 700);
  MUTATE ("mailbox number 0", J804GpuBadLines, m.Mbox[0] = 0);
  MUTATE ("same mailbox numbers", J804GpuBadLines, m.Mbox[1] = 1);
  MUTATE ("empty receipt", J804GpuBadReceipt, memset (m.Receipt, 0, 64));
  MUTATE ("unterminated receipt", J804GpuBadReceipt, memset (m.Receipt, 'A', 64));
  MUTATE ("receipt control byte", J804GpuBadReceipt, m.Receipt[3] = '\n');
  MUTATE ("receipt garbage after NUL", J804GpuBadReceipt, m.Receipt[60] = 'x');

  // CRC: one flipped byte anywhere in [0, total) after the CRC was computed.
  {
    MODEL             m;
    J804_GPU_HANDOFF  h;

    model (&m);
    build (&m, page);
    page[0x100 + 5 * 32 + 3] ^= 0x40;
    CHECK (J804GpuParseHandoff (page, HANDOFF, RAM_BASE, RAM_TOP, &h) == J804GpuBadCrc, "flipped byte");
    build (&m, page);
    CHECK (J804GpuParseHandoff (page, HANDOFF + 0x4000, RAM_BASE, RAM_TOP, &h) == J804GpuBadHandoff,
           "page at another IPA than the handshake");
    CHECK (J804GpuParseHandoff (page, HANDOFF, RAM_TOP, RAM_BASE, &h) == J804GpuBadPropertyPage, "empty RAM");
    CHECK (J804GpuParseHandoff (NULL, HANDOFF, RAM_BASE, RAM_TOP, &h) == J804GpuBadPropertyPage, "NULL page");
  }
}

//
// G2: class-12 firmware register windows, declared after every other range.
// Synthetic windows: two overlapping ones in the first sgx page (unaligned,
// as the firmware's are), one in the sgx hole's page, one outside the sgx
// window (an interrupt controller page, say).
//
static void
g2_model (MODEL *m)
{
  model (m);
  m->State = 0xf5;
  m->Boot  = 0x701;
  add (m, SGX, 0x36000, J804_GPU_C_FW_MMIO, 17);
  add (m, SGX + 0x1800, 0x27f8, J804_GPU_C_FW_MMIO, 29);
  add (m, SGX + 0xd12200, 0xfc, J804_GPU_C_FW_MMIO, 30);
  add (m, 0x280008000ULL, 0x4000, J804_GPU_C_FW_MMIO, 0);
}

#define G2_MUTATE(name, want, stmt)  \
  do {                               \
    MODEL  m;                        \
    g2_model (&m);                   \
    stmt;                            \
    expect (name, &m, want);         \
  } while (0)

static void
test_g2 (void)
{
  MODEL             m;
  J804_GPU_HANDOFF  h;

  g2_model (&m);
  CHECK (parse (&m, &h) == J804GpuOk, "G2 page refused");
  CHECK (h.FwMmioCount == 4 && h.RangeCount == 19, "G2 counts %u/%u", h.FwMmioCount, h.RangeCount);
  CHECK (h.BootControl == 0x701 && h.State == 0xf5, "G2 boot control");
  CHECK (h.SgxBase == SGX && h.SgxSize == SGX_SIZE, "G2 sgx window unchanged");
  CHECK (h.MailboxBase == MBOX && h.PoolBase == POOL_BASE, "G2 mailbox and pool unchanged");
  model (&m);
  CHECK (parse (&m, &h) == J804GpuOk && h.FwMmioCount == 0 && h.BootControl == 0, "G1 page: no G2 fields");

  G2_MUTATE ("fw-mmio without the boot bit", J804GpuOk, (m.State = 0x75, m.Boot = 0));
  G2_MUTATE ("fw-mmio unaligned base", J804GpuBadFwMmio, m.Ranges[16].Base += 2);
  G2_MUTATE ("fw-mmio unaligned size", J804GpuBadFwMmio, m.Ranges[16].Size = 0x27f9);
  G2_MUTATE ("fw-mmio empty", J804GpuBadFwMmio, m.Ranges[16].Size = 0);
  G2_MUTATE ("fw-mmio overflowing", J804GpuBadFwMmio, m.Ranges[18].Base = 0xfffffffffffff000ULL);
  G2_MUTATE ("fw-mmio in DRAM", J804GpuBadFwMmio, m.Ranges[18].Base = 0x808000000ULL);
  G2_MUTATE ("fw-mmio in guest RAM", J804GpuBadFwMmio, m.Ranges[18].Base = RAM_BASE + 0x8000);
  G2_MUTATE ("fw-mmio page touches the mailbox", J804GpuBadFwMmio, (m.Ranges[18].Base = MBOX + 0x3f00,
                                                                     m.Ranges[18].Size = 0x10));
  G2_MUTATE ("fw-mmio touches CPU_CONTROL", J804GpuBadFwMmio, m.Ranges[18].Base = SGX + 0x2600000);
  G2_MUTATE ("fw-mmio page touches iop-vbar", J804GpuBadFwMmio, (m.Ranges[18].Base = SGX + 0x2053ff0,
                                                                   m.Ranges[18].Size = 0x10));
  G2_MUTATE ("fw-mmio touches the rest of an ASC", J804GpuBadFwMmio, m.Ranges[18].Base = SGX + 0x2604000);
  G2_MUTATE ("fw-mmio pad", J804GpuBadReserved, m.Ranges[17].Pad = 1);
  G2_MUTATE ("table range after fw-mmio", J804GpuBadFwMmio,
             (m.Ranges[19] = m.Ranges[14], m.Ranges[14] = m.Ranges[18], m.Ranges[18] = m.Ranges[19],
              m.RangeCount = 19));
  G2_MUTATE ("too many ranges with fw-mmio", J804GpuBadLayout, m.RangeCount = 129);
}

static void
test_property (void)
{
  UINT8              v[24];
  J804_GPU_PROPERTY  p;

  memset (v, 0xA5, sizeof (v));
  put (v, 0, HANDOFF, 8);
  put (v, 8, 0x4000, 8);
  CHECK (J804GpuParseProperty (v, 16, &p) == J804GpuOk && p.Ipa == HANDOFF && p.Size == 0x4000, "valid property");
  CHECK (J804GpuParseProperty (v, 15, &p) == J804GpuBadPropertyLength && p.Ipa == 0, "15 bytes");
  CHECK (J804GpuParseProperty (v, 24, &p) == J804GpuBadPropertyLength, "24 bytes");
  CHECK (J804GpuParseProperty (NULL, 16, &p) == J804GpuBadPropertyLength, "NULL");
  // ADT property values are 4-byte aligned, not 8: parse at an odd offset.
  memmove (v + 4, v, 16);
  CHECK (J804GpuParseProperty (v + 4, 16, &p) == J804GpuOk && p.Ipa == HANDOFF, "4-byte aligned value");
  put (v, 0, HANDOFF, 8);
  put (v, 8, 0x8000, 8);
  CHECK (J804GpuParseProperty (v, 16, &p) == J804GpuBadPropertyPage, "size 0x8000");
  put (v, 8, 0x4000, 8);
  put (v, 0, HANDOFF + 0x1000, 8);
  CHECK (J804GpuParseProperty (v, 16, &p) == J804GpuBadPropertyPage, "unaligned IPA");
  put (v, 0, 0, 8);
  CHECK (J804GpuParseProperty (v, 16, &p) == J804GpuBadPropertyPage, "IPA 0");
  put (v, 0, 0xffffffffffffc000ULL, 8);
  CHECK (J804GpuParseProperty (v, 16, &p) == J804GpuBadPropertyPage, "overflowing page");
}

static void
test_crc (void)
{
  static const UINT8  check[] = "123456789xxxx";

  CHECK (J804GpuCrc32 (check, 9, 100) == 0xCBF43926U, "CRC-32 check value");
  CHECK (J804GpuCrc32 (check, 13, 9) == (UINT32)crc32 (0, (const UINT8 *)"123456789\0\0\0\0", 13), "skip field");
}

//
// A minimal SSDT holding each sentinel once, in an order unlike the template.
//
static UINTN
fake_ssdt (UINT8 *t, int drop, int dup)
{
  UINTN  len = 36;
  int    n   = 0;

  memset (t, 0, 512);
  memcpy (t, "SSDT", 4);
  memcpy (t + 10, "AURORA", 6);
  memcpy (t + 16, "J804GPU0", 8);
  for (int k = 4; k >= 1; k--) {
    UINT64  s[3] = { J804_GPU_SENTINEL_LEN (k), J804_GPU_SENTINEL_MIN (k), J804_GPU_SENTINEL_MAX (k) };

    for (int i = 0; i < 3; i++, n++) {
      if (n == drop) {
        continue;
      }

      t[len++] = 0x0E;
      put (t, (UINT32)len, s[i], 8);
      len += 8;
      if (n == dup) {
        t[len++] = 0x0E;
        put (t, (UINT32)len, s[i], 8);
        len += 8;
      }
    }
  }

  t[len++] = 0x0E;
  put (t, (UINT32)len, J804_GPU_SENTINEL_IPA, 8);
  len += 8;
  put (t, 4, len, 4);
  return len;
}

static int
has (const UINT8 *t, UINTN len, UINT64 v)
{
  int  n = 0;

  for (UINTN i = 0; i + 8 <= len; i++) {
    UINT64  x = 0;

    for (int b = 7; b >= 0; b--) {
      x = (x << 8) | t[i + b];
    }

    n += (x == v);
  }

  return n;
}

static void
test_patch (const J804_GPU_HANDOFF *h)
{
  UINT8   t[512];
  UINT8   copy[512];
  UINTN   len;
  UINT8   sum = 0;

  len = fake_ssdt (t, -1, -1);
  CHECK (J804GpuPatchSsdt (t, len, h), "patch the fake SSDT");
  for (UINTN i = 0; i < len; i++) {
    sum = (UINT8)(sum + t[i]);
  }

  CHECK (sum == 0, "checksum");
  CHECK (has (t, len, SGX) == 1 && has (t, len, SGX + SGX_SIZE - 1) == 1 && has (t, len, SGX_SIZE) == 1, "sgx values");
  CHECK (has (t, len, MBOX) == 1 && has (t, len, MBOX + 0x3fff) == 1, "mailbox values");
  CHECK (has (t, len, POOL_BASE) == 1 && has (t, len, POOL_BASE + POOL_SIZE - 1) == 1 && has (t, len, POOL_SIZE) == 1, "pool values");
  CHECK (has (t, len, HANDOFF) == 2 && has (t, len, HANDOFF + 0x3fff) == 1, "handoff values (_CRS and _DSD)");
  CHECK (has (t, len, 0x4000) == 2, "two 16 KiB lengths");
  for (int k = 1; k <= 4; k++) {
    CHECK (!has (t, len, J804_GPU_SENTINEL_MIN (k)) && !has (t, len, J804_GPU_SENTINEL_MAX (k)) &&
           !has (t, len, J804_GPU_SENTINEL_LEN (k)), "sentinel %d left", k);
  }

  CHECK (!J804GpuPatchSsdt (t, len, h), "a patched table has no sentinels left");

  for (int n = 0; n < 12; n += 5) {
    len = fake_ssdt (t, n, -1);
    memcpy (copy, t, len);
    CHECK (!J804GpuPatchSsdt (t, len, h) && memcmp (copy, t, len) == 0, "missing sentinel %d: refused, untouched", n);
    len = fake_ssdt (t, -1, n);
    memcpy (copy, t, len);
    CHECK (!J804GpuPatchSsdt (t, len, h) && memcmp (copy, t, len) == 0, "repeated sentinel %d: refused, untouched", n);
  }

  len = fake_ssdt (t, -1, -1);
  CHECK (!J804GpuPatchSsdt (t, len - 1, h), "length differs from the header");
  t[0] = 'D';
  CHECK (!J804GpuPatchSsdt (t, len, h), "not an SSDT");
}

static int
patch_file (const char *in, const char *out, const J804_GPU_HANDOFF *h)
{
  static UINT8  t[1 << 16];
  FILE          *f = fopen (in, "rb");
  size_t        len;

  if (f == NULL) {
    perror (in);
    return 1;
  }

  len = fread (t, 1, sizeof (t), f);
  fclose (f);
  if (!J804GpuPatchSsdt (t, len, h)) {
    printf ("FAIL: %s is not a patchable GPU0 template\n", in);
    return 1;
  }

  f = fopen (out, "wb");
  if ((f == NULL) || (fwrite (t, 1, len, f) != len)) {
    perror (out);
    return 1;
  }

  fclose (f);
  printf ("patched %s -> %s (%zu bytes)\n", in, out, len);
  return 0;
}

int
main (int argc, char **argv)
{
  MODEL             m;
  J804_GPU_HANDOFF  h;

  test_crc ();
  test_property ();
  test_valid ();
  test_refusals ();
  test_g2 ();
  model (&m);
  if (parse (&m, &h) != J804GpuOk) {
    printf ("FAIL: the valid model does not parse\n");
    return 1;
  }

  test_patch (&h);
  if ((argc == 3) && (patch_file (argv[1], argv[2], &h) != 0)) {
    failures++;
  }

  if (failures) {
    printf ("J804_GPU_HANDOFF_TEST FAIL (%d)\n", failures);
  } else {
    printf ("J804_GPU_HANDOFF_TEST PASS\n");
  }

  return failures ? 1 : 0;
}

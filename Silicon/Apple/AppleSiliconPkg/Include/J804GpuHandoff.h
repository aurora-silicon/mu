/** @file
  J804 windows profile, G0 GPU (J804_WINDOWS_GPU builds only): parse the
  launcher's /chosen GPU handshake and the carrier's read-only GPU handoff page
  ("AGH1", version 1), and patch the GPU0 SSDT template.

  The carrier (aura WINDOWS=1 on T8152) admits the GPU firmware, builds the
  handoff page from the live ADT and maps it read-only at IPA == PA. Only after
  that does the launcher write /chosen/aura,windows-gpu-v1 (16 bytes: page IPA
  and size). Nothing here is a per-machine constant: every address, size and
  interrupt line comes from the page at run time, so this header holds only
  the page layout and structural rules.

  Header-only and free of UEFI services so that the same code runs in PrePi
  (MMU off: every byte is read on its own through a volatile pointer, so the
  compiler cannot merge reads into unaligned wider loads on device memory), in
  J804GpuAcpiDxe and in the host test (Tools/HostTests/J804GpuHandoff).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_GPU_HANDOFF_H_
#define J804_GPU_HANDOFF_H_

//
// /chosen handshake, little-endian, 16 bytes: u64 handoff page IPA, u64 size.
//
#define J804_GPU_ADT_PROPERTY       "aura,windows-gpu-v1"
#define J804_GPU_ADT_PROPERTY_SIZE  16

//
// Handoff page v1.
//
#define J804_GPU_HANDOFF_MAGIC    0x31484741U  // "AGH1"
#define J804_GPU_HANDOFF_VERSION  1
#define J804_GPU_HANDOFF_HEADER   0x100
#define J804_GPU_HANDOFF_SIZE     0x4000ULL
#define J804_GPU_RANGE_STRIDE     32           // u64 base, u64 size, u32 class, u32 tag, u32 flags, u32 0
#define J804_GPU_PERF_STRIDE      16           // u32 Hz, u32 core mV, u32 SRAM mV, u32 0
#define J804_GPU_MAX_RANGES       128          // carrier table capacity is 96
#define J804_GPU_MAX_PERF         32
#define J804_GPU_RECEIPT_SIZE     64
#define J804_GPU_LINES            4            // gfx-asc 0, 1; gfx1-asc 0, 1
#define J804_GPU_PAGE             0x4000ULL    // stage-2 granule; every range is aligned to it
#define J804_GPU_POOL_ALIGN       0x2000000ULL // 32 MiB: one level-2 block with 16 KiB pages
#define J804_GPU_CHIP_ID          0x8152
#define J804_GPU_BOARD_ID         0x22         // J804

//
// Header field offsets.
//
#define J804_GPU_HO_MAGIC         0x00
#define J804_GPU_HO_VERSION       0x04         // u16
#define J804_GPU_HO_HEADER        0x06         // u16
#define J804_GPU_HO_TOTAL         0x08
#define J804_GPU_HO_CRC           0x0c
#define J804_GPU_HO_STATE         0x10
#define J804_GPU_HO_CHIP          0x14
#define J804_GPU_HO_BOARD         0x18
#define J804_GPU_HO_RANGE_COUNT   0x1c
#define J804_GPU_HO_RANGE_OFFSET  0x20
#define J804_GPU_HO_RANGE_STRIDE  0x24
#define J804_GPU_HO_PERF_COUNT    0x28
#define J804_GPU_HO_PERF_OFFSET   0x2c
#define J804_GPU_HO_PERF_STRIDE   0x30
#define J804_GPU_HO_LINES         0x34         // u32[4]
#define J804_GPU_HO_MAILBOX_NUM   0x44         // u32[2]
#define J804_GPU_HO_RESERVED0     0x4c         // u32, zero
#define J804_GPU_HO_POOL_BASE     0x50
#define J804_GPU_HO_POOL_SIZE     0x58
#define J804_GPU_HO_HANDOFF_IPA   0x60
#define J804_GPU_HO_VM_BASE       0x68
#define J804_GPU_HO_DRAM_BASE     0x70
#define J804_GPU_HO_DRAM_SIZE     0x78
#define J804_GPU_HO_RECEIPT       0x80         // char[64], NUL padded
#define J804_GPU_HO_RESERVED1     0xc0         // to the header end, zero

//
// Range classes (carrier enum gpu_class).
//
#define J804_GPU_C_SGX       1   // sgx register window
#define J804_GPU_C_SGX_HOLE  2   // another node's registers inside the window
#define J804_GPU_C_ASC_CTRL  3   // wrapper page with CPU_CONTROL/STATUS
#define J804_GPU_C_ASC       4   // rest of an ASC wrapper
#define J804_GPU_C_IOP_VBAR  5   // page holding an iop-vbar word
#define J804_GPU_C_MAILBOX   6   // post-office page with both GFX windows
#define J804_GPU_C_FW_RW     7   // UAT regions
#define J804_GPU_C_FW_RO     8   // __DATA, __DATA_SHARED_RO
#define J804_GPU_C_FW_TEXT   9   // __TEXT, never mapped
#define J804_GPU_C_POOL      10  // GPU pool inside guest RAM
#define J804_GPU_C_HANDOFF   11  // this page
#define J804_GPU_C_COUNT     12

//
// State bits (offset 0x10): what the carrier has done.
//
#define J804_GPU_S_ADMITTED   0x01
#define J804_GPU_S_HOOKS_LOG  0x02
#define J804_GPU_S_POWERED    0x04
#define J804_GPU_S_IRQ_LIVE   0x08
#define J804_GPU_S_ASC_RUN    0x10
#define J804_GPU_S_MMIO_PASS  0x20
#define J804_GPU_S_FW_MAPPED  0x40
#define J804_GPU_S_KNOWN      0x7f

//
// Interrupt lines: GIC SPIs below the GSIV limit, outside the carrier's own
// block of published numbers.
//
#define J804_GPU_GSIV_MIN            32
#define J804_GPU_GSIV_LIMIT          1020
#define J804_GPU_CARRIER_BLOCK_BASE  980
#define J804_GPU_CARRIER_BLOCK_TOP   1000

typedef struct {
  UINT64    Ipa;
  UINT64    Size;
} J804_GPU_PROPERTY;

typedef struct {
  UINT64    HandoffIpa;
  UINT64    HandoffSize;
  UINT32    State;
  UINT32    RangeCount;
  UINT32    PerfCount;
  UINT32    Lines[J804_GPU_LINES];
  UINT32    MailboxNum[2];
  UINT64    SgxBase;          // union of classes 1..5: one contiguous window
  UINT64    SgxSize;
  UINT64    MailboxBase;      // the one class-6 page
  UINT64    MailboxSize;
  UINT64    PoolBase;
  UINT64    PoolSize;
  UINT64    DramBase;
  UINT64    DramSize;
  CHAR8     Receipt[J804_GPU_RECEIPT_SIZE];
} J804_GPU_HANDOFF;

typedef enum {
  J804GpuOk = 0,
  J804GpuBadPropertyLength,
  J804GpuBadPropertyPage,
  J804GpuBadMagic,
  J804GpuBadVersion,
  J804GpuBadTotal,
  J804GpuBadCrc,
  J804GpuBadIdentity,
  J804GpuBadState,
  J804GpuBadReserved,
  J804GpuBadLayout,
  J804GpuBadRange,
  J804GpuBadOverlap,
  J804GpuBadPlacement,
  J804GpuBadSgx,
  J804GpuBadMailbox,
  J804GpuBadPool,
  J804GpuBadHandoff,
  J804GpuBadLines,
  J804GpuBadReceipt,
  J804GpuStatusCount
} J804_GPU_STATUS;

//
// One line per J804_GPU_STATUS, for the refusal messages.
//
#define J804_GPU_STATUS_TEXT                                                                \
  "ok",                                                                                     \
  "handshake length is not 16 bytes",                                                       \
  "handshake page IPA/size unusable (size not 16 KiB, IPA zero, unaligned or overflowing)", \
  "magic is not AGH1",                                                                      \
  "version or header size is not 1 / 0x100",                                                \
  "total size outside [0x100, 0x4000]",                                                     \
  "CRC-32 mismatch",                                                                        \
  "chip/board is not T8152/J804",                                                           \
  "state lacks the admitted bit or has unknown bits",                                       \
  "reserved bytes are not zero",                                                            \
  "range/perf table offsets, strides or counts do not fit the page",                        \
  "a range is empty, unaligned, overflowing or of an unknown class",                        \
  "two ranges overlap",                                                                     \
  "a range is in guest RAM (or the pool is not), or a firmware range is outside DRAM",      \
  "classes 1..5 do not form one contiguous sgx window",                                     \
  "not exactly one 16 KiB mailbox page",                                                    \
  "pool is not one 32 MiB-aligned block at the top of guest RAM, at most half of it",       \
  "handoff range or header IPA differs from the handshake",                                 \
  "interrupt lines or mailbox numbers out of range or repeated",                            \
  "receipt is not NUL-terminated printable ASCII"

//
// Little-endian read of Count bytes, one byte at a time (never merged into a
// wider, possibly unaligned load).
//
STATIC
UINT64
J804GpuLe (
  IN CONST volatile UINT8  *Bytes,
  IN UINTN                 Count
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
  CRC-32 as zlib and EDK2's CalculateCrc32 compute it (reflected 0xEDB88320,
  initial and final value 0xFFFFFFFF), with the four bytes at SkipOffset
  taken as zero.
**/
STATIC
UINT32
J804GpuCrc32 (
  IN CONST volatile UINT8  *Bytes,
  IN UINTN                 Length,
  IN UINTN                 SkipOffset
  )
{
  UINT32  Crc;
  UINTN   Index;
  UINTN   Bit;
  UINT8   Byte;

  Crc = 0xFFFFFFFFU;
  for (Index = 0; Index < Length; Index++) {
    Byte = ((Index >= SkipOffset) && (Index < SkipOffset + 4)) ? 0 : Bytes[Index];
    Crc ^= Byte;
    for (Bit = 0; Bit < 8; Bit++) {
      Crc = (Crc >> 1) ^ (0xEDB88320U & (UINT32)(0U - (Crc & 1U)));
    }
  }

  return ~Crc;
}

/**
  TRUE when [Base0, Top0) and [Base1, Top1) share a byte (half-open; an empty
  range overlaps nothing).
**/
STATIC
BOOLEAN
J804GpuOverlap (
  IN UINT64  Base0,
  IN UINT64  Top0,
  IN UINT64  Base1,
  IN UINT64  Top1
  )
{
  return (BOOLEAN)((Base0 < Top0) && (Base1 < Top1) && (Base0 < Top1) && (Base1 < Top0));
}

/**
  Decode /chosen/aura,windows-gpu-v1. The page must be one 16 KiB page at a
  non-zero, 16 KiB-aligned IPA. On failure Out is zeroed.
**/
STATIC
J804_GPU_STATUS
J804GpuParseProperty (
  IN  CONST VOID         *Value,
  IN  UINTN              Length,
  OUT J804_GPU_PROPERTY  *Out
  )
{
  CONST volatile UINT8  *Bytes;
  UINT64                Ipa;
  UINT64                Size;

  Out->Ipa  = 0;
  Out->Size = 0;
  if ((Value == NULL) || (Length != J804_GPU_ADT_PROPERTY_SIZE)) {
    return J804GpuBadPropertyLength;
  }

  Bytes = (CONST volatile UINT8 *)Value;
  Ipa   = J804GpuLe (Bytes, 8);
  Size  = J804GpuLe (Bytes + 8, 8);
  if ((Size != J804_GPU_HANDOFF_SIZE) || (Ipa == 0) ||
      ((Ipa & (J804_GPU_PAGE - 1)) != 0) || (Ipa + Size < Ipa))
  {
    return J804GpuBadPropertyPage;
  }

  Out->Ipa  = Ipa;
  Out->Size = Size;
  return J804GpuOk;
}

/**
  Validate the handoff page and extract what GPU0 needs.

  @param[in]  Page          The page, J804_GPU_HANDOFF_SIZE readable bytes.
  @param[in]  Ipa           The IPA the handshake named for it.
  @param[in]  GuestRamBase  boot_args phys_base: the carrier's guest RAM window
  @param[in]  GuestRamTop   boot_args phys_base + mem_size (exclusive).
  @param[out] Out           Zeroed unless J804GpuOk is returned.

  Rules beyond the carrier's own layout (G0-CARRIER-CONTRACT 7 and 8):
    - the pool is one class-10 range equal to the header's pool, 32 MiB
      aligned, at the very top of guest RAM and at most half of it (Mu lowers
      its RAM top to the pool base, so the pool must end where RAM ends);
    - every other range lies outside guest RAM; the firmware classes lie
      inside DRAM; the sgx, ASC and mailbox classes outside it;
    - classes 1..5 tile one contiguous window (the sgx window of _CRS);
    - exactly one class-6 page and one class-11 page (= the handshake IPA).
**/
STATIC
J804_GPU_STATUS
J804GpuParseHandoff (
  IN  CONST volatile UINT8  *Page,
  IN  UINT64                Ipa,
  IN  UINT64                GuestRamBase,
  IN  UINT64                GuestRamTop,
  OUT J804_GPU_HANDOFF      *Out
  )
{
  J804_GPU_HANDOFF  Parsed;
  UINT32            Total;
  UINT32            RangeOffset;
  UINT32            PerfOffset;
  UINT32            Index;
  UINT32            Other;
  UINT32            Class;
  UINT32            Mailboxes;
  UINT32            Pools;
  UINT32            Handoffs;
  UINT32            SgxCount;
  UINT64            Base;
  UINT64            Size;
  UINT64            Top;
  UINT64            SgxTop;
  UINT64            SgxBytes;
  UINT64            DramTop;
  BOOLEAN           InDram;
  BOOLEAN           Ended;
  CONST volatile UINT8  *Entry;
  CONST volatile UINT8  *OtherEntry;

  for (Index = 0; Index < sizeof (*Out); Index++) {
    ((UINT8 *)Out)[Index] = 0;
  }

  for (Index = 0; Index < sizeof (Parsed); Index++) {
    ((UINT8 *)&Parsed)[Index] = 0;
  }

  if ((Page == NULL) || (GuestRamBase >= GuestRamTop)) {
    return J804GpuBadPropertyPage;
  }

  if ((UINT32)J804GpuLe (Page + J804_GPU_HO_MAGIC, 4) != J804_GPU_HANDOFF_MAGIC) {
    return J804GpuBadMagic;
  }

  if ((J804GpuLe (Page + J804_GPU_HO_VERSION, 2) != J804_GPU_HANDOFF_VERSION) ||
      (J804GpuLe (Page + J804_GPU_HO_HEADER, 2) != J804_GPU_HANDOFF_HEADER))
  {
    return J804GpuBadVersion;
  }

  Total = (UINT32)J804GpuLe (Page + J804_GPU_HO_TOTAL, 4);
  if ((Total < J804_GPU_HANDOFF_HEADER) || (Total > J804_GPU_HANDOFF_SIZE)) {
    return J804GpuBadTotal;
  }

  if (J804GpuCrc32 (Page, Total, J804_GPU_HO_CRC) != (UINT32)J804GpuLe (Page + J804_GPU_HO_CRC, 4)) {
    return J804GpuBadCrc;
  }

  if ((J804GpuLe (Page + J804_GPU_HO_CHIP, 4) != J804_GPU_CHIP_ID) ||
      (J804GpuLe (Page + J804_GPU_HO_BOARD, 4) != J804_GPU_BOARD_ID))
  {
    return J804GpuBadIdentity;
  }

  Parsed.State = (UINT32)J804GpuLe (Page + J804_GPU_HO_STATE, 4);
  if (((Parsed.State & J804_GPU_S_ADMITTED) == 0) || ((Parsed.State & ~(UINT32)J804_GPU_S_KNOWN) != 0)) {
    return J804GpuBadState;
  }

  if (J804GpuLe (Page + J804_GPU_HO_RESERVED0, 4) != 0) {
    return J804GpuBadReserved;
  }

  for (Index = J804_GPU_HO_RESERVED1; Index < J804_GPU_HANDOFF_HEADER; Index++) {
    if (Page[Index] != 0) {
      return J804GpuBadReserved;
    }
  }

  //
  // Tables: ranges right after the header, perf states right after the
  // ranges, ending at the total.
  //
  Parsed.RangeCount = (UINT32)J804GpuLe (Page + J804_GPU_HO_RANGE_COUNT, 4);
  RangeOffset       = (UINT32)J804GpuLe (Page + J804_GPU_HO_RANGE_OFFSET, 4);
  Parsed.PerfCount  = (UINT32)J804GpuLe (Page + J804_GPU_HO_PERF_COUNT, 4);
  PerfOffset        = (UINT32)J804GpuLe (Page + J804_GPU_HO_PERF_OFFSET, 4);
  if ((RangeOffset != J804_GPU_HANDOFF_HEADER) ||
      (J804GpuLe (Page + J804_GPU_HO_RANGE_STRIDE, 4) != J804_GPU_RANGE_STRIDE) ||
      (J804GpuLe (Page + J804_GPU_HO_PERF_STRIDE, 4) != J804_GPU_PERF_STRIDE) ||
      (Parsed.RangeCount == 0) || (Parsed.RangeCount > J804_GPU_MAX_RANGES) ||
      (Parsed.PerfCount == 0) || (Parsed.PerfCount > J804_GPU_MAX_PERF) ||
      (PerfOffset != RangeOffset + Parsed.RangeCount * J804_GPU_RANGE_STRIDE) ||
      ((UINT64)PerfOffset + (UINT64)Parsed.PerfCount * J804_GPU_PERF_STRIDE != Total))
  {
    return J804GpuBadLayout;
  }

  for (Index = 0; Index < Parsed.PerfCount; Index++) {
    if (J804GpuLe (Page + PerfOffset + Index * J804_GPU_PERF_STRIDE + 12, 4) != 0) {
      return J804GpuBadReserved;
    }
  }

  Parsed.HandoffIpa  = J804GpuLe (Page + J804_GPU_HO_HANDOFF_IPA, 8);
  Parsed.HandoffSize = J804_GPU_HANDOFF_SIZE;
  Parsed.PoolBase    = J804GpuLe (Page + J804_GPU_HO_POOL_BASE, 8);
  Parsed.PoolSize    = J804GpuLe (Page + J804_GPU_HO_POOL_SIZE, 8);
  Parsed.DramBase    = J804GpuLe (Page + J804_GPU_HO_DRAM_BASE, 8);
  Parsed.DramSize    = J804GpuLe (Page + J804_GPU_HO_DRAM_SIZE, 8);
  DramTop            = Parsed.DramBase + Parsed.DramSize;
  if ((Parsed.DramSize == 0) || (DramTop < Parsed.DramBase) ||
      (GuestRamBase < Parsed.DramBase) || (GuestRamTop > DramTop))
  {
    return J804GpuBadPlacement;
  }

  //
  // Each range on its own: aligned, non-empty, a known class, zero padding,
  // and placed by class.
  //
  Mailboxes = 0;
  Pools     = 0;
  Handoffs  = 0;
  SgxCount  = 0;
  SgxBytes  = 0;
  SgxTop    = 0;
  for (Index = 0; Index < Parsed.RangeCount; Index++) {
    Entry = Page + RangeOffset + Index * J804_GPU_RANGE_STRIDE;
    Base  = J804GpuLe (Entry, 8);
    Size  = J804GpuLe (Entry + 8, 8);
    Class = (UINT32)J804GpuLe (Entry + 16, 4);
    Top   = Base + Size;
    if ((Size == 0) || (Top < Base) || (((Base | Size) & (J804_GPU_PAGE - 1)) != 0) ||
        (Class == 0) || (Class >= J804_GPU_C_COUNT))
    {
      return J804GpuBadRange;
    }

    if (J804GpuLe (Entry + 28, 4) != 0) {
      return J804GpuBadReserved;
    }

    InDram = (BOOLEAN)((Base >= Parsed.DramBase) && (Top <= DramTop));
    if (Class == J804_GPU_C_POOL) {
      if ((Base < GuestRamBase) || (Top > GuestRamTop)) {
        return J804GpuBadPlacement;
      }
    } else if (J804GpuOverlap (Base, Top, GuestRamBase, GuestRamTop)) {
      return J804GpuBadPlacement;
    }

    if (((Class == J804_GPU_C_FW_RW) || (Class == J804_GPU_C_FW_RO) || (Class == J804_GPU_C_FW_TEXT)) && !InDram) {
      return J804GpuBadPlacement;
    }

    if ((Class <= J804_GPU_C_MAILBOX) && J804GpuOverlap (Base, Top, Parsed.DramBase, DramTop)) {
      return J804GpuBadPlacement;
    }

    for (Other = 0; Other < Index; Other++) {
      OtherEntry = Page + RangeOffset + Other * J804_GPU_RANGE_STRIDE;
      if (J804GpuOverlap (
            Base,
            Top,
            J804GpuLe (OtherEntry, 8),
            J804GpuLe (OtherEntry, 8) + J804GpuLe (OtherEntry + 8, 8)
            ))
      {
        return J804GpuBadOverlap;
      }
    }

    if (Class <= J804_GPU_C_IOP_VBAR) {
      if ((SgxCount == 0) || (Base < Parsed.SgxBase)) {
        Parsed.SgxBase = Base;
      }

      if (Top > SgxTop) {
        SgxTop = Top;
      }

      SgxBytes += Size;
      SgxCount++;
    } else if (Class == J804_GPU_C_MAILBOX) {
      Parsed.MailboxBase = Base;
      Parsed.MailboxSize = Size;
      Mailboxes++;
    } else if (Class == J804_GPU_C_POOL) {
      if ((Base != Parsed.PoolBase) || (Size != Parsed.PoolSize)) {
        return J804GpuBadPool;
      }

      Pools++;
    } else if (Class == J804_GPU_C_HANDOFF) {
      if ((Base != Ipa) || (Size != J804_GPU_HANDOFF_SIZE)) {
        return J804GpuBadHandoff;
      }

      Handoffs++;
    }
  }

  //
  // Disjoint ranges whose sizes add up to the span tile it without a gap.
  //
  if ((SgxCount == 0) || (SgxTop - Parsed.SgxBase != SgxBytes)) {
    return J804GpuBadSgx;
  }

  Parsed.SgxSize = SgxBytes;
  if ((Mailboxes != 1) || (Parsed.MailboxSize != J804_GPU_PAGE)) {
    return J804GpuBadMailbox;
  }

  if ((Pools != 1) || (Parsed.PoolSize == 0) ||
      (((Parsed.PoolBase | Parsed.PoolSize) & (J804_GPU_POOL_ALIGN - 1)) != 0) ||
      (Parsed.PoolBase + Parsed.PoolSize != GuestRamTop) ||
      (Parsed.PoolSize > (GuestRamTop - GuestRamBase) / 2))
  {
    return J804GpuBadPool;
  }

  if ((Handoffs != 1) || (Parsed.HandoffIpa != Ipa)) {
    return J804GpuBadHandoff;
  }

  for (Index = 0; Index < J804_GPU_LINES; Index++) {
    Parsed.Lines[Index] = (UINT32)J804GpuLe (Page + J804_GPU_HO_LINES + 4 * Index, 4);
    if ((Parsed.Lines[Index] < J804_GPU_GSIV_MIN) || (Parsed.Lines[Index] >= J804_GPU_GSIV_LIMIT) ||
        ((Parsed.Lines[Index] >= J804_GPU_CARRIER_BLOCK_BASE) && (Parsed.Lines[Index] < J804_GPU_CARRIER_BLOCK_TOP)))
    {
      return J804GpuBadLines;
    }

    for (Other = 0; Other < Index; Other++) {
      if (Parsed.Lines[Other] == Parsed.Lines[Index]) {
        return J804GpuBadLines;
      }
    }
  }

  Parsed.MailboxNum[0] = (UINT32)J804GpuLe (Page + J804_GPU_HO_MAILBOX_NUM, 4);
  Parsed.MailboxNum[1] = (UINT32)J804GpuLe (Page + J804_GPU_HO_MAILBOX_NUM + 4, 4);
  if ((Parsed.MailboxNum[0] == 0) || (Parsed.MailboxNum[1] == 0) ||
      (Parsed.MailboxNum[0] == Parsed.MailboxNum[1]))
  {
    return J804GpuBadLines;
  }

  //
  // Receipt: printable ASCII, NUL-terminated, NUL-padded.
  //
  Ended = FALSE;
  for (Index = 0; Index < J804_GPU_RECEIPT_SIZE; Index++) {
    Parsed.Receipt[Index] = (CHAR8)Page[J804_GPU_HO_RECEIPT + Index];
    if (Parsed.Receipt[Index] == 0) {
      Ended = TRUE;
    } else if (Ended || (Parsed.Receipt[Index] < 0x20) || (Parsed.Receipt[Index] > 0x7e)) {
      return J804GpuBadReceipt;
    }
  }

  if (!Ended || (Parsed.Receipt[0] == 0)) {
    return J804GpuBadReceipt;
  }

  *Out = Parsed;
  return J804GpuOk;
}

//
// PrePi defines J804_GPU_HANDOFF_NO_SSDT: it only parses (unused static
// functions would fail the -Werror build).
//
#if !defined (J804_GPU_HANDOFF_NO_SSDT)

//
// GPU0 SSDT template (MacBookPro2026Pkg/AcpiTables/SsdtGpu0.asl): every value
// that comes from the handoff page is a unique 64-bit sentinel in the
// compiled AML, which J804GpuPatchSsdt replaces in place. Resource k (1..4:
// sgx window, mailbox page, pool, handoff page) has
//   _MIN = J804_GPU_SENTINEL_MIN (k), _LEN = J804_GPU_SENTINEL_LEN (k),
//   _MAX = _MIN + _LEN - 1 (iasl checks MinFixed/MaxFixed consistency);
// the _DSD carries the handoff IPA as J804_GPU_SENTINEL_IPA.
//
#define J804_GPU_SENTINEL_MIN(k)  (0x5AA5000000000000ULL | ((UINT64)(k) << 40))
#define J804_GPU_SENTINEL_LEN(k)  (0x0000005A00000000ULL | ((UINT64)(k) << 28))
#define J804_GPU_SENTINEL_MAX(k)  (J804_GPU_SENTINEL_MIN (k) + J804_GPU_SENTINEL_LEN (k) - 1)
#define J804_GPU_SENTINEL_IPA     0x5AA5A55A5AA5A55AULL
#define J804_GPU_SSDT_RESOURCES   4
#define J804_GPU_SSDT_PATCHES     (3 * J804_GPU_SSDT_RESOURCES + 1)

/**
  Replace Sentinel, which must occur exactly once in Aml[Start, Length), by
  Value (both 8 bytes little-endian). FALSE when it occurs zero or several
  times.
**/
STATIC
BOOLEAN
J804GpuPatchOne (
  IN OUT UINT8   *Aml,
  IN     UINTN   Length,
  IN     UINTN   Start,
  IN     UINT64  Sentinel,
  IN     UINT64  Value
  )
{
  UINTN  Index;
  UINTN  Byte;
  UINTN  Found;
  UINTN  At;

  Found = 0;
  At    = 0;
  for (Index = Start; Index + 8 <= Length; Index++) {
    for (Byte = 0; Byte < 8; Byte++) {
      if (Aml[Index + Byte] != (UINT8)(Sentinel >> (8 * Byte))) {
        break;
      }
    }

    if (Byte == 8) {
      Found++;
      At = Index;
    }
  }

  if (Found != 1) {
    return FALSE;
  }

  for (Byte = 0; Byte < 8; Byte++) {
    Aml[At + Byte] = (UINT8)(Value >> (8 * Byte));
  }

  return TRUE;
}

/**
  Patch the GPU0 SSDT template in place from a parsed handoff and fix its
  checksum. Every sentinel is counted before any byte changes. FALSE when the
  buffer is not an SSDT of the stated length or a sentinel is missing or
  repeated; the table must then not be installed.
**/
STATIC
BOOLEAN
J804GpuPatchSsdt (
  IN OUT UINT8                   *Aml,
  IN     UINTN                   Length,
  IN     CONST J804_GPU_HANDOFF  *Handoff
  )
{
  UINT64  Bases[J804_GPU_SSDT_RESOURCES];
  UINT64  Sizes[J804_GPU_SSDT_RESOURCES];
  UINT64  Sentinels[J804_GPU_SSDT_PATCHES];
  UINT64  Values[J804_GPU_SSDT_PATCHES];
  UINTN   Index;
  UINTN   Byte;
  UINTN   Found;
  UINTN   Patch;
  UINT8   Sum;

  if ((Aml == NULL) || (Length < 36) || (Aml[0] != 'S') || (Aml[1] != 'S') ||
      (Aml[2] != 'D') || (Aml[3] != 'T') ||
      ((UINT64)Aml[4] | ((UINT64)Aml[5] << 8) | ((UINT64)Aml[6] << 16) | ((UINT64)Aml[7] << 24)) != Length)
  {
    return FALSE;
  }

  Bases[0] = Handoff->SgxBase;
  Sizes[0] = Handoff->SgxSize;
  Bases[1] = Handoff->MailboxBase;
  Sizes[1] = Handoff->MailboxSize;
  Bases[2] = Handoff->PoolBase;
  Sizes[2] = Handoff->PoolSize;
  Bases[3] = Handoff->HandoffIpa;
  Sizes[3] = Handoff->HandoffSize;
  for (Index = 0; Index < J804_GPU_SSDT_RESOURCES; Index++) {
    if ((Sizes[Index] == 0) || (Bases[Index] + Sizes[Index] < Bases[Index])) {
      return FALSE;
    }

    Sentinels[3 * Index]     = J804_GPU_SENTINEL_MIN (Index + 1);
    Values[3 * Index]        = Bases[Index];
    Sentinels[3 * Index + 1] = J804_GPU_SENTINEL_MAX (Index + 1);
    Values[3 * Index + 1]    = Bases[Index] + Sizes[Index] - 1;
    Sentinels[3 * Index + 2] = J804_GPU_SENTINEL_LEN (Index + 1);
    Values[3 * Index + 2]    = Sizes[Index];
  }

  Sentinels[J804_GPU_SSDT_PATCHES - 1] = J804_GPU_SENTINEL_IPA;
  Values[J804_GPU_SSDT_PATCHES - 1]    = Handoff->HandoffIpa;

  //
  // Dry run: every sentinel exactly once, after the 36-byte header.
  //
  for (Patch = 0; Patch < J804_GPU_SSDT_PATCHES; Patch++) {
    Found = 0;
    for (Index = 36; Index + 8 <= Length; Index++) {
      for (Byte = 0; Byte < 8; Byte++) {
        if (Aml[Index + Byte] != (UINT8)(Sentinels[Patch] >> (8 * Byte))) {
          break;
        }
      }

      Found += (Byte == 8) ? 1 : 0;
    }

    if (Found != 1) {
      return FALSE;
    }
  }

  for (Patch = 0; Patch < J804_GPU_SSDT_PATCHES; Patch++) {
    if (!J804GpuPatchOne (Aml, Length, 36, Sentinels[Patch], Values[Patch])) {
      return FALSE;
    }
  }

  Aml[9] = 0;
  Sum    = 0;
  for (Index = 0; Index < Length; Index++) {
    Sum = (UINT8)(Sum + Aml[Index]);
  }

  Aml[9] = (UINT8)(0U - Sum);
  return TRUE;
}

#endif // !J804_GPU_HANDOFF_NO_SSDT

#endif

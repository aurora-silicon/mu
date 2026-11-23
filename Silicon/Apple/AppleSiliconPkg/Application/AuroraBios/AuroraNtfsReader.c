/** @file
  Read-only NTFS file reader for Aurora UEFI BIOS.

  Supports:
  - 512-byte and 4096-byte logical sectors (including Apple ANS2 4 KiB NVMe)
  - NTFS Update Sequence Array (USA) fixup restoration for FILE & INDX records
  - Fragmented $MFT (including $ATTRIBUTE_LIST extension records & up to 2048 runs)
  - Resident and non-resident attributes with multi-extent signed data runs
  - $ATTRIBUTE_LIST (0x20) extension MFT records with exact LowestVcn matching
  - Directory traversal across $INDEX_ROOT (0x90) and $INDEX_ALLOCATION (0xA0)
  - Automatic HIDF .bin discovery inside a directory

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AuroraNtfsReader.h"

#ifdef AURORA_BIOS_HOST_TEST
#include <stdlib.h>
#include <string.h>
#define NTFS_ALLOC(Sz)       malloc (Sz)
#define NTFS_FREE(Ptr)       free (Ptr)
#define NTFS_ZERO(Ptr, Sz)   memset ((Ptr), 0, (Sz))
#define NTFS_COPY(D, S, Sz)  memcpy ((D), (S), (Sz))
#define NTFS_LOG(...)        do { } while (0)
#else
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#define NTFS_ALLOC(Sz)       AllocatePool (Sz)
#define NTFS_FREE(Ptr)       FreePool (Ptr)
#define NTFS_ZERO(Ptr, Sz)   ZeroMem ((Ptr), (Sz))
#define NTFS_COPY(D, S, Sz)  CopyMem ((D), (S), (Sz))
#define NTFS_LOG(...)        DEBUG ((DEBUG_ERROR, __VA_ARGS__))
#endif

#define NTFS_ATTR_ATTRIBUTE_LIST    0x20u
#define NTFS_ATTR_DATA              0x80u
#define NTFS_ATTR_INDEX_ROOT        0x90u
#define NTFS_ATTR_INDEX_ALLOCATION  0xA0u
#define NTFS_ATTR_END               0xFFFFFFFFu

#define NTFS_MFT_RECORD_MFT         0ULL
#define NTFS_MFT_RECORD_ROOT        5ULL

#define NTFS_MAX_RUNS               2048u
#define NTFS_MAX_MFT_RECORD_SIZE    4096u
#define NTFS_MAX_INDEX_BLOCK_SIZE   16384u
#define NTFS_MAX_ATTR_LIST_SIZE     (128u * 1024u)
#define NTFS_MAX_INDEX_ALLOC_SIZE   (16u * 1024u * 1024u)

typedef struct {
  UINT64  Lcn;
  UINT64  Length;
  BOOLEAN Sparse;
} NTFS_DATA_RUN;

typedef struct {
  AURORA_NTFS_READ_FN  ReadFn;
  VOID                 *ReadCtx;
  UINT64               PartitionBytes;
  UINT32               BytesPerSector;
  UINT32               ClusterSize;
  UINT32               MftRecordSize;
  UINT32               IndexBlockSize;
  UINT64               MftStartLcn;
  NTFS_DATA_RUN        *MftRuns;
  UINT32               MftRunCount;
} NTFS_VOL;

static UINT16
RdLe16 (
  const UINT8  *P
  )
{
  return (UINT16)((UINT16)P[0] | ((UINT16)P[1] << 8));
}

static UINT32
RdLe32 (
  const UINT8  *P
  )
{
  return (UINT32)P[0] |
         ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) |
         ((UINT32)P[3] << 24);
}

static UINT64
RdLe64 (
  const UINT8  *P
  )
{
  return (UINT64)RdLe32 (P) | ((UINT64)RdLe32 (P + 4) << 32);
}

static char
AsciiUpper (
  char  C
  )
{
  if (C >= 'a' && C <= 'z') {
    return (char)(C - ('a' - 'A'));
  }
  return C;
}

static BOOLEAN
MatchUtf16AsciiInsensitive (
  const UINT8  *Utf16Le,
  UINTN        CharCount,
  const char   *AsciiName,
  UINTN        AsciiLen
  )
{
  UINTN  I;

  if (CharCount != AsciiLen) {
    return FALSE;
  }
  for (I = 0; I < AsciiLen; I++) {
    UINT16  Ch = RdLe16 (Utf16Le + I * 2u);
    if (Ch > 0x7Fu) {
      return FALSE;
    }
    if (AsciiUpper ((char)Ch) != AsciiUpper (AsciiName[I])) {
      return FALSE;
    }
  }
  return TRUE;
}

static BOOLEAN
EndsWithBinUtf16 (
  const UINT8  *Utf16Le,
  UINTN        CharCount
  )
{
  if (CharCount < 5u) {
    return FALSE;
  }
  return MatchUtf16AsciiInsensitive (Utf16Le + (CharCount - 4u) * 2u, 4u, ".bin", 4u);
}

static BOOLEAN
IsPowerOfTwo32 (
  UINT32  V
  )
{
  return (V != 0u) && ((V & (V - 1u)) == 0u);
}

static BOOLEAN
ApplyUsaFixup (
  UINT8   *Buf,
  UINT32  BufSize
  )
{
  UINT16  UsaOffset;
  UINT16  UsaCount;
  UINT16  Usn;
  UINT32  I;

  if (BufSize < 512u || (BufSize % 512u) != 0u) {
    return FALSE;
  }
  UsaOffset = RdLe16 (Buf + 4u);
  UsaCount  = RdLe16 (Buf + 6u);
  if (UsaCount < 2u) {
    return FALSE;
  }
  if ((UINT32)UsaOffset + (UINT32)UsaCount * 2u > BufSize) {
    return FALSE;
  }
  if ((UINT32)(UsaCount - 1u) * 512u > BufSize) {
    return FALSE;
  }

  Usn = RdLe16 (Buf + UsaOffset);
  for (I = 1u; I < UsaCount; I++) {
    UINT32  EndOff = I * 512u - 2u;
    if (RdLe16 (Buf + EndOff) != Usn) {
      return FALSE;
    }
    Buf[EndOff]      = Buf[UsaOffset + I * 2u];
    Buf[EndOff + 1u] = Buf[UsaOffset + I * 2u + 1u];
  }
  return TRUE;
}

static BOOLEAN
DecodeDataRunsAppend (
  const UINT8    *RunData,
  UINT32         RunDataLen,
  NTFS_DATA_RUN  *OutRuns,
  UINT32         MaxRuns,
  UINT32         *InOutCount
  )
{
  UINT32  Pos        = 0;
  UINT32  Count      = *InOutCount;
  UINT32  Added      = 0;
  INT64   CurrentLcn = 0;

  while (Pos < RunDataLen) {
    UINT8   Header = RunData[Pos++];
    UINT32  LenBytes;
    UINT32  OffBytes;
    UINT64  RunLen = 0;
    INT64   RunOff = 0;
    UINT32  I;

    if (Header == 0u) {
      break;
    }
    LenBytes = Header & 0x0Fu;
    OffBytes = (Header >> 4) & 0x0Fu;
    if (LenBytes == 0u || LenBytes > 8u || OffBytes > 8u) {
      return FALSE;
    }
    if (Pos + LenBytes + OffBytes > RunDataLen) {
      return FALSE;
    }
    if (Count >= MaxRuns) {
      return FALSE;
    }

    for (I = 0; I < LenBytes; I++) {
      RunLen |= ((UINT64)RunData[Pos + I]) << (I * 8u);
    }
    Pos += LenBytes;

    if (OffBytes == 0u) {
      OutRuns[Count].Lcn    = 0;
      OutRuns[Count].Length = RunLen;
      OutRuns[Count].Sparse = TRUE;
      Count++;
      Added++;
      continue;
    }

    for (I = 0; I < OffBytes; I++) {
      RunOff |= ((INT64)((UINT64)RunData[Pos + I] << (I * 8u)));
    }
    if ((RunData[Pos + OffBytes - 1u] & 0x80u) != 0u && OffBytes < 8u) {
      RunOff |= (INT64)(~0ULL << (OffBytes * 8u));
    }
    Pos += OffBytes;

    CurrentLcn += RunOff;
    if (CurrentLcn < 0) {
      return FALSE;
    }
    OutRuns[Count].Lcn    = (UINT64)CurrentLcn;
    OutRuns[Count].Length = RunLen;
    OutRuns[Count].Sparse = FALSE;
    Count++;
    Added++;
  }

  *InOutCount = Count;
  return (Added > 0u);
}

static BOOLEAN
ReadFromRuns (
  const NTFS_VOL       *Vol,
  const NTFS_DATA_RUN  *Runs,
  UINT32               RunCount,
  UINT64               ByteOffset,
  UINT8                *OutBuf,
  UINTN                Length
  )
{
  UINT64  CurVByte = 0;
  UINTN   Done     = 0;
  UINT32  I;

  for (I = 0; I < RunCount && Done < Length; I++) {
    UINT64  RunBytes = Runs[I].Length * (UINT64)Vol->ClusterSize;
    if (ByteOffset + (UINT64)Done >= CurVByte + RunBytes) {
      CurVByte += RunBytes;
      continue;
    }

    UINT64  InRunOff = (ByteOffset + (UINT64)Done) - CurVByte;
    UINT64  Avail    = RunBytes - InRunOff;
    UINTN   Take     = (Length - Done < Avail) ? (Length - Done) : (UINTN)Avail;

    if (Runs[I].Sparse) {
      NTFS_ZERO (OutBuf + Done, Take);
    } else {
      UINT64  PhysOff = Runs[I].Lcn * (UINT64)Vol->ClusterSize + InRunOff;
      if (Vol->PartitionBytes != 0ULL && PhysOff + (UINT64)Take > Vol->PartitionBytes) {
        return FALSE;
      }
      if (!Vol->ReadFn (Vol->ReadCtx, PhysOff, Take, OutBuf + Done)) {
        return FALSE;
      }
    }
    Done     += Take;
    CurVByte += RunBytes;
  }

  return (Done == Length);
}

static BOOLEAN
ReadMftRecord (
  const NTFS_VOL  *Vol,
  UINT64          RecordIndex,
  UINT8           *OutRecord
  )
{
  UINT64  ByteOff = RecordIndex * (UINT64)Vol->MftRecordSize;

  if (Vol->MftRuns != NULL && Vol->MftRunCount > 0u) {
    if (!ReadFromRuns (Vol, Vol->MftRuns, Vol->MftRunCount, ByteOff, OutRecord, Vol->MftRecordSize)) {
      return FALSE;
    }
  } else {
    UINT64  PhysOff = Vol->MftStartLcn * (UINT64)Vol->ClusterSize + ByteOff;
    if (!Vol->ReadFn (Vol->ReadCtx, PhysOff, Vol->MftRecordSize, OutRecord)) {
      return FALSE;
    }
  }

  if (OutRecord[0] != 'F' || OutRecord[1] != 'I' || OutRecord[2] != 'L' || OutRecord[3] != 'E') {
    return FALSE;
  }
  return ApplyUsaFixup (OutRecord, Vol->MftRecordSize);
}

// Find an attribute in a single MFT record, optionally matching a required LowestVcn (for non-resident extents).
static const UINT8 *
FindAttrInRecordVcn (
  const UINT8  *Record,
  UINT32       RecordSize,
  UINT32       AttrType,
  BOOLEAN      RequireUnnamed,
  BOOLEAN      MatchLowestVcn,
  UINT64       ExpectedLowestVcn,
  UINT32       *OutAttrLen
  )
{
  UINT32  Off;

  if (RecordSize < 48u) {
    return NULL;
  }
  Off = RdLe16 (Record + 20u);
  while (Off + 8u <= RecordSize) {
    UINT32  Type = RdLe32 (Record + Off);
    UINT32  Len;
    UINT8   NameLen;

    if (Type == NTFS_ATTR_END) {
      break;
    }
    Len = RdLe32 (Record + Off + 4u);
    if (Len < 16u || Off + Len > RecordSize) {
      break;
    }
    NameLen = Record[Off + 9u];
    if (Type == AttrType && (!RequireUnnamed || NameLen == 0u)) {
      if (!MatchLowestVcn) {
        if (OutAttrLen != NULL) {
          *OutAttrLen = Len;
        }
        return Record + Off;
      }
      if (Record[Off + 8u] != 0u && Len >= 64u && RdLe64 (Record + Off + 16u) == ExpectedLowestVcn) {
        if (OutAttrLen != NULL) {
          *OutAttrLen = Len;
        }
        return Record + Off;
      }
    }
    Off += Len;
  }
  return NULL;
}

static BOOLEAN
ReadAttributePayload (
  const NTFS_VOL  *Vol,
  const UINT8     *Attr,
  UINT32          AttrLen,
  UINTN           MaxBytes,
  UINT8           **OutBuf,
  UINTN           *OutSize
  )
{
  UINT8  NonResident;

  *OutBuf  = NULL;
  *OutSize = 0;

  if (AttrLen < 24u) {
    return FALSE;
  }
  NonResident = Attr[8u];
  if (NonResident == 0u) {
    UINT32  ValLen = RdLe32 (Attr + 16u);
    UINT16  ValOff = RdLe16 (Attr + 20u);
    UINT8   *Buf;

    if ((UINT32)ValOff + ValLen > AttrLen || ValLen > MaxBytes) {
      return FALSE;
    }
    Buf = (UINT8 *)NTFS_ALLOC (ValLen > 0u ? ValLen : 1u);
    if (Buf == NULL) {
      return FALSE;
    }
    if (ValLen > 0u) {
      NTFS_COPY (Buf, Attr + ValOff, ValLen);
    }
    *OutBuf  = Buf;
    *OutSize = ValLen;
    return TRUE;
  } else {
    UINT16         RunOff;
    UINT64         DataSize;
    NTFS_DATA_RUN  *Runs;
    UINT32         RunCount = 0;
    UINT8          *Buf;

    if (AttrLen < 64u) {
      return FALSE;
    }
    if ((RdLe16 (Attr + 12u) & 0x4001u) != 0u) {
      return FALSE;
    }
    RunOff   = RdLe16 (Attr + 32u);
    DataSize = RdLe64 (Attr + 48u);
    if (RunOff >= AttrLen || DataSize > (UINT64)MaxBytes) {
      return FALSE;
    }

    Runs = (NTFS_DATA_RUN *)NTFS_ALLOC (sizeof (NTFS_DATA_RUN) * NTFS_MAX_RUNS);
    if (Runs == NULL) {
      return FALSE;
    }
    if (!DecodeDataRunsAppend (Attr + RunOff, AttrLen - RunOff, Runs, NTFS_MAX_RUNS, &RunCount)) {
      NTFS_FREE (Runs);
      return FALSE;
    }

    Buf = (UINT8 *)NTFS_ALLOC (DataSize > 0ULL ? (UINTN)DataSize : 1u);
    if (Buf == NULL) {
      NTFS_FREE (Runs);
      return FALSE;
    }
    if (DataSize > 0ULL &&
        !ReadFromRuns (Vol, Runs, RunCount, 0ULL, Buf, (UINTN)DataSize)) {
      NTFS_FREE (Buf);
      NTFS_FREE (Runs);
      return FALSE;
    }

    NTFS_FREE (Runs);
    *OutBuf  = Buf;
    *OutSize = (UINTN)DataSize;
    return TRUE;
  }
}

static BOOLEAN
ReadMftAttributeFull (
  const NTFS_VOL  *Vol,
  const UINT8     *BaseRecord,
  UINT32          AttrType,
  BOOLEAN         RequireUnnamed,
  UINTN           MaxBytes,
  UINT8           **OutBuf,
  UINTN           *OutSize
  )
{
  UINT32       AttrLen       = 0;
  const UINT8  *Attr;
  const UINT8  *AttrListAttr;
  UINT32       AttrListLen   = 0;
  UINT8        *AttrListBuf  = NULL;
  UINTN        AttrListSize  = 0;
  UINT8        *ExtRec       = NULL;
  UINT8        *ResultBuf    = NULL;
  UINTN        TotalSize     = 0;
  BOOLEAN      HaveTotalSize = FALSE;
  UINTN        Pos;

  *OutBuf  = NULL;
  *OutSize = 0;

  AttrListAttr = FindAttrInRecordVcn (BaseRecord, Vol->MftRecordSize, NTFS_ATTR_ATTRIBUTE_LIST, FALSE, FALSE, 0ULL, &AttrListLen);
  if (AttrListAttr == NULL) {
    Attr = FindAttrInRecordVcn (BaseRecord, Vol->MftRecordSize, AttrType, RequireUnnamed, FALSE, 0ULL, &AttrLen);
    if (Attr == NULL) {
      return FALSE;
    }
    return ReadAttributePayload (Vol, Attr, AttrLen, MaxBytes, OutBuf, OutSize);
  }

  if (!ReadAttributePayload (Vol, AttrListAttr, AttrListLen, NTFS_MAX_ATTR_LIST_SIZE, &AttrListBuf, &AttrListSize)) {
    return FALSE;
  }
  ExtRec = (UINT8 *)NTFS_ALLOC (Vol->MftRecordSize);
  if (ExtRec == NULL) {
    NTFS_FREE (AttrListBuf);
    return FALSE;
  }

  // Pass 1: locate base extent (StartVcn == 0)
  Pos = 0;
  while (Pos + 26u <= AttrListSize) {
    UINT32  EntryType = RdLe32 (AttrListBuf + Pos);
    UINT16  EntryLen  = RdLe16 (AttrListBuf + Pos + 4u);
    UINT8   NameLen   = AttrListBuf[Pos + 6u];
    UINT64  StartVcn  = RdLe64 (AttrListBuf + Pos + 8u);
    UINT64  MftRef    = RdLe64 (AttrListBuf + Pos + 16u) & 0x0000FFFFFFFFFFFFULL;

    if (EntryLen < 26u || Pos + EntryLen > AttrListSize) {
      break;
    }
    if (EntryType == AttrType && (!RequireUnnamed || NameLen == 0u) && StartVcn == 0ULL) {
      if (ReadMftRecord (Vol, MftRef, ExtRec)) {
        Attr = FindAttrInRecordVcn (ExtRec, Vol->MftRecordSize, AttrType, RequireUnnamed, FALSE, 0ULL, &AttrLen);
        if (Attr != NULL) {
          if (Attr[8u] == 0u) {
            BOOLEAN  Ok = ReadAttributePayload (Vol, Attr, AttrLen, MaxBytes, OutBuf, OutSize);
            NTFS_FREE (ExtRec);
            NTFS_FREE (AttrListBuf);
            return Ok;
          } else if (AttrLen >= 64u) {
            UINT64  DataSize = RdLe64 (Attr + 48u);
            if (DataSize <= (UINT64)MaxBytes) {
              TotalSize     = (UINTN)DataSize;
              HaveTotalSize = TRUE;
            }
          }
        }
      }
      break;
    }
    Pos += EntryLen;
  }

  if (!HaveTotalSize) {
    NTFS_FREE (ExtRec);
    NTFS_FREE (AttrListBuf);
    return FALSE;
  }

  ResultBuf = (UINT8 *)NTFS_ALLOC (TotalSize > 0u ? TotalSize : 1u);
  if (ResultBuf == NULL) {
    NTFS_FREE (ExtRec);
    NTFS_FREE (AttrListBuf);
    return FALSE;
  }
  NTFS_ZERO (ResultBuf, TotalSize);

  // Pass 2: read every extent matched by exact LowestVcn == StartVcn
  Pos = 0;
  while (Pos + 26u <= AttrListSize) {
    UINT32  EntryType = RdLe32 (AttrListBuf + Pos);
    UINT16  EntryLen  = RdLe16 (AttrListBuf + Pos + 4u);
    UINT8   NameLen   = AttrListBuf[Pos + 6u];
    UINT64  StartVcn  = RdLe64 (AttrListBuf + Pos + 8u);
    UINT64  MftRef    = RdLe64 (AttrListBuf + Pos + 16u) & 0x0000FFFFFFFFFFFFULL;

    if (EntryLen < 26u || Pos + EntryLen > AttrListSize) {
      break;
    }
    if (EntryType == AttrType && (!RequireUnnamed || NameLen == 0u)) {
      if (!ReadMftRecord (Vol, MftRef, ExtRec)) {
        NTFS_FREE (ResultBuf);
        NTFS_FREE (ExtRec);
        NTFS_FREE (AttrListBuf);
        return FALSE;
      }
      Attr = FindAttrInRecordVcn (ExtRec, Vol->MftRecordSize, AttrType, RequireUnnamed, TRUE, StartVcn, &AttrLen);
      if (Attr != NULL && Attr[8u] != 0u && AttrLen >= 64u) {
        UINT64         LowestVcn  = RdLe64 (Attr + 16u);
        UINT64         HighestVcn = RdLe64 (Attr + 24u);
        UINT16         RunOff     = RdLe16 (Attr + 32u);
        NTFS_DATA_RUN  *Runs;
        UINT32         RunCount   = 0;

        if (HighestVcn >= LowestVcn && RunOff < AttrLen) {
          UINT64  DstOff   = LowestVcn * (UINT64)Vol->ClusterSize;
          UINT64  ExtBytes = (HighestVcn - LowestVcn + 1ULL) * (UINT64)Vol->ClusterSize;

          if (DstOff < (UINT64)TotalSize) {
            UINTN  CopyLen = ((UINT64)TotalSize - DstOff < ExtBytes)
                             ? (UINTN)((UINT64)TotalSize - DstOff)
                             : (UINTN)ExtBytes;
            Runs = (NTFS_DATA_RUN *)NTFS_ALLOC (sizeof (NTFS_DATA_RUN) * NTFS_MAX_RUNS);
            if (Runs == NULL) {
              NTFS_FREE (ResultBuf);
              NTFS_FREE (ExtRec);
              NTFS_FREE (AttrListBuf);
              return FALSE;
            }
            if (!DecodeDataRunsAppend (Attr + RunOff, AttrLen - RunOff, Runs, NTFS_MAX_RUNS, &RunCount) ||
                !ReadFromRuns (Vol, Runs, RunCount, 0ULL, ResultBuf + DstOff, CopyLen)) {
              NTFS_FREE (Runs);
              NTFS_FREE (ResultBuf);
              NTFS_FREE (ExtRec);
              NTFS_FREE (AttrListBuf);
              return FALSE;
            }
            NTFS_FREE (Runs);
          }
        }
      }
    }
    Pos += EntryLen;
  }

  NTFS_FREE (ExtRec);
  NTFS_FREE (AttrListBuf);
  *OutBuf  = ResultBuf;
  *OutSize = TotalSize;
  return TRUE;
}

static BOOLEAN
ScanIndexEntriesForName (
  const UINT8  *Entries,
  UINT32       EntriesBytes,
  const char   *TargetName,
  UINTN        TargetLen,
  UINT64       *OutMftIndex
  )
{
  UINT32  Pos = 0;

  while (Pos + 16u <= EntriesBytes) {
    UINT64  MftRef    = RdLe64 (Entries + Pos) & 0x0000FFFFFFFFFFFFULL;
    UINT16  EntryLen  = RdLe16 (Entries + Pos + 8u);
    UINT16  StreamLen = RdLe16 (Entries + Pos + 10u);
    UINT16  Flags     = RdLe16 (Entries + Pos + 12u);

    if (EntryLen < 16u || Pos + EntryLen > EntriesBytes) {
      break;
    }
    if ((Flags & 0x0002u) != 0u) {
      break;
    }
    if (StreamLen >= 66u && Pos + 16u + StreamLen <= EntriesBytes) {
      const UINT8  *Fn     = Entries + Pos + 16u;
      UINT8        FnChars = Fn[64u];
      if (66u + (UINT32)FnChars * 2u <= StreamLen) {
        if (MatchUtf16AsciiInsensitive (Fn + 66u, FnChars, TargetName, TargetLen)) {
          *OutMftIndex = MftRef;
          return TRUE;
        }
      }
    }
    Pos += EntryLen;
  }
  return FALSE;
}

static BOOLEAN
LookupInDirectory (
  const NTFS_VOL  *Vol,
  UINT64          DirMftIdx,
  const char      *Name,
  UINTN           NameLen,
  UINT64          *OutChildMftIdx
  )
{
  UINT8    *DirRec     = NULL;
  UINT8    *IdxRootBuf = NULL;
  UINTN    IdxRootSize = 0;
  UINT8    *IdxAlloc   = NULL;
  UINTN    IdxAllocSz  = 0;
  BOOLEAN  Found       = FALSE;

  DirRec = (UINT8 *)NTFS_ALLOC (Vol->MftRecordSize);
  if (DirRec == NULL) {
    return FALSE;
  }
  if (!ReadMftRecord (Vol, DirMftIdx, DirRec)) {
    NTFS_FREE (DirRec);
    return FALSE;
  }

  if (ReadMftAttributeFull (Vol, DirRec, NTFS_ATTR_INDEX_ROOT, FALSE, 64u * 1024u, &IdxRootBuf, &IdxRootSize)) {
    if (IdxRootSize >= 32u) {
      UINT32  EntriesOff = 16u + RdLe32 (IdxRootBuf + 16u);
      UINT32  TotalLen   = 16u + RdLe32 (IdxRootBuf + 20u);
      if (EntriesOff < IdxRootSize && TotalLen <= IdxRootSize && TotalLen > EntriesOff) {
        Found = ScanIndexEntriesForName (
                  IdxRootBuf + EntriesOff,
                  TotalLen - EntriesOff,
                  Name,
                  NameLen,
                  OutChildMftIdx
                  );
      }
    }
    NTFS_FREE (IdxRootBuf);
  }

  if (!Found &&
      ReadMftAttributeFull (Vol, DirRec, NTFS_ATTR_INDEX_ALLOCATION, FALSE, NTFS_MAX_INDEX_ALLOC_SIZE, &IdxAlloc, &IdxAllocSz)) {
    UINTN  Off;
    for (Off = 0; Off + Vol->IndexBlockSize <= IdxAllocSz; Off += Vol->IndexBlockSize) {
      UINT8  *Blk = IdxAlloc + Off;
      if (Blk[0] != 'I' || Blk[1] != 'N' || Blk[2] != 'D' || Blk[3] != 'X') {
        continue;
      }
      if (!ApplyUsaFixup (Blk, Vol->IndexBlockSize)) {
        continue;
      }
      {
        UINT32  EntriesOff = 24u + RdLe32 (Blk + 24u);
        UINT32  TotalLen   = 24u + RdLe32 (Blk + 28u);
        if (EntriesOff < Vol->IndexBlockSize && TotalLen <= Vol->IndexBlockSize && TotalLen > EntriesOff) {
          if (ScanIndexEntriesForName (Blk + EntriesOff, TotalLen - EntriesOff, Name, NameLen, OutChildMftIdx)) {
            Found = TRUE;
            break;
          }
        }
      }
    }
    NTFS_FREE (IdxAlloc);
  }

  NTFS_FREE (DirRec);
  return Found;
}

static VOID
UnmountNtfsVolume (
  NTFS_VOL  *Vol
  )
{
  if (Vol->MftRuns != NULL) {
    NTFS_FREE (Vol->MftRuns);
    Vol->MftRuns     = NULL;
    Vol->MftRunCount = 0;
  }
}

static BOOLEAN
MountNtfsVolume (
  AURORA_NTFS_READ_FN  ReadFn,
  VOID                 *Context,
  UINT64               PartitionSizeBytes,
  NTFS_VOL             *Vol
  )
{
  UINT8        Boot[512];
  UINT32       Bps;
  UINT8        Spc;
  INT8         RecVal;
  INT8         IdxVal;
  UINT8        *Mft0        = NULL;
  const UINT8  *DataAttr;
  UINT32       DataAttrLen  = 0;
  const UINT8  *AttrList;
  UINT32       AttrListLen  = 0;

  NTFS_ZERO (Vol, sizeof (*Vol));
  Vol->ReadFn         = ReadFn;
  Vol->ReadCtx        = Context;
  Vol->PartitionBytes = PartitionSizeBytes;

  if (!ReadFn (Context, 0ULL, sizeof (Boot), Boot)) {
    return FALSE;
  }
  if (Boot[3] != 'N' || Boot[4] != 'T' || Boot[5] != 'F' || Boot[6] != 'S' ||
      Boot[7] != ' ' || Boot[8] != ' ' || Boot[9] != ' ' || Boot[10] != ' ') {
    return FALSE;
  }

  Bps = RdLe16 (Boot + 0x0Bu);
  Spc = Boot[0x0Du];
  if (!IsPowerOfTwo32 (Bps) || Bps < 512u || Bps > 4096u || Spc == 0u || !IsPowerOfTwo32 (Spc)) {
    return FALSE;
  }

  Vol->BytesPerSector = Bps;
  Vol->ClusterSize    = Bps * (UINT32)Spc;
  if (Vol->ClusterSize == 0u || Vol->ClusterSize > 65536u) {
    return FALSE;
  }

  Vol->MftStartLcn = RdLe64 (Boot + 0x30u);
  RecVal = (INT8)Boot[0x40u];
  IdxVal = (INT8)Boot[0x44u];

  Vol->MftRecordSize = (RecVal < 0)
                       ? (1u << (UINT32)(-RecVal))
                       : ((UINT32)RecVal * Vol->ClusterSize);
  Vol->IndexBlockSize = (IdxVal < 0)
                        ? (1u << (UINT32)(-IdxVal))
                        : ((UINT32)IdxVal * Vol->ClusterSize);

  if (Vol->MftRecordSize < 512u || Vol->MftRecordSize > NTFS_MAX_MFT_RECORD_SIZE ||
      (Vol->MftRecordSize % 512u) != 0u) {
    return FALSE;
  }
  if (Vol->IndexBlockSize < 512u || Vol->IndexBlockSize > NTFS_MAX_INDEX_BLOCK_SIZE ||
      (Vol->IndexBlockSize % 512u) != 0u) {
    return FALSE;
  }

  Vol->MftRuns = (NTFS_DATA_RUN *)NTFS_ALLOC (sizeof (NTFS_DATA_RUN) * NTFS_MAX_RUNS);
  if (Vol->MftRuns == NULL) {
    return FALSE;
  }

  Mft0 = (UINT8 *)NTFS_ALLOC (Vol->MftRecordSize);
  if (Mft0 == NULL) {
    UnmountNtfsVolume (Vol);
    return FALSE;
  }
  if (!ReadMftRecord (Vol, NTFS_MFT_RECORD_MFT, Mft0)) {
    NTFS_FREE (Mft0);
    UnmountNtfsVolume (Vol);
    return FALSE;
  }

  // Decode $MFT $DATA runs (either directly in Record 0, or via $ATTRIBUTE_LIST)
  AttrList = FindAttrInRecordVcn (Mft0, Vol->MftRecordSize, NTFS_ATTR_ATTRIBUTE_LIST, FALSE, FALSE, 0ULL, &AttrListLen);
  if (AttrList == NULL) {
    DataAttr = FindAttrInRecordVcn (Mft0, Vol->MftRecordSize, NTFS_ATTR_DATA, TRUE, FALSE, 0ULL, &DataAttrLen);
    if (DataAttr != NULL && DataAttr[8u] != 0u && DataAttrLen >= 64u) {
      UINT16  RunOff = RdLe16 (DataAttr + 32u);
      if (RunOff < DataAttrLen) {
        DecodeDataRunsAppend (DataAttr + RunOff, DataAttrLen - RunOff, Vol->MftRuns, NTFS_MAX_RUNS, &Vol->MftRunCount);
      }
    }
  } else {
    UINT8  *AlBuf  = NULL;
    UINTN  AlSize  = 0;
    UINT8  *ExtRec = (UINT8 *)NTFS_ALLOC (Vol->MftRecordSize);
    if (ExtRec != NULL &&
        ReadAttributePayload (Vol, AttrList, AttrListLen, NTFS_MAX_ATTR_LIST_SIZE, &AlBuf, &AlSize)) {
      UINTN  Pos = 0;
      while (Pos + 26u <= AlSize) {
        UINT32  EType  = RdLe32 (AlBuf + Pos);
        UINT16  ELen   = RdLe16 (AlBuf + Pos + 4u);
        UINT8   EName  = AlBuf[Pos + 6u];
        UINT64  SVcn   = RdLe64 (AlBuf + Pos + 8u);
        UINT64  MftRef = RdLe64 (AlBuf + Pos + 16u) & 0x0000FFFFFFFFFFFFULL;
        if (ELen < 26u || Pos + ELen > AlSize) break;
        if (EType == NTFS_ATTR_DATA && EName == 0u) {
          if (ReadMftRecord (Vol, MftRef, ExtRec)) {
            DataAttr = FindAttrInRecordVcn (ExtRec, Vol->MftRecordSize, NTFS_ATTR_DATA, TRUE, TRUE, SVcn, &DataAttrLen);
            if (DataAttr != NULL && DataAttrLen >= 64u) {
              UINT16  RunOff = RdLe16 (DataAttr + 32u);
              if (RunOff < DataAttrLen) {
                DecodeDataRunsAppend (DataAttr + RunOff, DataAttrLen - RunOff, Vol->MftRuns, NTFS_MAX_RUNS, &Vol->MftRunCount);
              }
            }
          }
        }
        Pos += ELen;
      }
      NTFS_FREE (AlBuf);
    }
    if (ExtRec != NULL) {
      NTFS_FREE (ExtRec);
    }
  }

  NTFS_FREE (Mft0);
  NTFS_LOG (
    "[AURORA NTFS] Mounted: bps=%u clus=%u mftRec=%u idxBlk=%u mftRuns=%u\r\n",
    Vol->BytesPerSector,
    Vol->ClusterSize,
    Vol->MftRecordSize,
    Vol->IndexBlockSize,
    Vol->MftRunCount
    );
  return TRUE;
}

static BOOLEAN
WalkPathToMft (
  const NTFS_VOL  *Vol,
  const char      *Path,
  UINT64          *OutMftIdx
  )
{
  UINT64      CurrentMft = NTFS_MFT_RECORD_ROOT;
  const char  *P         = Path;

  while (*P != '\0') {
    const char  *Start;
    UINTN       Len     = 0;
    UINT64      NextMft = 0;

    while (*P == '\\' || *P == '/') {
      P++;
    }
    if (*P == '\0') {
      break;
    }
    Start = P;
    while (*P != '\0' && *P != '\\' && *P != '/') {
      P++;
      Len++;
    }
    if (!LookupInDirectory (Vol, CurrentMft, Start, Len, &NextMft)) {
      NTFS_LOG ("[AURORA NTFS] Path segment '%.*a' not found in MFT %Lu\r\n", (INT32)Len, Start, CurrentMft);
      return FALSE;
    }
    NTFS_LOG ("[AURORA NTFS] '%.*a' -> MFT %Lu\r\n", (INT32)Len, Start, NextMft);
    CurrentMft = NextMft;
  }

  *OutMftIdx = CurrentMft;
  return TRUE;
}

static BOOLEAN
ReadFileByMft (
  const NTFS_VOL  *Vol,
  UINT64          FileMftIdx,
  UINT8           *OutBuffer,
  UINTN           MaxLength,
  UINTN           *OutLength
  )
{
  UINT8  *FileRec = (UINT8 *)NTFS_ALLOC (Vol->MftRecordSize);
  UINT8  *DataBuf = NULL;
  UINTN  DataSize = 0;

  *OutLength = 0;
  if (FileRec == NULL) {
    return FALSE;
  }
  if (!ReadMftRecord (Vol, FileMftIdx, FileRec)) {
    NTFS_FREE (FileRec);
    return FALSE;
  }
  if (!ReadMftAttributeFull (Vol, FileRec, NTFS_ATTR_DATA, TRUE, MaxLength, &DataBuf, &DataSize)) {
    NTFS_FREE (FileRec);
    return FALSE;
  }
  NTFS_FREE (FileRec);

  if (DataSize > MaxLength) {
    NTFS_FREE (DataBuf);
    return FALSE;
  }
  if (DataSize > 0u) {
    NTFS_COPY (OutBuffer, DataBuf, DataSize);
  }
  NTFS_FREE (DataBuf);
  *OutLength = DataSize;
  return TRUE;
}

BOOLEAN
AuroraNtfsReadFile (
  AURORA_NTFS_READ_FN  ReadFn,
  VOID                 *Context,
  UINT64               PartitionSizeBytes,
  const char           *Path,
  UINT8                *OutBuffer,
  UINTN                MaxLength,
  UINTN                *OutLength
  )
{
  NTFS_VOL  Vol;
  UINT64    FileMft = 0;
  BOOLEAN   Ok;

  if (ReadFn == NULL || Path == NULL || OutBuffer == NULL || OutLength == NULL || MaxLength == 0u) {
    return FALSE;
  }
  *OutLength = 0;

  if (!MountNtfsVolume (ReadFn, Context, PartitionSizeBytes, &Vol)) {
    return FALSE;
  }
  Ok = WalkPathToMft (&Vol, Path, &FileMft) &&
       ReadFileByMft (&Vol, FileMft, OutBuffer, MaxLength, OutLength);
  UnmountNtfsVolume (&Vol);
  return Ok;
}

// Helper: scan index entries for any *.bin file whose $DATA begins with MagicLe32.
static BOOLEAN
ScanEntriesForMagicBin (
  const NTFS_VOL  *Vol,
  const UINT8     *Entries,
  UINT32          EntriesBytes,
  UINT32          MagicLe32,
  UINT8           *OutBuffer,
  UINTN           MaxLength,
  UINTN           *OutLength,
  char            *MatchedNameOut,
  UINTN           MatchedNameCap
  )
{
  UINT32  Pos = 0;

  while (Pos + 16u <= EntriesBytes) {
    UINT64  MftRef    = RdLe64 (Entries + Pos) & 0x0000FFFFFFFFFFFFULL;
    UINT16  EntryLen  = RdLe16 (Entries + Pos + 8u);
    UINT16  StreamLen = RdLe16 (Entries + Pos + 10u);
    UINT16  Flags     = RdLe16 (Entries + Pos + 12u);

    if (EntryLen < 16u || Pos + EntryLen > EntriesBytes) {
      break;
    }
    if ((Flags & 0x0002u) != 0u) {
      break;
    }
    if (StreamLen >= 66u && Pos + 16u + StreamLen <= EntriesBytes) {
      const UINT8  *Fn     = Entries + Pos + 16u;
      UINT8        FnChars = Fn[64u];
      if (66u + (UINT32)FnChars * 2u <= StreamLen && EndsWithBinUtf16 (Fn + 66u, FnChars)) {
        UINTN  Len = 0;
        if (ReadFileByMft (Vol, MftRef, OutBuffer, MaxLength, &Len) &&
            Len >= 20u && RdLe32 (OutBuffer) == MagicLe32) {
          *OutLength = Len;
          if (MatchedNameOut != NULL && MatchedNameCap > 0u) {
            UINTN  I;
            for (I = 0; I < FnChars && I + 1u < MatchedNameCap; I++) {
              UINT16  Ch = RdLe16 (Fn + 66u + I * 2u);
              MatchedNameOut[I] = (Ch >= 0x20u && Ch <= 0x7Eu) ? (char)Ch : '?';
            }
            MatchedNameOut[I] = '\0';
          }
          return TRUE;
        }
      }
    }
    Pos += EntryLen;
  }
  return FALSE;
}

BOOLEAN
AuroraNtfsFindMagicBinInDir (
  AURORA_NTFS_READ_FN  ReadFn,
  VOID                 *Context,
  UINT64               PartitionSizeBytes,
  const char           *DirPath,
  UINT32               MagicLe32,
  UINT8                *OutBuffer,
  UINTN                MaxLength,
  UINTN                *OutLength,
  char                 *MatchedNameOut,
  UINTN                MatchedNameCap
  )
{
  NTFS_VOL  Vol;
  UINT64    DirMft     = 0;
  UINT8     *DirRec    = NULL;
  UINT8     *IdxRoot   = NULL;
  UINTN     IdxRootSz  = 0;
  UINT8     *IdxAlloc  = NULL;
  UINTN     IdxAllocSz = 0;
  BOOLEAN   Found      = FALSE;

  if (ReadFn == NULL || DirPath == NULL || OutBuffer == NULL || OutLength == NULL || MaxLength == 0u) {
    return FALSE;
  }
  *OutLength = 0;
  if (MatchedNameOut != NULL && MatchedNameCap > 0u) {
    MatchedNameOut[0] = '\0';
  }

  if (!MountNtfsVolume (ReadFn, Context, PartitionSizeBytes, &Vol)) {
    return FALSE;
  }
  if (!WalkPathToMft (&Vol, DirPath, &DirMft)) {
    UnmountNtfsVolume (&Vol);
    return FALSE;
  }

  DirRec = (UINT8 *)NTFS_ALLOC (Vol.MftRecordSize);
  if (DirRec != NULL && ReadMftRecord (&Vol, DirMft, DirRec)) {
    if (ReadMftAttributeFull (&Vol, DirRec, NTFS_ATTR_INDEX_ROOT, FALSE, 64u * 1024u, &IdxRoot, &IdxRootSz)) {
      if (IdxRootSz >= 32u) {
        UINT32  EntriesOff = 16u + RdLe32 (IdxRoot + 16u);
        UINT32  TotalLen   = 16u + RdLe32 (IdxRoot + 20u);
        if (EntriesOff < IdxRootSz && TotalLen <= IdxRootSz && TotalLen > EntriesOff) {
          Found = ScanEntriesForMagicBin (
                    &Vol, IdxRoot + EntriesOff, TotalLen - EntriesOff,
                    MagicLe32, OutBuffer, MaxLength, OutLength, MatchedNameOut, MatchedNameCap
                    );
        }
      }
      NTFS_FREE (IdxRoot);
    }

    if (!Found &&
        ReadMftAttributeFull (&Vol, DirRec, NTFS_ATTR_INDEX_ALLOCATION, FALSE, NTFS_MAX_INDEX_ALLOC_SIZE, &IdxAlloc, &IdxAllocSz)) {
      UINTN  Off;
      for (Off = 0; Off + Vol.IndexBlockSize <= IdxAllocSz; Off += Vol.IndexBlockSize) {
        UINT8  *Blk = IdxAlloc + Off;
        if (Blk[0] != 'I' || Blk[1] != 'N' || Blk[2] != 'D' || Blk[3] != 'X') {
          continue;
        }
        if (!ApplyUsaFixup (Blk, Vol.IndexBlockSize)) {
          continue;
        }
        {
          UINT32  EntriesOff = 24u + RdLe32 (Blk + 24u);
          UINT32  TotalLen   = 24u + RdLe32 (Blk + 28u);
          if (EntriesOff < Vol.IndexBlockSize && TotalLen <= Vol.IndexBlockSize && TotalLen > EntriesOff) {
            if (ScanEntriesForMagicBin (
                  &Vol, Blk + EntriesOff, TotalLen - EntriesOff,
                  MagicLe32, OutBuffer, MaxLength, OutLength, MatchedNameOut, MatchedNameCap
                  )) {
              Found = TRUE;
              break;
            }
          }
        }
      }
      NTFS_FREE (IdxAlloc);
    }
  }

  if (DirRec != NULL) {
    NTFS_FREE (DirRec);
  }
  UnmountNtfsVolume (&Vol);
  return Found;
}

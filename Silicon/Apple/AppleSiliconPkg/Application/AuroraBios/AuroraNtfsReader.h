/** @file
  Read-only NTFS file reader for Aurora UEFI BIOS.

  Allows AuroraBios to load board-specific trackpad firmware directly from the
  user's existing Windows installation (\Windows\System32\drivers\tpmtfw.bin
  or any HIDF *.bin in \Windows\System32\drivers\) via EFI_BLOCK_IO_PROTOCOL
  without bundling or duplicating Apple firmware.

  Dual-compiled for AArch64 UEFI and host unit tests (AURORA_BIOS_HOST_TEST).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef AURORA_NTFS_READER_H_
#define AURORA_NTFS_READER_H_

#include "AuroraBiosConfig.h"

typedef BOOLEAN (*AURORA_NTFS_READ_FN) (
  VOID    *Context,
  UINT64  ByteOffset,
  UINTN   Length,
  VOID    *OutBuffer
  );

// Read a specific file by backslash-separated ASCII path from an NTFS partition.
BOOLEAN
AuroraNtfsReadFile (
  AURORA_NTFS_READ_FN  ReadFn,
  VOID                 *Context,
  UINT64               PartitionSizeBytes,
  const char           *Path,
  UINT8                *OutBuffer,
  UINTN                MaxLength,
  UINTN                *OutLength
  );

// Scan a directory (e.g. "Windows\\System32\\drivers") for any .bin file whose
// first 4 bytes equal MagicLe32 (e.g. 0x46444948 = 'HIDF'), returning its full
// contents and matched filename.
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
  );

#endif  // AURORA_NTFS_READER_H_

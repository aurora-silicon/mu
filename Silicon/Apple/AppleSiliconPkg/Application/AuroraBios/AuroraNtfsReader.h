/** @file
  Read-only NTFS file reader for Aurora UEFI BIOS.

  Allows AuroraBios to load board-specific trackpad firmware directly from the
  user's existing Windows installation (\Windows\System32\drivers\tpmtfw.bin)
  via EFI_BLOCK_IO_PROTOCOL without bundling or duplicating Apple firmware.

  Dual-compiled for AArch64 UEFI and host unit tests (AURORA_BIOS_HOST_TEST).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef AURORA_NTFS_READER_H_
#define AURORA_NTFS_READER_H_

#include "AuroraBiosConfig.h"

// Abstract byte-level read callback backed by EFI_BLOCK_IO_PROTOCOL (UEFI)
// or a memory/file buffer (host tests). Returns TRUE on success.
typedef BOOLEAN (*AURORA_NTFS_READ_FN) (
  VOID    *Context,
  UINT64  ByteOffset,
  UINTN   Length,
  VOID    *OutBuffer
  );

// Read a file by backslash-separated ASCII path (e.g. "Windows\\System32\\drivers\\tpmtfw.bin")
// from an NTFS partition accessible through ReadFn.
//
// OutBuffer must have capacity MaxLength bytes.
// On success, sets *OutLength to the exact file size and returns TRUE.
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

#endif  // AURORA_NTFS_READER_H_

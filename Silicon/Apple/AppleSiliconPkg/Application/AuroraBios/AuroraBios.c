/** @file
  Aurora Silicon UEFI Setup - UEFI Application Entry Point.

  Provides a mouse, trackpad, and keyboard interactive UEFI Setup for Apple M5
  (J813 / T8142) with zero-lag dirty-rect double-buffered GOP rendering,
  native Apple MTP DockChannel keyboard + multitouch trackpad input, live
  FPWM0 keyboard backlight control, interactive UEFI Boot Manager, and
  dual-backed persistent configuration (UEFI NVRAM + NVMe ESP).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Guid/GlobalVariable.h>
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/UsbIo.h>

#include "AuroraBiosConfig.h"
#include "AuroraMtpInput.h"

STATIC EFI_GUID  mAuroraBiosConfigGuid = AURORA_BIOS_CONFIG_GUID_INIT;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *Gop;
  UINT32                             *SceneBuf;     // Logical Width * Height (0x00RRGGBB, no cursor)
  UINT32                             *FbBase;       // Direct GOP linear framebuffer pointer
  UINTN                              FbStride;      // Physical PixelsPerScanLine
  BOOLEAN                            FbIs30Bit;     // TRUE for Apple 10-bit X2R10G10B10 panel
  UINT32                             LogRowBuf[2048];
  UINT32                             PhysRowBuf[4096];

  // Independent zero-lag cursor tracking
  BOOLEAN                            CurDrawn;
  INT32                              DrawnCurX;
  INT32                              DrawnCurY;

  EFI_SIMPLE_POINTER_PROTOCOL        **SimplePtrs;
  UINTN                              SimplePtrCount;
  EFI_ABSOLUTE_POINTER_PROTOCOL      **AbsPtrs;
  UINTN                              AbsPtrCount;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  **TextInEx;
  UINTN                              TextInExCount;

  EFI_BOOT_MANAGER_LOAD_OPTION       *BootOptions;
  UINTN                              BootOptionCount;

  AURORA_MTP_CONTEXT                 Mtp;
  AURORA_KBL_CONTEXT                 Kbl;
  AURORA_BIOS_STATE                  State;
} AURORA_BIOS_HOST;

STATIC
UINT32
BiosNowMs (
  VOID
  )
{
  return (UINT32)(GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000000ULL);
}

// ---------------------------------------------------------------------------
// ASCII Conversion Helper
// ---------------------------------------------------------------------------
STATIC
VOID
UnicodeToAsciiSafe (
  OUT char          *Dst,
  IN  UINTN         DstSize,
  IN  CONST CHAR16  *Src
  )
{
  UINTN  I = 0;

  if (Dst == NULL || DstSize == 0) {
    return;
  }
  if (Src != NULL) {
    while (I + 1 < DstSize && Src[I] != L'\0') {
      CHAR16  C = Src[I];
      Dst[I] = (C >= 0x20 && C <= 0x7E) ? (char)C : '?';
      I++;
    }
  }
  Dst[I] = '\0';
}

// ---------------------------------------------------------------------------
// ESP (\EFI\Aurora\bios.cfg) + UEFI Runtime Variable Persistence
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
LoadConfigFromEsp (
  OUT AURORA_BIOS_CONFIG  *Cfg,
  OUT BOOLEAN             *EspWritable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  *EspWritable = FALSE;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status) || Count == 0) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (I = 0; I < Count; I++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs   = NULL;
    EFI_FILE_PROTOCOL                *Root = NULL;
    EFI_FILE_PROTOCOL                *File = NULL;
    AURORA_BIOS_CONFIG               Tmp;
    UINTN                            ReadSize;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) || Fs == NULL) {
      continue;
    }
    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || Root == NULL) {
      continue;
    }

    {
      EFI_FILE_PROTOCOL  *EfiDir = NULL;
      if (!EFI_ERROR (Root->Open (Root, &EfiDir, L"\\EFI", EFI_FILE_MODE_READ, 0)) && EfiDir != NULL) {
        *EspWritable = TRUE;
        EfiDir->Close (EfiDir);
      }
    }

    if (!EFI_ERROR (Root->Open (Root, &File, AURORA_BIOS_CONFIG_ESP_PATH, EFI_FILE_MODE_READ, 0)) && File != NULL) {
      ReadSize = sizeof (Tmp);
      ZeroMem (&Tmp, sizeof (Tmp));
      if (!EFI_ERROR (File->Read (File, &ReadSize, &Tmp)) &&
          ReadSize == sizeof (Tmp) &&
          AuroraBiosConfigValidate (&Tmp)) {
        CopyMem (Cfg, &Tmp, sizeof (Tmp));
        Status = EFI_SUCCESS;
      }
      File->Close (File);
    }

    Root->Close (Root);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  FreePool (Handles);
  return Status;
}

STATIC
EFI_STATUS
SaveConfigToEsp (
  IN CONST AURORA_BIOS_CONFIG  *Cfg
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status) || Count == 0) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (I = 0; I < Count; I++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs     = NULL;
    EFI_FILE_PROTOCOL                *Root   = NULL;
    EFI_FILE_PROTOCOL                *EfiDir = NULL;
    EFI_FILE_PROTOCOL                *AurDir = NULL;
    EFI_FILE_PROTOCOL                *File   = NULL;
    UINTN                            WriteSize;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) || Fs == NULL) {
      continue;
    }
    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || Root == NULL) {
      continue;
    }
    if (EFI_ERROR (Root->Open (Root, &EfiDir, L"\\EFI", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) || EfiDir == NULL) {
      Root->Close (Root);
      continue;
    }
    EfiDir->Close (EfiDir);

    if (!EFI_ERROR (Root->Open (
                            Root,
                            &AurDir,
                            L"\\EFI\\Aurora",
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                            EFI_FILE_DIRECTORY
                            )) && AurDir != NULL) {
      AurDir->Close (AurDir);
    }

    Status = Root->Open (
                     Root,
                     &File,
                     AURORA_BIOS_CONFIG_ESP_PATH,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                     0
                     );
    if (!EFI_ERROR (Status) && File != NULL) {
      WriteSize = sizeof (*Cfg);
      Status = File->Write (File, &WriteSize, (VOID *)Cfg);
      if (!EFI_ERROR (Status)) {
        File->Flush (File);
      }
      File->Close (File);
    }
    Root->Close (Root);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  FreePool (Handles);
  return Status;
}

STATIC
VOID
LoadBiosConfig (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  AURORA_BIOS_CONFIG  Loaded;
  UINTN               VarSize     = sizeof (Loaded);
  EFI_STATUS          Status;
  BOOLEAN             EspWritable = FALSE;

  AuroraBiosConfigSetDefaults (&Host->State.Config);

  Status = LoadConfigFromEsp (&Loaded, &EspWritable);
  Host->State.Info.EspWritable = EspWritable;
  if (!EFI_ERROR (Status)) {
    CopyMem (&Host->State.Config, &Loaded, sizeof (Loaded));
    Host->State.Info.EspConfigFound = TRUE;
  } else {
    Status = gRT->GetVariable (
                    AURORA_BIOS_CONFIG_VAR_NAME,
                    &mAuroraBiosConfigGuid,
                    NULL,
                    &VarSize,
                    &Loaded
                    );
    if (!EFI_ERROR (Status) && VarSize == sizeof (Loaded) && AuroraBiosConfigValidate (&Loaded)) {
      CopyMem (&Host->State.Config, &Loaded, sizeof (Loaded));
    }
  }

  CopyMem (&Host->State.SavedConfig, &Host->State.Config, sizeof (AURORA_BIOS_CONFIG));
}

STATIC
VOID
CommitBootOrderVariable (
  IN AURORA_BIOS_HOST  *Host
  )
{
  UINT16  Order[AURORA_BIOS_MAX_BOOT_ENTRIES];
  UINTN   Count = Host->State.BootEntryCount;
  UINTN   I;

  if (Count == 0 || !Host->State.BootOrderDirty) {
    return;
  }
  if (Count > AURORA_BIOS_MAX_BOOT_ENTRIES) {
    Count = AURORA_BIOS_MAX_BOOT_ENTRIES;
  }
  for (I = 0; I < Count; I++) {
    Order[I] = Host->State.BootEntries[I].OptionNumber;
  }
  gRT->SetVariable (
         EFI_BOOT_ORDER_VARIABLE_NAME,
         &gEfiGlobalVariableGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
         Count * sizeof (UINT16),
         Order
         );
  Host->State.BootOrderDirty = FALSE;
}

STATIC
VOID
SaveBiosConfig (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_STATUS  EspSt;

  Host->State.Config.EnterBiosOnce = 0;
  AuroraBiosConfigFinalize (&Host->State.Config);

  gRT->SetVariable (
         AURORA_BIOS_CONFIG_VAR_NAME,
         &mAuroraBiosConfigGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
         sizeof (AURORA_BIOS_CONFIG),
         &Host->State.Config
         );

  EspSt = SaveConfigToEsp (&Host->State.Config);
  if (!EFI_ERROR (EspSt)) {
    Host->State.Info.EspConfigFound = TRUE;
    Host->State.Info.EspWritable    = TRUE;
  }
  CommitBootOrderVariable (Host);
  CopyMem (&Host->State.SavedConfig, &Host->State.Config, sizeof (AURORA_BIOS_CONFIG));
}

// ---------------------------------------------------------------------------
// Hardware & Input Protocol Discovery
// ---------------------------------------------------------------------------
STATIC
VOID
FreeInputProtocols (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  if (Host->SimplePtrs != NULL) { FreePool (Host->SimplePtrs); Host->SimplePtrs = NULL; }
  if (Host->AbsPtrs != NULL)    { FreePool (Host->AbsPtrs);    Host->AbsPtrs    = NULL; }
  if (Host->TextInEx != NULL)   { FreePool (Host->TextInEx);   Host->TextInEx   = NULL; }
  Host->SimplePtrCount = 0;
  Host->AbsPtrCount    = 0;
  Host->TextInExCount  = 0;
}

STATIC
VOID
DiscoverInputProtocols (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  FreeInputProtocols (Host);

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimplePointerProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    Host->SimplePtrs = AllocateZeroPool (Count * sizeof (EFI_SIMPLE_POINTER_PROTOCOL *));
    if (Host->SimplePtrs != NULL) {
      for (I = 0; I < Count; I++) {
        EFI_SIMPLE_POINTER_PROTOCOL  *Ptr = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimplePointerProtocolGuid, (VOID **)&Ptr)) && Ptr != NULL) {
          Ptr->Reset (Ptr, FALSE);
          Host->SimplePtrs[Host->SimplePtrCount++] = Ptr;
        }
      }
    }
    FreePool (Handles);
    Handles = NULL;
  }

  Count = 0;
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    Host->AbsPtrs = AllocateZeroPool (Count * sizeof (EFI_ABSOLUTE_POINTER_PROTOCOL *));
    if (Host->AbsPtrs != NULL) {
      for (I = 0; I < Count; I++) {
        EFI_ABSOLUTE_POINTER_PROTOCOL  *Ptr = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Ptr)) && Ptr != NULL) {
          Ptr->Reset (Ptr, FALSE);
          Host->AbsPtrs[Host->AbsPtrCount++] = Ptr;
        }
      }
    }
    FreePool (Handles);
    Handles = NULL;
  }

  Count = 0;
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleTextInputExProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    Host->TextInEx = AllocateZeroPool (Count * sizeof (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *));
    if (Host->TextInEx != NULL) {
      for (I = 0; I < Count; I++) {
        EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *In = NULL;
        if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleTextInputExProtocolGuid, (VOID **)&In)) && In != NULL) {
          Host->TextInEx[Host->TextInExCount++] = In;
        }
      }
    }
    FreePool (Handles);
  }

  Host->State.Info.SimplePointerCount = (UINT32)Host->SimplePtrCount;
  Host->State.Info.AbsPointerCount    = (UINT32)Host->AbsPtrCount;
  Host->State.Info.KeyboardCount      = (UINT32)(Host->TextInExCount > 0 ? Host->TextInExCount : 1);
}

STATIC
VOID
ConnectStoragePartitions (
  VOID
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  // Connect raw NVMe/disk BlockIo handles so DiskIoDxe -> PartitionDxe -> Fat.efi
  // publish all GPT logical partitions (FAT ESP + Windows NTFS C:) before we
  // read \EFI\Aurora\bios.cfg or \Windows\System32\drivers\tpmtfw.bin.
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    for (I = 0; I < Count; I++) {
      gBS->ConnectController (Handles[I], NULL, NULL, TRUE);
    }
    FreePool (Handles);
  }
}

STATIC
VOID
ScanUsbHidOnDemand (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    for (I = 0; I < Count; I++) {
      gBS->ConnectController (Handles[I], NULL, NULL, TRUE);
    }
    FreePool (Handles);
  }

  DiscoverInputProtocols (Host);
}

STATIC
VOID
PopulateTelemetryAndBootOptions (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;
  UINT64      MaxBytes = 0;

  Host->State.Info.CpuCores      = PcdGet32 (PcdCoreCount);
  Host->State.Info.TotalRamMiB   = PcdGet64 (PcdSystemMemorySize) / (1024ULL * 1024ULL);
  Host->State.Info.GpuPoolActive = PcdGetBool (PcdAppleJ813GpuLocalPool);

  // Populate firmware version string cleanly (avoid bare "1.0")
  {
    CONST CHAR16  *FwVer = (CONST CHAR16 *)PcdGetPtr (PcdFirmwareVersionString);
    if (FwVer != NULL && FwVer[0] != L'\0' && StrLen (FwVer) >= 6) {
      UnicodeToAsciiSafe (Host->State.Info.FirmwareVersion, sizeof (Host->State.Info.FirmwareVersion), FwVer);
    } else {
      AsciiStrCpyS (Host->State.Info.FirmwareVersion, sizeof (Host->State.Info.FirmwareVersion), "Aurora Mu 2026");
    }
  }

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles)) && Count > 0) {
    Host->State.Info.StorageDeviceCount = (UINT32)Count;
    for (I = 0; I < Count; I++) {
      EFI_BLOCK_IO_PROTOCOL  *Blk = NULL;
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Blk)) &&
          Blk != NULL && Blk->Media != NULL && Blk->Media->MediaPresent && !Blk->Media->LogicalPartition) {
        UINT64  Bytes = ((UINT64)Blk->Media->LastBlock + 1ULL) * (UINT64)Blk->Media->BlockSize;
        if (Bytes > MaxBytes) {
          MaxBytes = Bytes;
        }
      }
    }
    FreePool (Handles);
  }
  Host->State.Info.PrimaryDiskGiB = MaxBytes / (1024ULL * 1024ULL * 1024ULL);

  EfiBootManagerRefreshAllBootOption ();
  Host->BootOptions = EfiBootManagerGetLoadOptions (&Host->BootOptionCount, LoadOptionTypeBoot);
  Host->State.BootEntryCount = 0;
  for (I = 0; I < Host->BootOptionCount && Host->State.BootEntryCount < AURORA_BIOS_MAX_BOOT_ENTRIES; I++) {
    AURORA_BOOT_ENTRY  *Dst = &Host->State.BootEntries[Host->State.BootEntryCount];
    CHAR16             *DpText;

    ZeroMem (Dst, sizeof (*Dst));
    Dst->OptionNumber = (UINT16)Host->BootOptions[I].OptionNumber;
    Dst->IsActive     = ((Host->BootOptions[I].Attributes & LOAD_OPTION_ACTIVE) != 0);
    UnicodeToAsciiSafe (
      Dst->Description,
      sizeof (Dst->Description),
      Host->BootOptions[I].Description != NULL ? Host->BootOptions[I].Description : L"UEFI Boot Entry"
      );

    DpText = ConvertDevicePathToText (Host->BootOptions[I].FilePath, TRUE, TRUE);
    if (DpText != NULL) {
      UnicodeToAsciiSafe (Dst->DeviceHint, sizeof (Dst->DeviceHint), DpText);
      FreePool (DpText);
    } else {
      AsciiStrCpyS (Dst->DeviceHint, sizeof (Dst->DeviceHint), "Internal UEFI Device");
    }

    Host->State.BootEntryCount++;
  }
}

STATIC
VOID
UpdateClock (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_TIME  Now;
  if (!EFI_ERROR (gRT->GetTime (&Now, NULL))) {
    Host->State.Info.ClockHour = Now.Hour;
    Host->State.Info.ClockMin  = Now.Minute;
    Host->State.Info.ClockSec  = Now.Second;
  }
}

// ---------------------------------------------------------------------------
// Direct Write-Combining Framebuffer Blitter + Independent Zero-Lag Cursor
// ---------------------------------------------------------------------------
#define CURSOR_W  18
#define CURSOR_H  20

STATIC
VOID
BlitLogicalRectDirect (
  IN OUT AURORA_BIOS_HOST  *Host,
  IN     INT32             X1,
  IN     INT32             Y1,
  IN     INT32             X2,
  IN     INT32             Y2,
  IN     BOOLEAN           IncludeCursor
  )
{
  AURORA_BIOS_STATE  *S     = &Host->State;
  INT32              Scale  = S->UiScale;
  INT32              Lw, Lh, Ly, Lx, Sy;
  UINTN              RowBytes;
  BOOLEAN            CurOverlap;

  if (Host->FbBase == NULL || Host->SceneBuf == NULL) {
    return;
  }
  if (X1 < 0) X1 = 0;
  if (Y1 < 0) Y1 = 0;
  if (X2 > S->Width)  X2 = S->Width;
  if (Y2 > S->Height) Y2 = S->Height;
  Lw = X2 - X1;
  Lh = Y2 - Y1;
  if (Lw <= 0 || Lh <= 0 || Lw > 2048 || (Lw * Scale) > 4096) {
    return;
  }

  CurOverlap = IncludeCursor && S->MouseVisible &&
               (S->MouseX < X2) && (S->MouseX + CURSOR_W > X1) &&
               (S->MouseY < Y2) && (S->MouseY + CURSOR_H > Y1);
  RowBytes   = (UINTN)(Lw * Scale) * sizeof (UINT32);

  for (Ly = 0; Ly < Lh; Ly++) {
    INT32  AbsY = Y1 + Ly;

    CopyMem (
      Host->LogRowBuf,
      Host->SceneBuf + (UINTN)AbsY * (UINTN)S->Width + (UINTN)X1,
      (UINTN)Lw * sizeof (UINT32)
      );

    // Stamp cursor scanline directly onto L1-cached row if overlapping
    if (CurOverlap && AbsY >= S->MouseY && AbsY < S->MouseY + 18) {
      static const char  *Sprite[18] = {
        "X...............",
        "XX..............",
        "XWX.............",
        "XWWX............",
        "XWWWX...........",
        "XWWWWX..........",
        "XWWWWWX.........",
        "XWWWWWWX........",
        "XWWWWWWWX.......",
        "XWWWWWWWWX......",
        "XWWWWWWWWWX.....",
        "XWWWWWWXXXXX....",
        "XWWXOWWX........",
        "XWX.XWWX........",
        "XX...XWWX.......",
        "X....XWWX.......",
        "......XX........",
        "................"
      };
      const char  *SprRow = Sprite[AbsY - S->MouseY];
      INT32       Sx;
      for (Sx = 0; Sx < 16; Sx++) {
        INT32  Px = S->MouseX + Sx - X1;
        char   C  = SprRow[Sx];
        if (C == '.' || Px < 0 || Px >= Lw) continue;
        Host->LogRowBuf[Px] = (C == 'X') ? 0x00000000u : ((C == 'O') ? 0x00FF8533u : 0x00FFFFFFu);
      }
    }

    // Encode row into L1-cached PhysRowBuf (exact pixel format + 1x/2x scale)
    if (Host->FbIs30Bit) {
      if (Scale == 2) {
        UINT32  *Dst = Host->PhysRowBuf;
        for (Lx = 0; Lx < Lw; Lx++) {
          UINT32  C = Host->LogRowBuf[Lx];
          UINT32  R = ((C >> 14) & 0x3FCu) | ((C >> 22) & 0x3u);
          UINT32  G = ((C >> 6)  & 0x3FCu) | ((C >> 14) & 0x3u);
          UINT32  B = ((C << 2)  & 0x3FCu) | ((C >> 6)  & 0x3u);
          UINT32  Raw = (R << 20) | (G << 10) | B;
          Dst[0] = Raw;
          Dst[1] = Raw;
          Dst += 2;
        }
      } else {
        for (Lx = 0; Lx < Lw; Lx++) {
          UINT32  C = Host->LogRowBuf[Lx];
          UINT32  R = ((C >> 14) & 0x3FCu) | ((C >> 22) & 0x3u);
          UINT32  G = ((C >> 6)  & 0x3FCu) | ((C >> 14) & 0x3u);
          UINT32  B = ((C << 2)  & 0x3FCu) | ((C >> 6)  & 0x3u);
          Host->PhysRowBuf[Lx] = (R << 20) | (G << 10) | B;
        }
      }
    } else {
      // 32-bit BGRA (0x00RRGGBB in little-endian matches SceneBuf identically!)
      if (Scale == 2) {
        UINT32  *Dst = Host->PhysRowBuf;
        for (Lx = 0; Lx < Lw; Lx++) {
          UINT32  Raw = Host->LogRowBuf[Lx];
          Dst[0] = Raw;
          Dst[1] = Raw;
          Dst += 2;
        }
      } else {
        CopyMem (Host->PhysRowBuf, Host->LogRowBuf, (UINTN)Lw * sizeof (UINT32));
      }
    }

    // Burst write directly to Normal-NC VRAM via ARM64 write-combining CopyMem
    for (Sy = 0; Sy < Scale; Sy++) {
      UINTN   PhysY = (UINTN)(AbsY * Scale + Sy + S->TopInsetPx);
      UINT32  *FbRow = Host->FbBase + PhysY * Host->FbStride + (UINTN)(X1 * Scale);
      CopyMem (FbRow, Host->PhysRowBuf, RowBytes);
    }
  }
}

STATIC
VOID
PresentDirtyRect (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  AURORA_BIOS_STATE  *S = &Host->State;

  if (Host->FbBase == NULL || Host->SceneBuf == NULL) {
    return;
  }

  // Case 1: Full redraw (mode/tab switch, modal open/close, initial frame)
  if (S->FullRedrawNeeded) {
    AuroraBiosRender (S, Host->SceneBuf);
    S->FullRedrawNeeded = FALSE;
    S->SceneDirty       = FALSE;
    S->Dirty.Valid      = FALSE;
    BlitLogicalRectDirect (Host, 0, 0, S->Width, S->Height, TRUE);
    Host->CurDrawn  = S->MouseVisible;
    Host->DrawnCurX = S->MouseX;
    Host->DrawnCurY = S->MouseY;
    return;
  }

  // Case 2: Partial scene update (hover highlight change, small control update)
  if (S->SceneDirty || S->Dirty.Valid) {
    INT32  Dx1 = S->Dirty.MinX;
    INT32  Dy1 = S->Dirty.MinY;
    INT32  Dx2 = S->Dirty.MaxX;
    INT32  Dy2 = S->Dirty.MaxY;
    BOOLEAN  HaveDirty = S->Dirty.Valid;

    if (S->SceneDirty) {
      AuroraBiosRender (S, Host->SceneBuf);
      S->SceneDirty = FALSE;
    }
    S->Dirty.Valid = FALSE;

    // Erase previous cursor cleanly if it was outside Dirty
    if (Host->CurDrawn &&
        (Host->DrawnCurX != S->MouseX || Host->DrawnCurY != S->MouseY)) {
      BlitLogicalRectDirect (
        Host,
        Host->DrawnCurX,
        Host->DrawnCurY,
        Host->DrawnCurX + CURSOR_W,
        Host->DrawnCurY + CURSOR_H,
        FALSE
        );
    }

    if (HaveDirty) {
      BlitLogicalRectDirect (Host, Dx1, Dy1, Dx2, Dy2, TRUE);
    }

    // Stamp new cursor (in case cursor sits outside Dirty)
    if (S->MouseVisible) {
      BlitLogicalRectDirect (
        Host,
        S->MouseX,
        S->MouseY,
        S->MouseX + CURSOR_W,
        S->MouseY + CURSOR_H,
        TRUE
        );
      Host->CurDrawn  = TRUE;
      Host->DrawnCurX = S->MouseX;
      Host->DrawnCurY = S->MouseY;
    }
    return;
  }

  // Case 3: Pure cursor movement
  // - If old & new overlap (smooth glide): single atomic union blit (zero flicker, ~15-22 KiB!)
  // - If disjoint (fast flick): two tiny 18x20 patches (11.5 KiB total, never a huge bbox!)
  if (S->MouseVisible &&
      (!Host->CurDrawn || Host->DrawnCurX != S->MouseX || Host->DrawnCurY != S->MouseY)) {
    if (Host->CurDrawn &&
        (Host->DrawnCurX < S->MouseX + CURSOR_W) && (S->MouseX < Host->DrawnCurX + CURSOR_W) &&
        (Host->DrawnCurY < S->MouseY + CURSOR_H) && (S->MouseY < Host->DrawnCurY + CURSOR_H)) {
      INT32  Ux1 = (Host->DrawnCurX < S->MouseX) ? Host->DrawnCurX : S->MouseX;
      INT32  Uy1 = (Host->DrawnCurY < S->MouseY) ? Host->DrawnCurY : S->MouseY;
      INT32  Ux2 = ((Host->DrawnCurX > S->MouseX) ? Host->DrawnCurX : S->MouseX) + CURSOR_W;
      INT32  Uy2 = ((Host->DrawnCurY > S->MouseY) ? Host->DrawnCurY : S->MouseY) + CURSOR_H;
      BlitLogicalRectDirect (Host, Ux1, Uy1, Ux2, Uy2, TRUE);
    } else {
      if (Host->CurDrawn) {
        BlitLogicalRectDirect (
          Host,
          Host->DrawnCurX,
          Host->DrawnCurY,
          Host->DrawnCurX + CURSOR_W,
          Host->DrawnCurY + CURSOR_H,
          FALSE
          );
      }
      BlitLogicalRectDirect (
        Host,
        S->MouseX,
        S->MouseY,
        S->MouseX + CURSOR_W,
        S->MouseY + CURSOR_H,
        TRUE
        );
    }
    Host->CurDrawn  = TRUE;
    Host->DrawnCurX = S->MouseX;
    Host->DrawnCurY = S->MouseY;
  }
}

// ---------------------------------------------------------------------------
// Input Polling (Native Apple MTP DockChannel + UEFI ConIn / Pointer)
// ---------------------------------------------------------------------------
STATIC
AURORA_BIOS_KEY
TranslateInputKey (
  IN CONST EFI_INPUT_KEY  *Key,
  IN UINT32               KeyShiftState
  )
{
  switch (Key->ScanCode) {
    case SCAN_UP:    return AURORA_KEY_UP;
    case SCAN_DOWN:  return AURORA_KEY_DOWN;
    case SCAN_LEFT:  return AURORA_KEY_LEFT;
    case SCAN_RIGHT: return AURORA_KEY_RIGHT;
    case SCAN_ESC:   return AURORA_KEY_ESC;
    case SCAN_F1:    return AURORA_KEY_F1;
    case SCAN_F2:    return AURORA_KEY_F2;
    case SCAN_F3:    return AURORA_KEY_F3;
    case SCAN_F4:    return AURORA_KEY_F4;
    case SCAN_F5:    return AURORA_KEY_F5;
    case SCAN_F7:    return AURORA_KEY_F7;
    case SCAN_F9:    return AURORA_KEY_F9;
    case SCAN_F10:   return AURORA_KEY_F10;
    case SCAN_F11:   return AURORA_KEY_F11;
    default: break;
  }

  switch (Key->UnicodeChar) {
    case CHAR_CARRIAGE_RETURN:
    case CHAR_LINEFEED:
      return AURORA_KEY_ENTER;
    case CHAR_TAB:
      if ((KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0) {
        return AURORA_KEY_SHIFT_TAB;
      }
      return AURORA_KEY_TAB;
    case L' ':  return AURORA_KEY_SPACE;
    case L'1':  return AURORA_KEY_F1;
    case L'2':  return AURORA_KEY_F2;
    case L'3':  return AURORA_KEY_F3;
    case L'4':  return AURORA_KEY_F4;
    case L'5':  return AURORA_KEY_F5;
    case L'+':
    case L'=':  return AURORA_KEY_PLUS;
    case L'-':
    case L'_':  return AURORA_KEY_MINUS;
    case L'q':
    case L'Q':  return AURORA_KEY_ESC;
    default:    return AURORA_KEY_NONE;
  }
}

STATIC
VOID
HandleRawKey (
  IN OUT AURORA_BIOS_HOST  *Host,
  IN CONST EFI_INPUT_KEY   *Key,
  IN UINT32                KeyShiftState
  )
{
  AURORA_BIOS_KEY  K = TranslateInputKey (Key, KeyShiftState);
  if (K != AURORA_KEY_NONE) {
    AuroraBiosHandleKey (&Host->State, K);
  }
}

STATIC
VOID
PollInputs (
  IN OUT AURORA_BIOS_HOST  *Host,
  IN     UINT32            NowMs
  )
{
  UINTN  I;

  // 1. Poll Native Apple MTP DockChannel (Built-in MacBook Air Keyboard + Trackpad!)
  AuroraMtpHwPoll (&Host->Mtp, NowMs, &Host->State);

  // 2. Poll Extended Keyboard Protocols (USB / Serial Terminal / ConIn)
  if (Host->TextInExCount > 0) {
    for (I = 0; I < Host->TextInExCount; I++) {
      EFI_KEY_DATA  KeyData;
      while (!EFI_ERROR (Host->TextInEx[I]->ReadKeyStrokeEx (Host->TextInEx[I], &KeyData))) {
        HandleRawKey (Host, &KeyData.Key, KeyData.KeyState.KeyShiftState);
      }
    }
  } else if (gST->ConIn != NULL) {
    EFI_INPUT_KEY  Key;
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      HandleRawKey (Host, &Key, 0);
    }
  }

  // 3. Poll Relative USB Mice (EFI_SIMPLE_POINTER_PROTOCOL)
  for (I = 0; I < Host->SimplePtrCount; I++) {
    EFI_SIMPLE_POINTER_STATE  St;
    if (!EFI_ERROR (Host->SimplePtrs[I]->GetState (Host->SimplePtrs[I], &St))) {
      INT32  Dx = St.RelativeMovementX;
      INT32  Dy = St.RelativeMovementY;
      if (Host->SimplePtrs[I]->Mode != NULL) {
        if (Host->SimplePtrs[I]->Mode->ResolutionX > 8) {
          Dx = (INT32)((INT64)Dx * 4 / (INT64)Host->SimplePtrs[I]->Mode->ResolutionX);
        }
        if (Host->SimplePtrs[I]->Mode->ResolutionY > 8) {
          Dy = (INT32)((INT64)Dy * 4 / (INT64)Host->SimplePtrs[I]->Mode->ResolutionY);
        }
      }
      if (Dx != 0 || Dy != 0 || St.LeftButton != Host->State.MouseLeftDown) {
        AuroraBiosHandleMouseDelta (&Host->State, Dx, Dy, St.LeftButton);
      }
    }
  }

  // 4. Poll Absolute Pointing Devices (EFI_ABSOLUTE_POINTER_PROTOCOL)
  for (I = 0; I < Host->AbsPtrCount; I++) {
    EFI_ABSOLUTE_POINTER_STATE  St;
    EFI_ABSOLUTE_POINTER_MODE   *Mode = Host->AbsPtrs[I]->Mode;
    if (!EFI_ERROR (Host->AbsPtrs[I]->GetState (Host->AbsPtrs[I], &St)) && Mode != NULL) {
      UINT64   RangeX = (Mode->AbsoluteMaxX > Mode->AbsoluteMinX) ? (Mode->AbsoluteMaxX - Mode->AbsoluteMinX) : 1;
      UINT64   RangeY = (Mode->AbsoluteMaxY > Mode->AbsoluteMinY) ? (Mode->AbsoluteMaxY - Mode->AbsoluteMinY) : 1;
      INT32    X      = (INT32)(((St.CurrentX - Mode->AbsoluteMinX) * (UINT64)Host->State.Width) / RangeX);
      INT32    Y      = (INT32)(((St.CurrentY - Mode->AbsoluteMinY) * (UINT64)Host->State.Height) / RangeY);
      BOOLEAN  Down   = ((St.ActiveButtons & EFI_ABSP_TouchActive) != 0);
      AuroraBiosHandleMouseMove (&Host->State, X, Y, Down);
    }
  }
}

// ---------------------------------------------------------------------------
// Screen Clear & UEFI Shell Launcher
// ---------------------------------------------------------------------------
STATIC
VOID
ClearGopScreen (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;
  if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) {
    return;
  }
  ZeroMem (&Black, sizeof (Black));
  Gop->Blt (
         Gop,
         &Black,
         EfiBltVideoFill,
         0,
         0,
         0,
         0,
         Gop->Mode->Info->HorizontalResolution,
         Gop->Mode->Info->VerticalResolution,
         0
         );
}

STATIC
VOID
LaunchInternalShell (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                        Status;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage = NULL;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_DEVICE_PATH_PROTOCOL          *ShellPath   = NULL;
  EFI_HANDLE                        ShellHandle  = NULL;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR (Status) || LoadedImage == NULL) {
    return;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, PcdGetPtr (PcdShellFile));
  ShellPath = AppendDevicePathNode (DevicePathFromHandle (LoadedImage->DeviceHandle), (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  if (ShellPath == NULL) {
    return;
  }

  Status = gBS->LoadImage (FALSE, ImageHandle, ShellPath, NULL, 0, &ShellHandle);
  FreePool (ShellPath);
  if (!EFI_ERROR (Status) && ShellHandle != NULL) {
    gBS->StartImage (ShellHandle, NULL, NULL);
  }
}

// ---------------------------------------------------------------------------
// Main Application Entry Point (Non-Blocking 125 Hz Live Loop)
// ---------------------------------------------------------------------------
EFI_STATUS
EFIAPI
AuroraBiosEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  AURORA_BIOS_HOST  *Host;
  EFI_STATUS        Status;
  BOOLEAN           Running     = TRUE;
  UINT32            StartMs;
  UINT32            LastSecTick = 0;

  Host = AllocateZeroPool (sizeof (AURORA_BIOS_HOST));
  if (Host == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Host->Gop);
  if (EFI_ERROR (Status) || Host->Gop == NULL || Host->Gop->Mode == NULL || Host->Gop->Mode->Info == NULL) {
    Print (L"Aurora UEFI Setup: Graphics Output Protocol not found (%r)\r\n", Status);
    FreePool (Host);
    return Status;
  }

  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  AuroraBiosStateInit (
    &Host->State,
    (INT32)Host->Gop->Mode->Info->HorizontalResolution,
    (INT32)Host->Gop->Mode->Info->VerticalResolution
    );
  Host->FbBase    = (UINT32 *)(UINTN)Host->Gop->Mode->FrameBufferBase;
  Host->FbStride  = Host->Gop->Mode->Info->PixelsPerScanLine;
  Host->FbIs30Bit = (Host->Gop->Mode->Info->PixelFormat == PixelBitMask);
  Host->State.Info.GopBpp = Host->FbIs30Bit ? 30u : 32u;

  Host->SceneBuf = AllocateZeroPool ((UINTN)Host->State.Width * (UINTN)Host->State.Height * sizeof (UINT32));
  if (Host->SceneBuf == NULL) {
    FreePool (Host);
    return EFI_OUT_OF_RESOURCES;
  }

  ConnectStoragePartitions ();
  LoadBiosConfig (Host);
  PopulateTelemetryAndBootOptions (Host);
  DiscoverInputProtocols (Host);

  // Light up built-in MacBook Air keyboard LEDs via SoC FPWM0 immediately!
  Host->State.Info.KblActive = AuroraKblHwInit (
                                 &Host->Kbl,
                                 AuroraBiosKblLevelToPwm (Host->State.Config.KbdBacklightLevel)
                                 );

  // Bring up built-in MacBook Air Keyboard + MultiTouch Trackpad over MTP DockChannel!
  (VOID)AuroraMtpHwInit (&Host->Mtp);
  Host->State.Info.MtpActive    = Host->Mtp.Available;
  Host->State.Info.MtpKbReady   = Host->Mtp.Keyboard.Ready;
  Host->State.Info.MtpTpReady   = (Host->Mtp.MultiTouch.Ready || Host->Mtp.MultiTouch.Enabled);
  Host->State.Info.MtpFwLoaded  = Host->Mtp.FwLoaded;
  Host->State.Info.MtpPacketsRx = Host->Mtp.PacketsRx;
  AsciiStrCpyS (Host->State.Info.MtpFwSource, sizeof (Host->State.Info.MtpFwSource), Host->Mtp.FwSource);

  UpdateClock (Host);
  AuroraBiosBuildLayout (&Host->State);
  Host->State.FullRedrawNeeded = TRUE;

  DEBUG ((
    DEBUG_ERROR,
    "[AURORA SETUP] LIVE at %ux%u (%ux HiDPI, %u bpp) | BootEntries=%u | MTP: KB=%u TP=%u FW=%a | KBL=%u\r\n",
    Host->State.Info.GopWidth,
    Host->State.Info.GopHeight,
    (UINT32)Host->State.UiScale,
    Host->State.Info.GopBpp,
    Host->State.BootEntryCount,
    Host->State.Info.MtpKbReady ? 1u : 0u,
    Host->State.Info.MtpTpReady ? 1u : 0u,
    Host->State.Info.MtpFwSource,
    Host->State.Info.KblActive ? 1u : 0u
    ));

  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
  // Clear full physical screen (including top 64px notch strip) to pure obsidian black
  ClearGopScreen (Host->Gop);

  StartMs     = BiosNowMs ();
  LastSecTick = 0;

  while (Running) {
    UINT32  NowMs     = BiosNowMs ();
    UINT32  ElapsedS  = (NowMs - StartMs) / 1000u;

    PresentDirtyRect (Host);
    PollInputs (Host, NowMs);
    Host->State.TickCount++;

    // Surgical 1-second monotonic tick: ONLY updates clock, uptime, and RX counter
    // (< 25 KiB direct VRAM burst instead of re-rendering/blitting 17 MiB!).
    if (ElapsedS != LastSecTick) {
      AURORA_DIRTY_RECT  DynRects[3];
      UINT32             DynCount;
      UINT32             R;

      LastSecTick = ElapsedS;
      Host->State.Info.UptimeSec = ElapsedS;
      UpdateClock (Host);
      DynCount = AuroraBiosRenderDynamicOnly (&Host->State, Host->SceneBuf, DynRects);
      for (R = 0; R < DynCount; R++) {
        BlitLogicalRectDirect (
          Host,
          DynRects[R].MinX,
          DynRects[R].MinY,
          DynRects[R].MaxX,
          DynRects[R].MaxY,
          TRUE
          );
      }
    }

    switch (Host->State.PendingAction) {
      case AURORA_ACTION_NONE:
        break;

      case AURORA_ACTION_SET_KBD_BACKLIGHT:
        AuroraKblHwSetBrightness (&Host->Kbl, Host->State.PendingActionArg);
        Host->State.PendingAction = AURORA_ACTION_NONE;
        break;

      case AURORA_ACTION_SAVE_CONFIG:
        SaveBiosConfig (Host);
        AuroraBiosSetStatus (
          &Host->State,
          Host->State.Info.EspWritable
            ? "Settings saved to UEFI NVRAM + ESP:\\EFI\\Aurora\\bios.cfg"
            : "Settings saved to UEFI NVRAM",
          0x0022C55Eu
          );
        Host->State.PendingAction = AURORA_ACTION_NONE;
        break;

      case AURORA_ACTION_SAVE_AND_BOOT:
        SaveBiosConfig (Host);
        Host->State.PendingAction = AURORA_ACTION_NONE;
        Running = FALSE;
        break;

      case AURORA_ACTION_SAVE_AND_REBOOT:
        SaveBiosConfig (Host);
        ClearGopScreen (Host->Gop);
        gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
        Running = FALSE;
        break;

      case AURORA_ACTION_DISCARD_AND_BOOT:
        Host->State.PendingAction = AURORA_ACTION_NONE;
        Running = FALSE;
        break;

      case AURORA_ACTION_REORDER_BOOT:
        Host->State.PendingAction = AURORA_ACTION_NONE;
        break;

      case AURORA_ACTION_SCAN_USB_HID:
        ScanUsbHidOnDemand (Host);
        AuroraBiosSetStatus (
          &Host->State,
          (Host->State.Info.MtpKbReady || (Host->SimplePtrCount + Host->AbsPtrCount) > 0)
            ? "Input scan complete: Built-in MTP / HID devices active"
            : "Input scan complete",
          0x0022C55Eu
          );
        Host->State.PendingAction = AURORA_ACTION_NONE;
        break;

      case AURORA_ACTION_BOOT_OPTION: {
        UINT32  Sel = Host->State.PendingActionArg;
        Host->State.PendingAction = AURORA_ACTION_NONE;
        if (Sel < Host->State.BootEntryCount) {
          UINT16  OptNum = Host->State.BootEntries[Sel].OptionNumber;
          UINTN   K;
          for (K = 0; K < Host->BootOptionCount; K++) {
            if ((UINT16)Host->BootOptions[K].OptionNumber == OptNum) {
              SaveBiosConfig (Host);
              ClearGopScreen (Host->Gop);
              if (gST->ConOut != NULL) {
                gST->ConOut->EnableCursor (gST->ConOut, TRUE);
              }
              EfiBootManagerBoot (&Host->BootOptions[K]);
              if (gST->ConOut != NULL) {
                gST->ConOut->EnableCursor (gST->ConOut, FALSE);
              }
              AuroraBiosSetStatus (&Host->State, "Boot option returned to Aurora UEFI Setup", 0x00F59E0Bu);
              break;
            }
          }
        }
        break;
      }

      case AURORA_ACTION_LAUNCH_SHELL:
        Host->State.PendingAction = AURORA_ACTION_NONE;
        ClearGopScreen (Host->Gop);
        if (gST->ConOut != NULL) {
          gST->ConOut->EnableCursor (gST->ConOut, TRUE);
        }
        LaunchInternalShell (ImageHandle);
        if (gST->ConOut != NULL) {
          gST->ConOut->EnableCursor (gST->ConOut, FALSE);
        }
        AuroraBiosSetStatus (&Host->State, "Returned from UEFI Shell", 0x00FF8533u);
        break;

      case AURORA_ACTION_POWER_OFF:
        ClearGopScreen (Host->Gop);
        gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
        Running = FALSE;
        break;

      default:
        Host->State.PendingAction = AURORA_ACTION_NONE;
        break;
    }

    if (Running) {
      // Non-blocking 4 ms (~250 Hz) poll for ultra-smooth low-latency trackpad motion
      gBS->Stall (4000);
    }
  }

  ClearGopScreen (Host->Gop);
  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  }
  if (Host->BootOptions != NULL) {
    EfiBootManagerFreeLoadOptions (Host->BootOptions, Host->BootOptionCount);
  }
  FreeInputProtocols (Host);
  FreePool (Host->SceneBuf);
  FreePool (Host);

  return EFI_ABORTED;
}

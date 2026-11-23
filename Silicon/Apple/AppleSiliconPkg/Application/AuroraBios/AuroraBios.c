/** @file
  Aurora Silicon Gaming UEFI BIOS - UEFI Application Entry Point.

  Provides a real mouse + keyboard interactive gaming UEFI BIOS for Apple M5
  (J813 / T8142) with zero-lag dirty-rect double-buffered GOP rendering,
  live hardware telemetry, interactive UEFI Boot Manager, and dual-backed
  persistent configuration (UEFI NVRAM + NVMe ESP \EFI\Aurora\bios.cfg).

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

STATIC EFI_GUID  mAuroraBiosConfigGuid = AURORA_BIOS_CONFIG_GUID_INIT;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *Gop;
  UINT32                             *SceneBuf;   // Logical Width * Height (0x00RRGGBB, no cursor)
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltScratch; // Physical dirty-rect staging buffer
  UINTN                              BltScratchPixels;

  EFI_SIMPLE_POINTER_PROTOCOL        **SimplePtrs;
  UINTN                              SimplePtrCount;
  EFI_ABSOLUTE_POINTER_PROTOCOL      **AbsPtrs;
  UINTN                              AbsPtrCount;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  **TextInEx;
  UINTN                              TextInExCount;

  EFI_BOOT_MANAGER_LOAD_OPTION       *BootOptions;
  UINTN                              BootOptionCount;

  AURORA_BIOS_STATE                  State;
} AURORA_BIOS_HOST;

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
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles = NULL;
  UINTN                            Count    = 0;
  UINTN                            I;

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

    // Check whether this volume has an \EFI directory (ESP)
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
    // Prefer a volume that already holds \EFI
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
  UINTN               VarSize = sizeof (Loaded);
  EFI_STATUS          Status;
  BOOLEAN             EspWritable = FALSE;

  AuroraBiosConfigSetDefaults (&Host->State.Config);

  // 1. Prefer persistent ESP config if valid (survives emulated NVRAM resets)
  Status = LoadConfigFromEsp (&Loaded, &EspWritable);
  Host->State.Info.EspWritable = EspWritable;
  if (!EFI_ERROR (Status)) {
    CopyMem (&Host->State.Config, &Loaded, sizeof (Loaded));
    Host->State.Info.EspConfigFound = TRUE;
  } else {
    // 2. Fallback to UEFI runtime variable
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

  // Relative Mouse (USB Mouse / UsbMouseDxe)
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

  // Absolute Mouse / Pointer (UsbMouseAbsolutePointerDxe / HID Touchpad)
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

  // Extended Keyboard Input (UsbKbDxe / TerminalDxe / ConSplitter)
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
ScanUsbHidOnDemand (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  // Connect already-enumerated USB I/O handles (UsbKbDxe, UsbMouseDxe, UsbMouseAbsolutePointerDxe)
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

  {
    CONST CHAR16  *FwVer = (CONST CHAR16 *)PcdGetPtr (PcdFirmwareVersionString);
    if (FwVer != NULL && FwVer[0] != L'\0') {
      UnicodeToAsciiSafe (Host->State.Info.FirmwareVersion, sizeof (Host->State.Info.FirmwareVersion), FwVer);
    }
  }

  // Block storage inventory
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

  // Enumerate live UEFI Boot#### options
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
// Dirty-Rect Double-Buffered Fast GOP Presenter
// ---------------------------------------------------------------------------
STATIC
VOID
PresentDirtyRect (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  AURORA_BIOS_STATE  *S = &Host->State;
  INT32              Scale = S->UiScale;
  INT32              X1, Y1, X2, Y2;
  INT32              Lw, Lh, Pw, Ph;
  INT32              Ly, Lx, Sy, Sx;
  UINT32             *TmpSceneRow = NULL;
  BOOLEAN            DrawCur;

  if (Host->Gop == NULL || Host->SceneBuf == NULL) {
    return;
  }

  if (S->FullRedrawNeeded) {
    AuroraBiosRender (S, Host->SceneBuf);
    S->Dirty.Valid = TRUE;
    S->Dirty.MinX  = 0;
    S->Dirty.MinY  = 0;
    S->Dirty.MaxX  = S->Width;
    S->Dirty.MaxY  = S->Height;
  }

  if (!S->Dirty.Valid) {
    return;
  }

  X1 = S->Dirty.MinX;
  Y1 = S->Dirty.MinY;
  X2 = S->Dirty.MaxX;
  Y2 = S->Dirty.MaxY;
  if (X1 < 0) X1 = 0;
  if (Y1 < 0) Y1 = 0;
  if (X2 > S->Width)  X2 = S->Width;
  if (Y2 > S->Height) Y2 = S->Height;
  S->Dirty.Valid = FALSE;

  Lw = X2 - X1;
  Lh = Y2 - Y1;
  if (Lw <= 0 || Lh <= 0) {
    return;
  }

  Pw = Lw * Scale;
  Ph = Lh * Scale;
  if ((UINTN)Pw * (UINTN)Ph > Host->BltScratchPixels) {
    return;
  }

  // Temporarily overlay cursor into a tiny scratch copy of the dirty rect, or
  // stamp directly while converting SceneBuf -> GOP BLT pixels!
  DrawCur = S->MouseVisible &&
            (S->MouseX < X2) && (S->MouseX + 14 > X1) &&
            (S->MouseY < Y2) && (S->MouseY + 20 > Y1);

  TmpSceneRow = AllocatePool ((UINTN)Lw * (UINTN)Lh * sizeof (UINT32));
  if (TmpSceneRow == NULL) {
    return;
  }

  for (Ly = 0; Ly < Lh; Ly++) {
    CopyMem (
      TmpSceneRow + (UINTN)Ly * (UINTN)Lw,
      Host->SceneBuf + (UINTN)(Y1 + Ly) * (UINTN)S->Width + (UINTN)X1,
      (UINTN)Lw * sizeof (UINT32)
      );
  }
  if (DrawCur) {
    AuroraBiosDrawCursor (TmpSceneRow, Lw, Lh, S->MouseX - X1, S->MouseY - Y1);
  }

  for (Ly = 0; Ly < Lh; Ly++) {
    CONST UINT32  *SrcRow = TmpSceneRow + (UINTN)Ly * (UINTN)Lw;
    for (Sy = 0; Sy < Scale; Sy++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *DstRow =
        Host->BltScratch + (UINTN)(Ly * Scale + Sy) * (UINTN)Pw;
      for (Lx = 0; Lx < Lw; Lx++) {
        UINT32                         C = SrcRow[Lx];
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Px;
        Px.Red      = (UINT8)((C >> 16) & 0xFFu);
        Px.Green    = (UINT8)((C >> 8) & 0xFFu);
        Px.Blue     = (UINT8)(C & 0xFFu);
        Px.Reserved = 0;
        for (Sx = 0; Sx < Scale; Sx++) {
          DstRow[Lx * Scale + Sx] = Px;
        }
      }
    }
  }

  FreePool (TmpSceneRow);

  Host->Gop->Blt (
               Host->Gop,
               Host->BltScratch,
               EfiBltBufferToVideo,
               0,
               0,
               (UINTN)(X1 * Scale),
               (UINTN)(Y1 * Scale),
               (UINTN)Pw,
               (UINTN)Ph,
               (UINTN)Pw * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
               );
}

// ---------------------------------------------------------------------------
// Input Polling (Keyboard + SimplePointer + AbsolutePointer)
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
    case SCAN_F9:    return AURORA_KEY_F9;
    case SCAN_F10:   return AURORA_KEY_F10;
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
  // Allow WASD cursor motion and C left-click for smooth cursor control even
  // over keyboard/serial when no hardware pointer is attached.
  switch (Key->UnicodeChar) {
    case L'w':
    case L'W':
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX, Host->State.MouseY - 18, FALSE);
      return;
    case L's':
    case L'S':
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX, Host->State.MouseY + 18, FALSE);
      return;
    case L'a':
    case L'A':
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX - 18, Host->State.MouseY, FALSE);
      return;
    case L'd':
    case L'D':
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX + 18, Host->State.MouseY, FALSE);
      return;
    case L'c':
    case L'C':
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX, Host->State.MouseY, TRUE);
      AuroraBiosHandleMouseMove (&Host->State, Host->State.MouseX, Host->State.MouseY, FALSE);
      return;
    default:
      break;
  }

  {
    AURORA_BIOS_KEY  K = TranslateInputKey (Key, KeyShiftState);
    if (K != AURORA_KEY_NONE) {
      AuroraBiosHandleKey (&Host->State, K);
    }
  }
}

STATIC
VOID
PollInputs (
  IN OUT AURORA_BIOS_HOST  *Host
  )
{
  UINTN  I;

  // 1. Poll Extended Keyboard Protocols
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

  // 2. Poll Relative USB Mice (EFI_SIMPLE_POINTER_PROTOCOL)
  for (I = 0; I < Host->SimplePtrCount; I++) {
    EFI_SIMPLE_POINTER_STATE  St;
    if (!EFI_ERROR (Host->SimplePtrs[I]->GetState (Host->SimplePtrs[I], &St))) {
      INT32  Dx = St.RelativeMovementX;
      INT32  Dy = St.RelativeMovementY;
      // Normalize high-DPI mouse resolution if reported
      if (Host->SimplePtrs[I]->Mode != NULL) {
        if (Host->SimplePtrs[I]->Mode->ResolutionX > 8) {
          Dx = (INT32)((INT64)Dx * 4 / (INT64)Host->SimplePtrs[I]->Mode->ResolutionX);
        }
        if (Host->SimplePtrs[I]->Mode->ResolutionY > 8) {
          Dy = (INT32)((INT64)Dy * 4 / (INT64)Host->SimplePtrs[I]->Mode->ResolutionY);
        }
      }
      AuroraBiosHandleMouseDelta (&Host->State, Dx, Dy, St.LeftButton);
    }
  }

  // 3. Poll Absolute Pointing Devices (EFI_ABSOLUTE_POINTER_PROTOCOL)
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
// UEFI Shell & Boot Option Launchers
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
// Main Application Entry Point
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
  EFI_EVENT         TickTimer = NULL;
  BOOLEAN           Running   = TRUE;
  UINT32            LastSec   = 0xFFFFFFFFu;

  Host = AllocateZeroPool (sizeof (AURORA_BIOS_HOST));
  if (Host == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Host->Gop);
  if (EFI_ERROR (Status) || Host->Gop == NULL || Host->Gop->Mode == NULL || Host->Gop->Mode->Info == NULL) {
    Print (L"Aurora BIOS: Graphics Output Protocol not found (%r)\r\n", Status);
    FreePool (Host);
    return Status;
  }

  // Disable watchdog timer while user is interactively configuring BIOS
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  AuroraBiosStateInit (
    &Host->State,
    (INT32)Host->Gop->Mode->Info->HorizontalResolution,
    (INT32)Host->Gop->Mode->Info->VerticalResolution
    );
  Host->State.Info.GopBpp =
    (Host->Gop->Mode->Info->PixelFormat == PixelBitMask) ? 30u : 32u;

  Host->SceneBuf = AllocateZeroPool ((UINTN)Host->State.Width * (UINTN)Host->State.Height * sizeof (UINT32));
  Host->BltScratchPixels =
    (UINTN)(Host->State.Width * Host->State.UiScale) *
    (UINTN)(Host->State.Height * Host->State.UiScale);
  Host->BltScratch = AllocatePool (Host->BltScratchPixels * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  if (Host->SceneBuf == NULL || Host->BltScratch == NULL) {
    if (Host->SceneBuf != NULL)   FreePool (Host->SceneBuf);
    if (Host->BltScratch != NULL) FreePool (Host->BltScratch);
    FreePool (Host);
    return EFI_OUT_OF_RESOURCES;
  }

  LoadBiosConfig (Host);
  PopulateTelemetryAndBootOptions (Host);
  DiscoverInputProtocols (Host);
  UpdateClock (Host);
  AuroraBiosBuildLayout (&Host->State);
  Host->State.FullRedrawNeeded = TRUE;

  DEBUG ((
    DEBUG_ERROR,
    "[AURORA BIOS] LIVE at %ux%u (%ux HiDPI, %u bpp) | BootEntries=%u | Ptr=%u+%u KB=%u\r\n"
    "[AURORA BIOS] Controls: 1-5/F1-F5 Tabs | Arrows/Tab Nav | WASD Move Cursor | C Click | Enter/Space | F10 Save&Boot | ESC Exit\r\n",
    Host->State.Info.GopWidth,
    Host->State.Info.GopHeight,
    (UINT32)Host->State.UiScale,
    Host->State.Info.GopBpp,
    Host->State.BootEntryCount,
    Host->State.Info.SimplePointerCount,
    Host->State.Info.AbsPointerCount,
    Host->State.Info.KeyboardCount
    ));

  if (gST->ConOut != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }

  // 16ms (~60 Hz) periodic event for smooth cursor polling & live clock
  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TickTimer);
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (TickTimer, TimerPeriodic, 160000); // 16 ms in 100ns units
  }

  while (Running) {
    PresentDirtyRect (Host);
    PollInputs (Host);

    Host->State.TickCount++;
    if ((Host->State.TickCount % 60u) == 0) {
      Host->State.Info.UptimeSec++;
      UpdateClock (Host);
      if (Host->State.Info.ClockSec != LastSec) {
        LastSec = Host->State.Info.ClockSec;
        // Re-render scene in RAM and dirty only the small top-right clock box
        // (and diagnostics tab if open) instead of re-blitting all 17 MiB.
        AuroraBiosRender (&Host->State, Host->SceneBuf);
        if (Host->State.ActiveTab == AURORA_TAB_DIAG) {
          Host->State.Dirty.Valid = TRUE;
          Host->State.Dirty.MinX  = 0;
          Host->State.Dirty.MinY  = 0;
          Host->State.Dirty.MaxX  = Host->State.Width;
          Host->State.Dirty.MaxY  = Host->State.Height;
        } else {
          Host->State.Dirty.Valid = TRUE;
          Host->State.Dirty.MinX  = Host->State.Width - 120;
          Host->State.Dirty.MinY  = 10;
          Host->State.Dirty.MaxX  = Host->State.Width - 16;
          Host->State.Dirty.MaxY  = 46;
        }
      }
    }

    switch (Host->State.PendingAction) {
      case AURORA_ACTION_NONE:
        break;

      case AURORA_ACTION_SAVE_CONFIG:
        SaveBiosConfig (Host);
        AuroraBiosSetStatus (
          &Host->State,
          Host->State.Info.EspWritable
            ? "Settings saved to NVRAM + ESP:\\EFI\\Aurora\\bios.cfg"
            : "Settings saved to UEFI NVRAM",
          0x0010B981u
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
          (Host->SimplePtrCount + Host->AbsPtrCount) > 0
            ? "USB HID scan complete: Mouse + Keyboard detected!"
            : "USB HID scan complete (plug USB mouse/KB into Type-C port or use keyboard)",
          (Host->SimplePtrCount + Host->AbsPtrCount) > 0 ? 0x0010B981u : 0x00F59E0Bu
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
              // If boot returned (e.g. failed or exited), resume BIOS cleanly
              if (gST->ConOut != NULL) {
                gST->ConOut->EnableCursor (gST->ConOut, FALSE);
              }
              AuroraBiosSetStatus (&Host->State, "Boot option returned to Aurora BIOS", 0x00F59E0Bu);
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
        AuroraBiosSetStatus (&Host->State, "Returned from UEFI Shell", 0x0000E5FFu);
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
      if (TickTimer != NULL) {
        UINTN  EvtIdx;
        gBS->WaitForEvent (1, &TickTimer, &EvtIdx);
      } else {
        gBS->Stall (16000);
      }
    }
  }

  if (TickTimer != NULL) {
    gBS->CloseEvent (TickTimer);
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
  FreePool (Host->BltScratch);
  FreePool (Host);

  // Return EFI_ABORTED so MsPlatform.c:PlatformBootManagerPriorityBoot does not
  // immediately re-open BootManagerMenu a second time, and proceeds straight to
  // normal OS boot (bootmgfw.efi).
  return EFI_ABORTED;
}

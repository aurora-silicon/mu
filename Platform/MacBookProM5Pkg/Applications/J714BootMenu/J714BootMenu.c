/* SPDX-License-Identifier: BSD-2-Clause-Patent */
// Transient boot choices: never write BootOrder or alter a disk's BCD store.
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextIn.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#if J714_UEFI_MTP
#include <Library/J714MtpKeyboardLib.h>
#endif

#define MAX_CHOICES 64
typedef struct {
  EFI_BOOT_MANAGER_LOAD_OPTION Option;
  UINTN Rank;
} CHOICE;
STATIC CHOICE mChoices[MAX_CHOICES];
STATIC UINTN mCount;

STATIC BOOLEAN IsUsb (EFI_HANDLE Handle)
{
  EFI_DEVICE_PATH_PROTOCOL *Node = DevicePathFromHandle (Handle);
  if (Node == NULL) return FALSE;
  for (; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) == MESSAGING_DEVICE_PATH &&
        (DevicePathSubType (Node) == MSG_USB_DP ||
         DevicePathSubType (Node) == MSG_USB_CLASS_DP ||
         DevicePathSubType (Node) == MSG_USB_WWID_DP)) return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN FileExists (EFI_FILE_PROTOCOL *Root, CHAR16 *Name)
{
  EFI_FILE_PROTOCOL *File;
  EFI_FILE_INFO *Info;
  EFI_STATUS Status;
  UINTN Size = 0;
  BOOLEAN Found = FALSE;
  Status = Root->Open (Root, &File, Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) return FALSE;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &Size, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL && Size >= SIZE_OF_EFI_FILE_INFO && Size <= 4096) {
    Info = AllocatePool (Size);
    if (Info != NULL) {
      Status = File->GetInfo (File, &gEfiFileInfoGuid, &Size, Info);
      Found = !EFI_ERROR (Status) && !(Info->Attribute & EFI_FILE_DIRECTORY) && Info->FileSize != 0;
      FreePool (Info);
    }
  }
  File->Close (File);
  return Found;
}

STATIC VOID Add (EFI_HANDLE Handle, EFI_FILE_PROTOCOL *Root, CHAR16 *File,
                 CHAR16 *Label, UINTN Volume, UINTN Rank)
{
  EFI_DEVICE_PATH_PROTOCOL *Path;
  CHAR16 Description[128];
  EFI_STATUS Status;
  if (mCount == MAX_CHOICES || !FileExists (Root, File)) return;
  Path = FileDevicePath (Handle, File);
  if (Path == NULL) return;
  UnicodeSPrint (Description, sizeof (Description), L"%s [%s volume %u]",
                Label, IsUsb (Handle) ? L"USB" : L"local", (UINT32)Volume);
  Status = EfiBootManagerInitializeLoadOption (&mChoices[mCount].Option,
             LoadOptionNumberUnassigned, LoadOptionTypeBoot, LOAD_OPTION_ACTIVE,
             Description, Path, NULL, 0);
  FreePool (Path);
  if (!EFI_ERROR (Status)) mChoices[mCount++].Rank = Rank;
}

STATIC VOID ClearChoices (VOID)
{
  while (mCount != 0) EfiBootManagerFreeLoadOption (&mChoices[--mCount].Option);
}

STATIC VOID Discover (VOID)
{
  EFI_HANDLE *Handles;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root;
  UINTN Count, Index;
  BOOLEAN Usb;
  ClearChoices ();
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                         NULL, &Count, &Handles))) return;
  for (Index = 0; Index < Count; Index++) {
    if (DevicePathFromHandle (Handles[Index]) == NULL ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
        EFI_ERROR (Fs->OpenVolume (Fs, &Root))) continue;
    Usb = IsUsb (Handles[Index]);
    Add (Handles[Index], Root, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
         L"Windows Boot Manager", Index + 1, Usb ? 21 : 10);
    // Separate loaders own their own BCD. Never pretend that a Winre.wim by
    // itself is an EFI application, or pass invented arguments to bootmgfw.
    Add (Handles[Index], Root, L"\\EFI\\Aurora\\Recovery\\bootaa64.efi",
         L"Recovery", Index + 1, 100);
    Add (Handles[Index], Root, L"\\EFI\\Aurora\\WinPE\\bootaa64.efi",
         L"WinPE", Index + 1, 30);
    Add (Handles[Index], Root, L"\\EFI\\BOOT\\BOOTAA64.EFI",
         Usb ? L"USB boot" : L"Local / RAM boot", Index + 1, Usb ? 20 : 40);
    Root->Close (Root);
  }
  FreePool (Handles);
}

// Connect only already-enumerated input devices. No ConnectAll: the proxy
// port is deliberately not a host controller in this profile.
STATIC VOID ConnectInput (VOID)
{
  EFI_HANDLE *Handles;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  UINTN Count, Index;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleTextInProtocolGuid,
                                         NULL, &Count, &Handles))) return;
  for (Index = 0; Index < Count; Index++) {
    Path = DevicePathFromHandle (Handles[Index]);
    if (Path == NULL) continue;
    EfiBootManagerUpdateConsoleVariable (ConIn, Path, NULL);
  }
  FreePool (Handles);
  EfiBootManagerConnectConsoleVariable (ConIn);
}

STATIC UINTN DefaultChoice (VOID)
{
  UINTN Best = MAX_CHOICES, Index;
  for (Index = 0; Index < mCount; Index++) {
    // Recovery is always an explicit choice; never an unattended default.
    if (mChoices[Index].Rank >= 100) continue;
    if (Best == MAX_CHOICES || mChoices[Index].Rank < mChoices[Best].Rank) Best = Index;
  }
  // Two equally preferred volumes require a choice, not handle-order luck.
  if (Best != MAX_CHOICES) {
    for (Index = 0; Index < mCount; Index++) {
      if (Index != Best && mChoices[Index].Rank == mChoices[Best].Rank) {
        // The Microsoft path and removable fallback on the same USB volume
        // are distinct choices too; explicit selection avoids double boots.
        return MAX_CHOICES;
      }
    }
  }
  return Best;
}

STATIC VOID Draw (UINTN Selected)
{
  UINTN Index;
  if (gST->ConOut != NULL) gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"Aurora UEFI boot menu\r\n\r\n");
  if (mCount == 0) Print (L"No readable boot loaders found.\r\n");
  for (Index = 0; Index < mCount; Index++)
    Print (L"%s %u. %s\r\n", Index == Selected ? L">" : L" ",
           (UINT32)(Index + 1), mChoices[Index].Option.Description);
  Print (L"\r\nUp/Down: select   Enter: boot   R: refresh   Esc: return to firmware\r\n");
}

STATIC EFI_STATUS ReadKey (EFI_EVENT Timer, EFI_INPUT_KEY *Key)
{
  EFI_EVENT Events[3], InputPoll;
  UINTN Which;
  EFI_STATUS Status;
  if (gST->ConIn == NULL || gST->ConIn->WaitForKey == NULL) return EFI_NOT_READY;
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &InputPoll);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->SetTimer (InputPoll, TimerPeriodic, 1000000);
  if (EFI_ERROR (Status)) { gBS->CloseEvent (InputPoll); return Status; }
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = InputPoll;
  Events[2] = Timer;
  for (;;) {
    Status = gBS->WaitForEvent (Timer == NULL ? 2 : 3, Events, &Which);
    if (EFI_ERROR (Status)) break;
    if (Which == 2) { Status = EFI_TIMEOUT; break; }
    if (Which == 1) { ConnectInput (); continue; }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
    if (Status != EFI_NOT_READY) break;
  }
  gBS->CloseEvent (InputPoll);
  return Status;
}

EFI_STATUS EFIAPI UefiMain (EFI_HANDLE Image, EFI_SYSTEM_TABLE *System)
{
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_INPUT_KEY Key;
  EFI_EVENT Timer = NULL;
  EFI_STATUS Status;
  UINTN Selected, Attempt;
  BOOLEAN Auto = FALSE, Boot = FALSE;
  // BDS arms an application watchdog before StartImage. An interactive menu
  // must be allowed to wait; EfiBootManagerBoot arms a fresh one for a loader.
  Status = gBS->SetWatchdogTimer (0, 0, 0, NULL);
  if (EFI_ERROR (Status)) return Status;
  if (!EFI_ERROR (gBS->HandleProtocol (Image, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded))) {
    Auto = Loaded->LoadOptionsSize == 5 && Loaded->LoadOptions != NULL &&
           CompareMem (Loaded->LoadOptions, "AUTO", 5) == 0;
  }
#if J714_UEFI_MTP
  Status = J714MtpKeyboardStart ();
  Print (L"Internal keyboard: %r\r\n", Status);
  if (EFI_ERROR (Status)) J714MtpKeyboardStop ();
#endif
  // USB bus enumeration runs on timers. Give newly attached storage/input
  // a bounded settling interval, with callbacks enabled at application TPL.
  for (Attempt = 0; Attempt < 31; Attempt++) {
    ConnectInput ();
    Discover ();
    if (mCount != 0 || Attempt == 30) break;
    gBS->Stall (100000);
  }
  Selected = DefaultChoice ();
  if (Auto && Selected != MAX_CHOICES) {
    Print (L"Booting %s in 2 seconds. Esc/F12 opens the boot menu.\r\n",
           mChoices[Selected].Option.Description);
    Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &Timer);
    if (!EFI_ERROR (Status)) {
      Status = gBS->SetTimer (Timer, TimerRelative, 20000000);
      if (!EFI_ERROR (Status)) {
        // Any key cancels automatic boot, including Esc/F12 on USB keyboards.
        Status = ReadKey (Timer, &Key);
        Boot = Status == EFI_TIMEOUT || Status == EFI_NOT_READY;
      }
      gBS->CloseEvent (Timer);
    }
  }
  if (Selected == MAX_CHOICES) Selected = 0;
  for (;;) {
    if (Boot && Selected < mCount) {
      Print (L"Starting %s...\r\n", mChoices[Selected].Option.Description);
#if J714_UEFI_MTP
      Status = J714MtpKeyboardStop ();
      if (EFI_ERROR (Status)) {
        Print (L"MTP ownership could not be released: %r. Boot cancelled.\r\n", Status);
        Boot = FALSE;
        if (EFI_ERROR (ReadKey (NULL, &Key))) continue;
        continue;
      }
#endif
      EfiBootManagerBoot (&mChoices[Selected].Option);
      Status = mChoices[Selected].Option.Status;
#if J714_UEFI_MTP
      J714MtpKeyboardStart ();
      ConnectInput ();
#endif
      // A returning loader never restarts automatically or silently loops.
      Print (L"Loader returned: %r. Press a key for the menu.\r\n", Status);
      ReadKey (NULL, &Key);
      Boot = FALSE;
    }
    Draw (Selected);
    Status = ReadKey (NULL, &Key);
    if (EFI_ERROR (Status) || Key.ScanCode == SCAN_ESC) {
#if J714_UEFI_MTP
      Status = J714MtpKeyboardStop ();
      if (EFI_ERROR (Status)) {
        Print (L"MTP is still owned by this menu: %r. Cannot unload.\r\n", Status);
        gBS->Stall (100000);
        continue;
      }
#endif
      break;
    }
    if (Key.UnicodeChar == L'r' || Key.UnicodeChar == L'R') {
      ConnectInput ();
      Discover ();
      Selected = 0;
    } else if (Key.ScanCode == SCAN_UP && mCount != 0) {
      Selected = Selected == 0 ? mCount - 1 : Selected - 1;
    } else if (Key.ScanCode == SCAN_DOWN && mCount != 0) {
      Selected = (Selected + 1) % mCount;
    } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN && mCount != 0) {
      Boot = TRUE;
    }
  }
  ClearChoices ();
  // BDS continues its existing shell/RAM policy; do not reset the machine.
  return EFI_ABORTED;
}

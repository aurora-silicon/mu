/** @file
  Select a complete, explicitly marked USB Windows installer before the default
  shell/RAM-disk option. Only the J714 USB-installer firmware profile enables it.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#if defined (J714_USB_INSTALLER) && J714_USB_INSTALLER
#include <Protocol/SimpleFileSystem.h>
#include <Guid/GlobalVariable.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC BOOLEAN mInstallerAttempted;
STATIC CONST CHAR8 mInstallerMarker[] = "AURORA-J714S-INSTALLER-v2\r\n";

STATIC BOOLEAN
UsbPartition (EFI_HANDLE Handle)
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  BOOLEAN Usb = FALSE;
  BOOLEAN Partition = FALSE;
  Node = DevicePathFromHandle (Handle);
  if (Node == NULL) return FALSE;
  for (; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) == MESSAGING_DEVICE_PATH &&
        (DevicePathSubType (Node) == MSG_USB_DP ||
         DevicePathSubType (Node) == MSG_USB_CLASS_DP ||
         DevicePathSubType (Node) == MSG_USB_WWID_DP)) Usb = TRUE;
    if (DevicePathType (Node) == MEDIA_DEVICE_PATH &&
        DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP) Partition = TRUE;
  }
  return Usb && Partition;
}

STATIC BOOLEAN
FilePrefix (EFI_FILE_PROTOCOL *Root, CHAR16 *Name, CONST VOID *Prefix,
            UINTN PrefixSize, BOOLEAN Exact)
{
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS Status;
  UINT8 Buffer[64];
  UINTN Size = Exact ? PrefixSize + 1 : PrefixSize;
  if (Size > sizeof (Buffer)) return FALSE;
  Status = Root->Open (Root, &File, Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) return FALSE;
  Status = File->Read (File, &Size, Buffer);
  File->Close (File);
  return !EFI_ERROR (Status) && Size == PrefixSize &&
         CompareMem (Buffer, Prefix, PrefixSize) == 0;
}

STATIC BOOLEAN
IsInstaller (EFI_HANDLE Handle)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root;
  EFI_STATUS Status;
  BOOLEAN Match;
  if (!UsbPartition (Handle)) return FALSE;
  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) return FALSE;
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) return FALSE;
  Match = FilePrefix (Root, L"\\Aurora\\Installer.id", mInstallerMarker,
                      sizeof (mInstallerMarker) - 1, TRUE) &&
          FilePrefix (Root, L"\\EFI\\BOOT\\BOOTAA64.EFI", "MZ", 2, FALSE) &&
          FilePrefix (Root, L"\\sources\\boot.wim", "MSWIM\0\0\0", 8, FALSE);
  Root->Close (Root);
  return Match;
}

EFI_STATUS
J714UsbInstallerPriorityBoot (EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
  EFI_HANDLE *Handles;
  EFI_HANDLE Selected;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_STATUS Status;
  UINTN Count, Index, Attempt, NextSize;
  UINT16 Next;
  // An explicit one-shot selection (including an installer-requested reboot)
  // takes precedence over the default USB choice. BDS consumes it normally.
  NextSize = sizeof (Next);
  Status = gRT->GetVariable (L"BootNext", &gEfiGlobalVariableGuid, NULL, &NextSize, &Next);
  if (!EFI_ERROR (Status) && NextSize == sizeof (Next)) return EFI_NOT_FOUND;
  if (mInstallerAttempted) return EFI_NOT_FOUND;
  mInstallerAttempted = TRUE;
  // EndOfDxe already connected the right xHCI controller. Allow its USB bus
  // timer to finish enumeration; never ConnectAll or touch the debug port.
  for (Attempt = 0; Attempt < 31; Attempt++) {
    Selected = NULL;
    Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                     NULL, &Count, &Handles);
    if (!EFI_ERROR (Status)) {
      for (Index = 0; Index < Count; Index++) {
        if (!IsInstaller (Handles[Index])) continue;
        if (Selected != NULL) {
          FreePool (Handles);
          DEBUG ((DEBUG_ERROR, "J714_USB_INSTALLER: ambiguous marked USB volumes; preserving normal boot\n"));
          return EFI_NOT_FOUND;
        }
        Selected = Handles[Index];
      }
      FreePool (Handles);
    }
    if (Selected != NULL) {
      Path = FileDevicePath (Selected, L"\\EFI\\BOOT\\BOOTAA64.EFI");
      if (Path == NULL) return EFI_OUT_OF_RESOURCES;
      Status = EfiBootManagerInitializeLoadOption (BootOption, LoadOptionNumberUnassigned,
                   LoadOptionTypeBoot, LOAD_OPTION_ACTIVE, L"Aurora USB Windows installer",
                   Path, NULL, 0);
      FreePool (Path);
      DEBUG ((DEBUG_ERROR, "J714_USB_INSTALLER: default USB boot selected: %r\n", Status));
      return Status;
    }
    if (Attempt < 30) gBS->Stall (100000);
  }
  DEBUG ((DEBUG_INFO, "J714_USB_INSTALLER: no complete marked USB installer; preserving normal boot\n"));
  return EFI_NOT_FOUND;
}
#endif

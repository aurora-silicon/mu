/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../Applications/J714BootMenu/J714BootMenu.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
  EFI_FILE_PROTOCOL Root, File;
  struct { USB_DEVICE_PATH Usb; EFI_DEVICE_PATH_PROTOCOL End; } Path;
  UINTN Files;
  BOOLEAN Directory;
  UINTN OpenCount;
} VOLUME;
STATIC VOLUME Volumes[4];
STATIC UINTN VolumeCount, OptionsLive;
UINT8 EFIAPI DevicePathType (CONST VOID *N) { return ((CONST EFI_DEVICE_PATH_PROTOCOL *)N)->Type; }
UINT8 EFIAPI DevicePathSubType (CONST VOID *N) { return ((CONST EFI_DEVICE_PATH_PROTOCOL *)N)->SubType; }
UINTN EFIAPI DevicePathNodeLength (CONST VOID *N) {
  CONST EFI_DEVICE_PATH_PROTOCOL *P = N; return P->Length[0] | ((UINTN)P->Length[1] << 8);
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI NextDevicePathNode (CONST VOID *N) {
  return (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)N + DevicePathNodeLength (N));
}
BOOLEAN EFIAPI IsDevicePathEnd (CONST VOID *N) {
  return DevicePathType (N) == END_DEVICE_PATH_TYPE && DevicePathSubType (N) == END_ENTIRE_DEVICE_PATH_SUBTYPE;
}
UINT16 EFIAPI SetDevicePathNodeLength (VOID *N, UINTN Size) {
  EFI_DEVICE_PATH_PROTOCOL *P = N; P->Length[0] = (UINT8)Size; P->Length[1] = (UINT8)(Size >> 8); return (UINT16)Size;
}
VOID EFIAPI SetDevicePathEndNode (VOID *N) {
  EFI_DEVICE_PATH_PROTOCOL *P = N; P->Type = END_DEVICE_PATH_TYPE;
  P->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE; SetDevicePathNodeLength (P, 4);
}
STATIC EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
EFI_GUID gEfiSimpleFileSystemProtocolGuid, gEfiFileInfoGuid;
STATIC CONST CHAR16 *Names[] = {L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
  L"\\EFI\\Aurora\\Recovery\\bootaa64.efi", L"\\EFI\\Aurora\\WinPE\\bootaa64.efi",
  L"\\EFI\\BOOT\\BOOTAA64.EFI"};
STATIC BOOLEAN Equal (CONST CHAR16 *A, CONST CHAR16 *B) {
  while (*A && *A == *B) { A++; B++; } return *A == *B;
}
VOID *EFIAPI AllocatePool (UINTN Size) { return malloc (Size); }
VOID EFIAPI FreePool (VOID *P) { free (P); }
UINTN EFIAPI UnicodeSPrint (CHAR16 *B, UINTN Size, CONST CHAR16 *Format, ...) {
  // Labels are not part of the discovery policy under test.
  assert (Size >= 2); B[0] = 0; return 0;
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI DevicePathFromHandle (EFI_HANDLE H) {
  return (EFI_DEVICE_PATH_PROTOCOL *)&((VOLUME *)H)->Path;
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI FileDevicePath (EFI_HANDLE H, CONST CHAR16 *File) {
  // Distinct owned path allocations verify cleanup on repeated discovery.
  return calloc (1, 8);
}
EFI_STATUS EFIAPI EfiBootManagerInitializeLoadOption (EFI_BOOT_MANAGER_LOAD_OPTION *O,
  UINTN Number, EFI_BOOT_MANAGER_LOAD_OPTION_TYPE Type, UINT32 Attributes,
  CHAR16 *Description, EFI_DEVICE_PATH_PROTOCOL *Path, UINT8 *Data, UINT32 DataSize) {
  memset (O, 0, sizeof (*O)); O->FilePath = calloc (1, 8); OptionsLive++; return EFI_SUCCESS;
}
EFI_STATUS EFIAPI EfiBootManagerFreeLoadOption (EFI_BOOT_MANAGER_LOAD_OPTION *O) {
  free (O->FilePath); O->FilePath = NULL; assert (OptionsLive); OptionsLive--; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Open (EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **File,
  CHAR16 *Name, UINT64 Mode, UINT64 Attributes) {
  VOLUME *V = BASE_CR (This, VOLUME, Root);
  assert (Mode == EFI_FILE_MODE_READ && Attributes == 0);
  for (UINTN I = 0; I < ARRAY_SIZE (Names); I++) {
    if (Equal (Name, Names[I]) && (V->Files & (1U << I))) {
      V->OpenCount++; *File = &V->File; return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}
STATIC EFI_STATUS EFIAPI Close (EFI_FILE_PROTOCOL *This) {
  for (UINTN I = 0; I < VolumeCount; I++) if (This == &Volumes[I].File) {
    assert (Volumes[I].OpenCount); Volumes[I].OpenCount--; return EFI_SUCCESS;
  }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Info (EFI_FILE_PROTOCOL *This, EFI_GUID *Guid,
  UINTN *Size, VOID *Buffer) {
  VOLUME *V = BASE_CR (This, VOLUME, File);
  if (*Size < sizeof (EFI_FILE_INFO)) { *Size = sizeof (EFI_FILE_INFO); return EFI_BUFFER_TOO_SMALL; }
  EFI_FILE_INFO *F = Buffer; memset (F, 0, sizeof (*F)); F->FileSize = 512;
  F->Attribute = V->Directory ? EFI_FILE_DIRECTORY : 0; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI OpenVolume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs, EFI_FILE_PROTOCOL **Root) {
  *Root = &BASE_CR (Fs, VOLUME, Fs)->Root; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Locate (EFI_LOCATE_SEARCH_TYPE Type, EFI_GUID *Guid,
  VOID *Key, UINTN *Count, EFI_HANDLE **Handles) {
  *Count = VolumeCount; *Handles = malloc (sizeof (EFI_HANDLE) * VolumeCount);
  for (UINTN I = 0; I < VolumeCount; I++) (*Handles)[I] = &Volumes[I];
  return VolumeCount ? EFI_SUCCESS : EFI_NOT_FOUND;
}
STATIC EFI_STATUS EFIAPI HandleProtocol (EFI_HANDLE H, EFI_GUID *Guid, VOID **Protocol) {
  *Protocol = &((VOLUME *)H)->Fs; return EFI_SUCCESS;
}
STATIC VOID Setup (UINTN I, BOOLEAN Usb, UINTN Files) {
  VOLUME *V = &Volumes[I]; memset (V, 0, sizeof (*V));
  V->Fs.OpenVolume = OpenVolume; V->Root.Open = Open; V->Root.Close = Close;
  V->File.Close = Close; V->File.GetInfo = Info; V->Files = Files;
  V->Path.Usb.Header.Type = Usb ? MESSAGING_DEVICE_PATH : HARDWARE_DEVICE_PATH;
  V->Path.Usb.Header.SubType = Usb ? MSG_USB_DP : HW_VENDOR_DP;
  SetDevicePathNodeLength (&V->Path.Usb, sizeof (USB_DEVICE_PATH));
  SetDevicePathEndNode (&V->Path.End);
}
int main (void) {
  Services.LocateHandleBuffer = Locate; Services.HandleProtocol = HandleProtocol;
  VolumeCount = 1; Setup (0, TRUE, BIT3); Discover ();
  assert (mCount == 1 && DefaultChoice () == 0 && IsUsb (&Volumes[0]));
  // A normal USB with both Microsoft and removable paths still boots once.
  Volumes[0].Files = BIT0 | BIT3; Discover (); assert (mCount == 2 && DefaultChoice () == 1);
  VolumeCount = 2; Setup (1, FALSE, BIT0 | BIT1 | BIT2); Discover ();
  assert (mCount == 5 && DefaultChoice () == 2 && !IsUsb (&Volumes[1]));
  // Two internal installations require selection; no arbitrary handle-order default.
  VolumeCount = 3; Setup (2, FALSE, BIT0); Discover (); assert (DefaultChoice () == MAX_CHOICES);
  VolumeCount = 1; Setup (0, FALSE, BIT1); Discover ();
  assert (mCount == 1 && DefaultChoice () == MAX_CHOICES); // recovery is explicit
  Volumes[0].Directory = TRUE; Discover (); assert (mCount == 0);
  Volumes[0].Directory = FALSE; Volumes[0].Files = 0; Discover (); assert (mCount == 0);
  for (UINTN I = 0; I < ARRAY_SIZE (Volumes); I++) assert (!Volumes[I].OpenCount);
  ClearChoices (); assert (!OptionsLive);
  puts ("PASS: actual UEFI discovery, USB paths, Windows preference, ambiguity, recovery, missing/directory loaders, refresh cleanup");
  return 0;
}

/* SPDX-License-Identifier: MIT */
#include <Uefi.h>
#include <PiDxe.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/FirmwareVolume2.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DEBUG(X) do {} while (0)
#define DEBUG_ERROR 1
STATIC BOOLEAN mTimerPhaseActive, FirmwareVolume, FailTimer;
STATIC UINTN Starts, Changes;
STATIC EFI_LOADED_IMAGE_PROTOCOL Loaded;
STATIC EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS=&Services;
EFI_GUID gEfiLoadedImageProtocolGuid,gEfiFirmwareVolume2ProtocolGuid;
STATIC EFI_STATUS SetFirmwareTimer(BOOLEAN On) {
  Changes++; if(FailTimer) return EFI_DEVICE_ERROR; mTimerPhaseActive=On; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI OriginalStart(EFI_HANDLE H,UINTN *Size,CHAR16 **Data) { Starts++; return EFI_ABORTED; }
STATIC EFI_IMAGE_START mStartImage=OriginalStart;
STATIC EFI_STATUS EFIAPI Protocol(EFI_HANDLE H,EFI_GUID *Guid,VOID **Out) {
  if(Guid==&gEfiLoadedImageProtocolGuid) { *Out=&Loaded; return EFI_SUCCESS; }
  return FirmwareVolume ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
UINT8 EFIAPI DevicePathType(CONST VOID *N) { return ((CONST EFI_DEVICE_PATH_PROTOCOL *)N)->Type; }
UINT8 EFIAPI DevicePathSubType(CONST VOID *N) { return ((CONST EFI_DEVICE_PATH_PROTOCOL *)N)->SubType; }
UINTN EFIAPI DevicePathNodeLength(CONST VOID *N) { CONST EFI_DEVICE_PATH_PROTOCOL *P=N; return P->Length[0]|((UINTN)P->Length[1]<<8); }
EFI_DEVICE_PATH_PROTOCOL *EFIAPI NextDevicePathNode(CONST VOID *N) { return (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)N+DevicePathNodeLength(N)); }
BOOLEAN EFIAPI IsDevicePathEnd(CONST VOID *N) { return DevicePathType(N)==END_DEVICE_PATH_TYPE && DevicePathSubType(N)==END_ENTIRE_DEVICE_PATH_SUBTYPE; }
BOOLEAN EFIAPI CompareGuid(CONST GUID *A,CONST GUID *B) { return memcmp(A,B,sizeof(*A))==0; }
// Extracted verbatim by run-host-tests.sh; no duplicate implementation.
#include "FirmwareBoundaryUnderTest.h"
int main(void) {
  struct { MEDIA_FW_VOL_FILEPATH_DEVICE_PATH Fv; EFI_DEVICE_PATH_PROTOCOL End; } Path={
    {{MEDIA_DEVICE_PATH,MEDIA_PIWG_FW_FILE_DP,{sizeof(MEDIA_FW_VOL_FILEPATH_DEVICE_PATH),0}},
     {0x3f47416b,0x7140,0x4bb0,{0xa0,0x3c,0x56,0x41,0x35,0x21,0,1}}},
    {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{4,0}}};
  Services.HandleProtocol=Protocol; Loaded.FilePath=(VOID *)&Path; Loaded.ImageCodeType=EfiLoaderCode;
  FirmwareVolume=TRUE; mTimerPhaseActive=TRUE;
  assert(IsFirmwareMenu(&Loaded)); assert(StartImage(NULL,NULL,NULL)==EFI_ABORTED && Starts==1 && Changes==0 && mTimerPhaseActive);
  mTimerPhaseActive=FALSE;
  assert(StartImage(NULL,NULL,NULL)==EFI_ABORTED && Starts==2 && Changes==2 && !mTimerPhaseActive);
  // A disk image with the menu GUID in its path still does not inherit FIQ.
  FirmwareVolume=FALSE; mTimerPhaseActive=TRUE;
  assert(!IsFirmwareMenu(&Loaded)); assert(StartImage(NULL,NULL,NULL)==EFI_ABORTED && Starts==3 && Changes==4 && mTimerPhaseActive);
  FirmwareVolume=TRUE; Path.Fv.FvFileName.Data1++;
  assert(!IsFirmwareMenu(&Loaded)); assert(StartImage(NULL,NULL,NULL)==EFI_ABORTED && Starts==4 && Changes==6 && mTimerPhaseActive);
  FailTimer=TRUE; assert(StartImage(NULL,NULL,NULL)==EFI_DEVICE_ERROR && Starts==4);
  Loaded.FilePath=NULL; assert(!IsFirmwareMenu(&Loaded));
  puts("PASS: actual StartImage timer boundary, built-in menu identity, no-USB timer activation, disk/Windows protection, restore, failed-transition refusal");
  return 0;
}

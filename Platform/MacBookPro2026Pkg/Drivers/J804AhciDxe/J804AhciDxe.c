// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, D1: the carrier-emulated AHCI disk
// (windows-m6/D1-AHCI-DESIGN.md section 6; contract in J804Windows.h).
//
// Only acts when all of these hold, otherwise it registers nothing and never
// touches J804_AHCI_BASE:
//   - EL1 (a guest of the carrier) on a J804 device tree;
//   - the launcher wrote /chosen/aura,windows-ahci-v1 with the contract's
//     ABAR, size and GSIV (it does so only after the carrier attached a disk
//     and hooked the ABAR);
//   - the carrier's DRAM store named in the property is not memory UEFI
//     owns (the launcher passes boot_args mem_size = store base - phys base,
//     so the store lies above the RAM PrePi described and is NonExistent in
//     the GCD map; anything else is a launcher bug and the disk is refused).
//
// Then, during DXE dispatch (not at EndOfDxe: BdsDxe caches BootNext before
// it calls PlatformBootManagerBeforeConsole, which signals EndOfDxe):
//   1. the ABAR is added to the GCD as uncached MMIO, which maps it in the
//      stage-1 tables;
//   2. it is registered as a non-discoverable AHCI device (PCI class
//      01/06/01, ABAR as BAR 5, coherent DMA; the resource is marked 32-bit,
//      see J804AhciRegister) and connected recursively:
//      NonDiscoverablePciDeviceDxe -> SataControllerDxe + AtaAtapiPassThru
//      -> AtaBusDxe -> DiskIo -> Partition -> Fat;
//   3. on a file system of that disk it looks for
//      \EFI\Microsoft\Boot\bootmgfw.efi, then \EFI\BOOT\BOOTAA64.EFI, and
//      adds a "Windows Boot Manager (AHCI)" load option;
//   4. boot = disk: the option goes first in BootOrder and becomes BootNext,
//      overriding the appended RAM disk (BootRamdiskHelperDxe always runs
//      before this driver: see the INF). boot = ramdisk: the option goes last
//      in BootOrder and becomes BootNext only if nothing set one.
// No disk, no file system or no loader: nothing changes, so the appended RAM
// disk and the shell behave exactly as without this driver.
//
#include <PiDxe.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/DevicePath.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <J804M1.h>
#include <J804Windows.h>
#include <J804AhciHandshake.h>

//
// NonDiscoverablePciDeviceDxe reports it through PciIo->GetLocation().
//
#define J804_AHCI_UNIQUE_ID  0x804A

#define J804_AHCI_OPTION_NAME  L"Windows Boot Manager (AHCI)"

STATIC CONST CHAR16  *mJ804AhciLoaders[] = {
  L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
  L"\\EFI\\BOOT\\BOOTAA64.EFI",
};

STATIC CONST CHAR8  *mJ804AhciHandshakeError[] = {
  "ok",
  "length is neither 40 nor 24 bytes",
  "ABAR base/size differ from J804_AHCI_BASE/J804_AHCI_SIZE",
  "GSIV differs from J804_GSIV_AHCI",
  "boot selector is neither 0 (disk) nor 1 (ramdisk)",
  "store range empty, unaligned or overlapping the ABAR",
};

/**
  Refuse a store that overlaps anything the GCD map knows: system memory,
  reserved memory or MMIO. The carrier's store is DRAM above the RAM PrePi
  described, so every byte of it must still be NonExistent here.
**/
STATIC
EFI_STATUS
J804AhciCheckStore (
  IN CONST J804_AHCI_HANDSHAKE  *Handshake
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Map;
  EFI_STATUS                       Status;
  UINTN                            Count;
  UINTN                            Index;
  UINT64                           Top;

  if (!Handshake->HasStore) {
    DEBUG ((
      DEBUG_WARN,
      "J804_WINDOWS_AHCI: handshake has no store range (24-byte form); relying on boot_args mem_size alone\n"
      ));
    return EFI_SUCCESS;
  }

  Map    = NULL;
  Status = gDS->GetMemorySpaceMap (&Count, &Map);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI refused: no GCD memory space map: %r\n", Status));
    return Status;
  }

  Status = EFI_SUCCESS;
  for (Index = 0; Index < Count; Index++) {
    Top = Map[Index].BaseAddress + Map[Index].Length;
    if ((Map[Index].GcdMemoryType != EfiGcdMemoryTypeNonExistent) &&
        J804AhciRangesOverlap (Map[Index].BaseAddress, Top, Handshake->StoreBase, Handshake->StoreTop))
    {
      DEBUG ((
        DEBUG_ERROR,
        "J804_WINDOWS_AHCI refused: carrier store 0x%lx..0x%lx overlaps GCD type %u at 0x%lx..0x%lx (launcher must pass mem_size below the store)\n",
        Handshake->StoreBase,
        Handshake->StoreTop,
        (UINT32)Map[Index].GcdMemoryType,
        Map[Index].BaseAddress,
        Top
        ));
      Status = EFI_ACCESS_DENIED;
    }
  }

  FreePool (Map);
  return Status;
}

/**
  Add the ABAR to the GCD map as uncached MMIO (CpuDxe then maps it as
  Device-nGnRnE in the stage-1 tables). Refused unless the page is
  NonExistent or already MMIO.
**/
STATIC
EFI_STATUS
J804AhciMapAbar (
  VOID
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;
  EFI_STATUS                       Status;

  Status = gDS->GetMemorySpaceDescriptor (J804_AHCI_BASE, &Desc);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI refused: no GCD descriptor for the ABAR: %r\n", Status));
    return Status;
  }

  if (Desc.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gDS->AddMemorySpace (
                    EfiGcdMemoryTypeMemoryMappedIo,
                    J804_AHCI_BASE,
                    J804_AHCI_SIZE,
                    EFI_MEMORY_UC
                    );
  } else if ((Desc.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) ||
             (Desc.BaseAddress > J804_AHCI_BASE) ||
             (Desc.BaseAddress + Desc.Length < J804_AHCI_BASE + J804_AHCI_SIZE))
  {
    Status = EFI_ACCESS_DENIED;
  }

  if (!EFI_ERROR (Status)) {
    Status = gDS->SetMemorySpaceAttributes (J804_AHCI_BASE, J804_AHCI_SIZE, EFI_MEMORY_UC);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_AHCI refused: cannot map the ABAR 0x%lx (GCD type %u): %r\n",
      (UINT64)J804_AHCI_BASE,
      (UINT32)Desc.GcdMemoryType,
      Status
      ));
  }

  return Status;
}

/**
  Register the ABAR as a non-discoverable AHCI device.

  NonDiscoverableDeviceRegistrationLib marks a resource that ends above
  4 GiB as a 64-bit BAR, and NonDiscoverablePciDeviceDxe refuses (ASSERT,
  then no BAR at all) a 64-bit BAR in the last BAR slot, which is where an
  AHCI ABAR lives (BAR 5). The ABAR is at 0xF40000000, so the descriptor is
  set back to 32-bit before anything binds to the device. That only changes
  the emulated BAR 5 register in config space (low 32 bits only); every
  PciIo Mem access uses the 64-bit AddrRangeMin of the descriptor.
**/
STATIC
EFI_STATUS
J804AhciRegister (
  OUT EFI_HANDLE  *Controller
  )
{
  NON_DISCOVERABLE_DEVICE  *Device;
  EFI_STATUS               Status;

  *Controller = NULL;
  Status      = RegisterNonDiscoverableMmioDevice (
                  J804_AHCI_UNIQUE_ID,
                  NonDiscoverableDeviceTypeAhci,
                  NonDiscoverableDeviceDmaTypeCoherent,
                  NULL,
                  Controller,
                  1,
                  (UINTN)J804_AHCI_BASE,
                  (UINTN)J804_AHCI_SIZE
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (*Controller, &gEdkiiNonDiscoverableDeviceProtocolGuid, (VOID **)&Device);
  if (EFI_ERROR (Status) ||
      (Device->Resources[0].AddrRangeMin != J804_AHCI_BASE) ||
      (Device->Resources[0].AddrLen != J804_AHCI_SIZE))
  {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }

  Device->Resources[0].AddrSpaceGranularity = 32;
  return EFI_SUCCESS;
}

/**
  Return the full device path of the first Windows loader on a file system
  whose device path starts with the controller's, or NULL.
**/
STATIC
EFI_DEVICE_PATH_PROTOCOL *
J804AhciFindLoader (
  IN EFI_HANDLE  Controller
  )
{
  EFI_DEVICE_PATH_PROTOCOL         *ControllerPath;
  EFI_DEVICE_PATH_PROTOCOL         *Candidate;
  EFI_DEVICE_PATH_PROTOCOL         *Found;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  EFI_HANDLE                       *Handles;
  EFI_STATUS                       Status;
  UINTN                            HandleCount;
  UINTN                            Loader;
  UINTN                            Index;
  UINTN                            PrefixSize;

  ControllerPath = DevicePathFromHandle (Controller);
  if (ControllerPath == NULL) {
    return NULL;
  }

  PrefixSize = GetDevicePathSize (ControllerPath) - END_DEVICE_PATH_LENGTH;
  Handles    = NULL;
  Status     = gBS->LocateHandleBuffer (
                      ByProtocol,
                      &gEfiSimpleFileSystemProtocolGuid,
                      NULL,
                      &HandleCount,
                      &Handles
                      );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  Found = NULL;
  for (Loader = 0; (Loader < ARRAY_SIZE (mJ804AhciLoaders)) && (Found == NULL); Loader++) {
    for (Index = 0; (Index < HandleCount) && (Found == NULL); Index++) {
      Candidate = DevicePathFromHandle (Handles[Index]);
      if ((Candidate == NULL) ||
          (GetDevicePathSize (Candidate) <= PrefixSize + END_DEVICE_PATH_LENGTH) ||
          (CompareMem (Candidate, ControllerPath, PrefixSize) != 0))
      {
        continue;
      }

      Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
      if (EFI_ERROR (Status)) {
        continue;
      }

      Root   = NULL;
      Status = Fs->OpenVolume (Fs, &Root);
      if (EFI_ERROR (Status) || (Root == NULL)) {
        continue;
      }

      File   = NULL;
      Status = Root->Open (Root, &File, (CHAR16 *)mJ804AhciLoaders[Loader], EFI_FILE_MODE_READ, 0);
      if (!EFI_ERROR (Status) && (File != NULL)) {
        File->Close (File);
        Found = FileDevicePath (Handles[Index], mJ804AhciLoaders[Loader]);
        DEBUG ((DEBUG_INFO, "J804_WINDOWS_AHCI: loader %s on file system %u of the AHCI disk\n", mJ804AhciLoaders[Loader], (UINT32)Index));
      }

      Root->Close (Root);
    }
  }

  FreePool (Handles);
  return Found;
}

/**
  Add the boot option and apply the boot rule (file header comment, step 4).
**/
STATIC
EFI_STATUS
J804AhciAddBootOption (
  IN EFI_DEVICE_PATH_PROTOCOL  *LoaderPath,
  IN UINT32                    Boot
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  Option;
  EFI_STATUS                    Status;
  UINT16                        *Existing;
  UINT16                        BootNext;

  Status = EfiBootManagerInitializeLoadOption (
             &Option,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             J804_AHCI_OPTION_NAME,
             LoaderPath,
             NULL,
             0
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EfiBootManagerAddLoadOptionVariable (
             &Option,
             (Boot == J804_AHCI_BOOT_DISK) ? 0 : MAX_UINTN
             );
  if (EFI_ERROR (Status) || (Option.OptionNumber > MAX_UINT16)) {
    EfiBootManagerFreeLoadOption (&Option);
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }

  BootNext = (UINT16)Option.OptionNumber;
  EfiBootManagerFreeLoadOption (&Option);

  if (Boot == J804_AHCI_BOOT_RAMDISK) {
    Existing = NULL;
    GetEfiGlobalVariable2 (EFI_BOOT_NEXT_VARIABLE_NAME, (VOID **)&Existing, NULL);
    if (Existing != NULL) {
      DEBUG ((DEBUG_INFO, "J804_WINDOWS_AHCI: Boot%04x added last; BootNext=Boot%04x (RAM disk) kept\n", BootNext, *Existing));
      FreePool (Existing);
      return EFI_SUCCESS;
    }
  }

  Status = gRT->SetVariable (
                  EFI_BOOT_NEXT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE |
                  EFI_VARIABLE_BOOTSERVICE_ACCESS |
                  EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (BootNext),
                  &BootNext
                  );
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
    "J804_WINDOWS_AHCI: BootNext=Boot%04x selects the AHCI disk (%a mode): %r\n",
    BootNext,
    (Boot == J804_AHCI_BOOT_DISK) ? "disk" : "ramdisk",
    Status
    ));
  return Status;
}

EFI_STATUS
EFIAPI
J804AhciEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  J804_AHCI_HANDSHAKE         Handshake;
  J804_AHCI_HANDSHAKE_STATUS  Parsed;
  EFI_DEVICE_PATH_PROTOCOL    *LoaderPath;
  EFI_HANDLE                  Controller;
  EFI_STATUS                  Status;
  dt_node_t                   *Chosen;
  CONST VOID                  *Value;
  size_t                      Length;
  UINT64                      CurrentEl;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  if (((CurrentEl >> 2) & 3) != 1) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI refused: CurrentEL=0x%lx, the AHCI disk exists only under the carrier\n", CurrentEl));
    return EFI_UNSUPPORTED;
  }

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI refused: the device tree is not a J804\n"));
    return EFI_UNSUPPORTED;
  }

  Chosen = dt_get ("/chosen");
  Length = 0;
  Value  = (Chosen != NULL) ? dt_prop (Chosen, J804_AHCI_ADT_PROPERTY, &Length) : NULL;
  if (Value == NULL) {
    DEBUG ((DEBUG_INFO, "J804_WINDOWS_AHCI: no /chosen/%a, no AHCI disk this boot\n", J804_AHCI_ADT_PROPERTY));
    return EFI_NOT_FOUND;
  }

  Parsed = J804AhciParseHandshake (Value, Length, &Handshake);
  if (Parsed != J804AhciHandshakeOk) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_AHCI refused: /chosen/%a (%u bytes): %a\n",
      J804_AHCI_ADT_PROPERTY,
      (UINT32)Length,
      mJ804AhciHandshakeError[Parsed]
      ));
    return EFI_UNSUPPORTED;
  }

  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_AHCI: ABAR 0x%lx/0x%lx GSIV %u boot %a store 0x%lx..0x%lx\n",
    Handshake.AbarBase,
    Handshake.AbarSize,
    Handshake.Gsiv,
    (Handshake.Boot == J804_AHCI_BOOT_DISK) ? "disk" : "ramdisk",
    Handshake.StoreBase,
    Handshake.StoreTop
    ));

  Status = J804AhciCheckStore (&Handshake);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = J804AhciMapAbar ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = J804AhciRegister (&Controller);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI: cannot register the AHCI device: %r\n", Status));
    return Status;
  }

  //
  // From here on the device stays registered even if no loader is found: the
  // shell (map -r) and a RAM-disk WinPE can still see the disk.
  //
  Status = gBS->ConnectController (Controller, NULL, NULL, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI: connecting the AHCI controller failed: %r\n", Status));
    return EFI_SUCCESS;
  }

  LoaderPath = J804AhciFindLoader (Controller);
  if (LoaderPath == NULL) {
    DEBUG ((DEBUG_WARN, "J804_WINDOWS_AHCI: no Windows loader on the AHCI disk, boot order unchanged\n"));
    return EFI_SUCCESS;
  }

  Status = J804AhciAddBootOption (LoaderPath, Handshake.Boot);
  FreePool (LoaderPath);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_AHCI: boot option not added: %r\n", Status));
  }

  return EFI_SUCCESS;
}

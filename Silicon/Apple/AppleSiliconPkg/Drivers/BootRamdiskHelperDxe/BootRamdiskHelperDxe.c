/**
 * @file BootRamdiskHelperDxe.c
 * @author amarioguy (Arminder Singh)
 * 
 * Sets up an embedded RAMDisk in the FV. For right now this is primarily used to bring up WinPE.
 * 
 * @version 1.0
 * @date 2022-12-25
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh), 2022.
 * 
 */

#include <PiDxe.h>

#include "BootRamdiskHelperDxe.h"

//
// ---------------------------------------------------------------------------
// Appended RAM disk (roadmap Phase 2)
// ---------------------------------------------------------------------------
//
// The FV-embedded path below works (FW-10/FW-11 mount FS1: and execute a PE off
// it) but cannot carry a real WinPE, for two independent reasons:
//
//   1. The whole FD is 0x1D80000 bytes. A `boot.wim` is 200-400 MB and is already
//      compressed, so it will not squeeze into FVMAIN_COMPACT the way a 16 MiB
//      image of mostly zeros did.
//   2. The FV path does AllocateCopyPool() of the entire image, so it needs 2x
//      the image in RAM on top of the copy already in the FV.
//
// m1n1 solves the delivery half for free. `run_guest.py --append-payload` simply
// concatenates the extra files onto the payload and loads the result as one raw
// image (proxyclient/tools/run_guest.py:83-89), so
//
//     run_guest.py -r ... J704_UEFI.fd winpe.img
//
// places winpe.img in guest RAM immediately after the FD, i.e. at
// PcdFdBaseAddress + PcdFdSize. Nothing needs to be added to m1n1.
//
// The image is self-describing, so no side-channel is needed to learn its size:
// a FAT boot sector ends in 0x55AA and its BPB carries the sector size and count.
// We validate that signature before believing anything is there, which also makes
// "no appended image" safe to detect -- whatever follows the FD in that case is
// very unlikely to end in 0x55AA and carry a sane BPB.
//
// This path registers the image IN PLACE. No copy, so no 2x cost and no size cap
// beyond available RAM.
//
#define FAT_BPB_BYTES_PER_SECTOR   0x0B    // UINT16
#define FAT_BPB_TOTAL_SECTORS_16   0x13    // UINT16, 0 when the count needs 32 bits
#define FAT_BPB_TOTAL_SECTORS_32   0x20    // UINT32
#define FAT_BOOT_SIGNATURE_OFFSET  0x1FE   // UINT16, must be 0xAA55
#define FAT_BOOT_SIGNATURE         0xAA55

//
// Sanity bounds. A FAT volume with a sector size outside this range, or a total
// size below one sector or above 8 GB, is not something we appended on purpose --
// treat it as "no image" rather than registering a bogus disk.
//
#define APPENDED_MIN_SIZE  ((UINT64)512)
#define APPENDED_MAX_SIZE  ((UINT64)8 * 1024 * 1024 * 1024)

/**
  Look for an appended FAT image directly after the firmware image in RAM.

  @param[out]  ImageBase  Physical base of the image, if found.
  @param[out]  ImageSize  Size in bytes derived from the FAT BPB, if found.

  @retval EFI_SUCCESS    A plausible FAT volume is present and described.
  @retval EFI_NOT_FOUND  Nothing that looks like a FAT volume is there.
**/
STATIC
EFI_STATUS
FindAppendedRamdisk (
  OUT UINT64  *ImageBase,
  OUT UINT64  *ImageSize
  )
{
  UINT64  Base;
  UINT8   *Sector;
  UINT16  Signature;
  UINT16  BytesPerSector;
  UINT32  TotalSectors;
  UINT64  Size;

  EFI_STATUS                       Status;
  VOID                             *HobPtr;
  APPLE_FD_INFO_HOB                *FdInfo;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;

  //
  // The real FD location comes from PrePi's HOB, never from PcdFdBaseAddress.
  // See Include/Guid/AppleFdInfoHob.h: those PCDs are patchable-in-module, so a
  // DXE driver reads the FDF defaults (0x830000000 / 0x1E00000) which are MMIO
  // here. FW-12 did exactly that and died with
  //   Guest exception: EXCEPTION_LOWER/SERROR   FAR = 0x831e001fe
  //
  HobPtr = GetFirstGuidHob (&gAppleSiliconPkgFdInfoHobGuid);
  if (HobPtr == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "BootRamdiskHelperDxe: no FD info HOB -- cannot locate an appended image "
      "safely, skipping\n"
      ));
    return EFI_NOT_FOUND;
  }

  FdInfo = (APPLE_FD_INFO_HOB *)GET_GUID_HOB_DATA (HobPtr);
  Base   = FdInfo->FdBase + FdInfo->FdSize;

  DEBUG ((
    DEBUG_INFO,
    "BootRamdiskHelperDxe: looking for an appended image at 0x%llx "
    "(HOB FdBase 0x%llx + FdSize 0x%llx)\n",
    Base,
    FdInfo->FdBase,
    FdInfo->FdSize
    ));

  //
  // Never dereference an address we have not confirmed is DRAM. Reading MMIO or a
  // hole here is not a benign miss -- it is an SError that takes the machine down
  // before anything can be logged about it.
  //
  Status = gDS->GetMemorySpaceDescriptor (Base, &Desc);
  if (EFI_ERROR (Status) || (Desc.GcdMemoryType != EfiGcdMemoryTypeSystemMemory)) {
    DEBUG ((
      DEBUG_WARN,
      "BootRamdiskHelperDxe: 0x%llx is not system memory (%r, GcdType %d) -- "
      "refusing to probe it\n",
      Base,
      Status,
      EFI_ERROR (Status) ? -1 : (INT32)Desc.GcdMemoryType
      ));
    return EFI_NOT_FOUND;
  }

  Sector = (UINT8 *)(UINTN)Base;

  //
  // Unaligned reads: the BPB fields are not naturally aligned, so copy them out
  // rather than dereferencing casts into the middle of the sector.
  //
  CopyMem (&Signature, Sector + FAT_BOOT_SIGNATURE_OFFSET, sizeof (Signature));
  if (Signature != FAT_BOOT_SIGNATURE) {
    DEBUG ((
      DEBUG_INFO,
      "BootRamdiskHelperDxe: no boot signature at 0x%llx (found 0x%04x, want 0x%04x)"
      " -- no appended image\n",
      Base + FAT_BOOT_SIGNATURE_OFFSET,
      Signature,
      FAT_BOOT_SIGNATURE
      ));
    return EFI_NOT_FOUND;
  }

  CopyMem (&BytesPerSector, Sector + FAT_BPB_BYTES_PER_SECTOR, sizeof (BytesPerSector));
  CopyMem (&TotalSectors, Sector + FAT_BPB_TOTAL_SECTORS_32, sizeof (TotalSectors));
  if (TotalSectors == 0) {
    UINT16  Total16;
    CopyMem (&Total16, Sector + FAT_BPB_TOTAL_SECTORS_16, sizeof (Total16));
    TotalSectors = Total16;
  }

  if ((BytesPerSector != 512) && (BytesPerSector != 1024) &&
      (BytesPerSector != 2048) && (BytesPerSector != 4096))
  {
    DEBUG ((
      DEBUG_WARN,
      "BootRamdiskHelperDxe: boot signature present but BytesPerSector=%u is not a "
      "valid FAT sector size -- ignoring\n",
      BytesPerSector
      ));
    return EFI_NOT_FOUND;
  }

  Size = (UINT64)TotalSectors * (UINT64)BytesPerSector;
  if ((Size < APPENDED_MIN_SIZE) || (Size > APPENDED_MAX_SIZE)) {
    DEBUG ((
      DEBUG_WARN,
      "BootRamdiskHelperDxe: implausible appended image size 0x%llx "
      "(%u sectors x %u bytes) -- ignoring\n",
      Size,
      TotalSectors,
      BytesPerSector
      ));
    return EFI_NOT_FOUND;
  }

  DEBUG ((
    DEBUG_INFO,
    "BootRamdiskHelperDxe: appended FAT image at 0x%llx, %u sectors x %u bytes "
    "= 0x%llx (%llu MiB)\n",
    Base,
    TotalSectors,
    BytesPerSector,
    Size,
    Size / (1024 * 1024)
    ));

  *ImageBase = Base;
  *ImageSize = Size;
  return EFI_SUCCESS;
}

/**
  Try to claim the appended image's pages so nothing else allocates over them.

  m1n1 places the image beyond PcdFdSize, which is outside the region UEFI already
  treats as firmware, so as far as the memory map is concerned those pages are
  free. Reserving them is best-effort here: by the time a DXE driver runs, some of
  that range may already be in use, in which case this fails and the correct fix
  is a PEI carveout in MemoryInitPeiLib. The return value is logged rather than
  fatal so the first hardware run tells us which of the two we need.
**/
STATIC
VOID
ReserveAppendedRamdisk (
  IN UINT64  Base,
  IN UINT64  Size
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;

  Address = (EFI_PHYSICAL_ADDRESS)Base;
  Status  = gBS->AllocatePages (
                   AllocateAddress,
                   EfiReservedMemoryType,
                   (UINTN)EFI_SIZE_TO_PAGES (Size),
                   &Address
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "BootRamdiskHelperDxe: could not reserve the appended image at 0x%llx (%r). "
      "Continuing -- the RAM disk still works, but those pages are not protected "
      "from later allocation. If this bites, carve the range out in PEI.\n",
      Base,
      Status
      ));
  } else {
    DEBUG ((DEBUG_INFO, "BootRamdiskHelperDxe: reserved 0x%llx bytes at 0x%llx\n", Size, Base));
  }
}

/**
 * @brief Main function for RAMDisk initialization.
 * 
 * Note that we only allocate a RAMDisk with a boot image here if we are told to do so and actually have the file embedded in the FV.
 * 
 * @param ImageHandle 
 * @param SystemTable 
 * @return
 * 
 * EFI_SUCCESS - we initialized the RAMDisk as a bootable device.
 * EFI_UNSUPPORTED - we are configured not to set up the RAMDisk.
 * EFI_NOT_FOUND - no FV embedded candidate image is present.
 * EFI_OUT_OF_RESOURCES - for some reason, we are out of memory and cannot create the ramdisk.
 * EFI_ABORTED - an unexpected error occurred.
 */
EFI_STATUS
EFIAPI
BootRamdiskHelperDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
    EFI_STATUS Status;
    BOOLEAN RamdiskBootConfigured;
    VOID *OriginalRamDiskPtr;
    VOID *DestinationRamdiskPtr;
    UINTN RamDiskSize;
    EFI_GUID *RamDiskRegisterType = &gEfiVirtualDiskGuid; //hardcode to IMG image for now
    EFI_RAM_DISK_PROTOCOL *RamdiskProtocol;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath;
    UINT64 AppendedBase;
    UINT64 AppendedSize;

    DEBUG((DEBUG_INFO, "BootRamdiskHelperDxe started\n"));
    // Before proceeding to RAMDisk creation, check that we're configured to do so
    // and that we have a candidate image with which to create said RAMDisk.
    RamdiskBootConfigured = PcdGetBool(PcdInitializeRamdisk);
    if(!RamdiskBootConfigured) {
        DEBUG((DEBUG_ERROR, "BootRamdiskHelperDxe - FV not configured for ramdisk boot, exiting\n"));
        return EFI_UNSUPPORTED;
    }
    //
    // Prefer an image appended by m1n1 over the one baked into the FV.
    //
    // The appended path is the one that scales to a real WinPE: it is registered
    // in place, so there is no AllocateCopyPool and no FD size limit. The FV path
    // stays as the fallback because it needs no extra argument to run_guest.py,
    // which keeps the small FAT test image (FW-10/FW-11) working unchanged.
    //
    Status = FindAppendedRamdisk (&AppendedBase, &AppendedSize);
    if (!EFI_ERROR (Status)) {
        ReserveAppendedRamdisk (AppendedBase, AppendedSize);

        Status = gBS->LocateProtocol(&gEfiRamDiskProtocolGuid, NULL, (VOID **)&RamdiskProtocol);
        if (EFI_ERROR (Status)) {
            DEBUG ((DEBUG_ERROR, "BootRamdiskHelperDxe: Couldn't find the RAMDisk protocol - %r\n", Status));
            return Status;
        }

        Status = RamdiskProtocol->Register(
                   (UINTN)AppendedBase,
                   AppendedSize,
                   RamDiskRegisterType,
                   NULL,
                   &DevicePath
                   );
        if (EFI_ERROR (Status)) {
            DEBUG ((DEBUG_ERROR, "BootRamdiskHelperDxe: cannot register appended RAM disk - %r\n", Status));
            return Status;
        }

        DEBUG ((
          DEBUG_INFO,
          "BootRamdiskHelperDxe: registered APPENDED RAM disk in place, 0x%llx bytes at 0x%llx\n",
          AppendedSize,
          AppendedBase
          ));
        return EFI_SUCCESS;
    }

    Status = GetSectionFromAnyFv(&gAppleSiliconPkgEmbeddedRamdiskGuid, EFI_SECTION_RAW, 0, &OriginalRamDiskPtr, &RamDiskSize);
    if(EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "BootRamdiskHelperDxe - no appended image and no FV embedded ramdisk, exiting\n"));
        return EFI_NOT_FOUND;
    }

    ASSERT (OriginalRamDiskPtr != NULL);
    ASSERT (RamDiskSize != 0);
    //copy the RAMDisk to a new scratch location
    DestinationRamdiskPtr = AllocateCopyPool(RamDiskSize, OriginalRamDiskPtr);

    ASSERT (DestinationRamdiskPtr != NULL);

    Status = gBS->LocateProtocol(&gEfiRamDiskProtocolGuid, NULL, (VOID **)&RamdiskProtocol);
    if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "BootRamdiskHelperDxe: Couldn't find the RAMDisk protocol - %r\n", Status));
        return Status;
    }
    Status = RamdiskProtocol->Register((UINTN)DestinationRamdiskPtr, (UINT64)RamDiskSize, RamDiskRegisterType, NULL, &DevicePath);
    if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "BootRamdiskHelperDxe: Cannot register RAM Disk - %r\n", Status));
    } else {
        DEBUG ((
          DEBUG_INFO,
          "BootRamdiskHelperDxe: registered FV-EMBEDDED RAM disk (copied), 0x%llx bytes\n",
          (UINT64)RamDiskSize
          ));
    }

    return Status;

}

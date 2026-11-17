// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, D4: publish usb-drd1's xHCI (\_SB.XHC1, PNP0D10) to
// Windows, only when the carrier really passes it through
// (windows-m6/D4-USB-DESIGN.md; contract in J804Windows.h).
//
// XHC1 lives in its own SSDT (J804Xhc1Ssdt, FILE_GUID below), not in the
// DSDT, so that it can be left out at run time. This driver installs that
// SSDT only when all of these hold; otherwise Windows sees no XHC1 and never
// touches the xHCI window (which the carrier's stage 2 denies unless the USB
// ATTACH succeeded):
//   - EL1 (a guest of the carrier), on the boot CPU cpu6, on a J804 device
//     tree (as J804AcpiDxe);
//   - the launcher wrote /chosen/aura,windows-usb-v1 (it does so only after
//     the carrier's ATTACH) with exactly the contract's port mask, GSIV,
//     xHCI base and size, and a well-formed DMA window;
//   - every byte of RAM in the GCD map lies inside that DMA window (the
//     carrier's DART identity map covers exactly the window, so a DMA buffer
//     Windows places anywhere in its RAM is reachable by the controller);
//   - the DMA window is clear of the AHCI store named in
//     /chosen/aura,windows-ahci-v1, when that property is present (the store
//     must never be DMA-reachable);
//   - the xHCI window is not RAM in the GCD map.
//
// Mu itself never maps or touches the controller.
//
#include <PiDxe.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <J804M1.h>
#include <J804Windows.h>
#include <J804AhciHandshake.h>
#include <J804UsbHandshake.h>

//
// MacBookPro2026Pkg/AcpiTables/J804Xhc1Ssdt.inf FILE_GUID.
//
STATIC CONST EFI_GUID  mJ804Xhc1Ssdt = {
  0xf6d32646, 0x0922, 0x4ef3, { 0x83, 0x37, 0xf5, 0x78, 0x0e, 0x98, 0xb3, 0x45 }
};

STATIC CONST CHAR8  *mJ804UsbHandshakeError[] = {
  "ok",
  "length is not 40 bytes",
  "port mask is not 0x2 (usb-drd1 only in this build)",
  "GSIV differs from J804_GSIV_XHC1",
  "xHCI base/size differ from J804_XHC1_BASE/J804_XHC_SIZE",
  "DMA window empty, unaligned or overlapping the xHCI window",
};

STATIC
BOOLEAN
J804UsbGcdIsRam (
  IN EFI_GCD_MEMORY_TYPE  Type
  )
{
  return (BOOLEAN)((Type == EfiGcdMemoryTypeSystemMemory) ||
                   (Type == EfiGcdMemoryTypePersistent) ||
                   (Type == EfiGcdMemoryTypeMoreReliable) ||
                   (Type == EfiGcdMemoryTypeUnaccepted));
}

/**
  Every RAM descriptor of the GCD map must lie inside the DMA window, and the
  xHCI window must not be RAM.
**/
STATIC
EFI_STATUS
J804UsbCheckGcd (
  IN CONST J804_USB_HANDSHAKE  *Handshake
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Map;
  EFI_STATUS                       Status;
  UINTN                            Count;
  UINTN                            Index;
  UINT64                           Base;
  UINT64                           Length;
  UINTN                            RamRanges;

  Map    = NULL;
  Status = gDS->GetMemorySpaceMap (&Count, &Map);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: no GCD memory space map: %r\n", Status));
    return Status;
  }

  Status    = EFI_SUCCESS;
  RamRanges = 0;
  for (Index = 0; Index < Count; Index++) {
    if (!J804UsbGcdIsRam (Map[Index].GcdMemoryType)) {
      continue;
    }

    Base   = Map[Index].BaseAddress;
    Length = Map[Index].Length;
    RamRanges++;
    if (!J804UsbRangeInside (Base, Length, Handshake->DmaBase, Handshake->DmaTop)) {
      DEBUG ((
        DEBUG_ERROR,
        "J804_WINDOWS_USB refused: RAM 0x%lx+0x%lx (GCD type %u) is outside the DMA window 0x%lx..0x%lx\n",
        Base,
        Length,
        (UINT32)Map[Index].GcdMemoryType,
        Handshake->DmaBase,
        Handshake->DmaTop
        ));
      Status = EFI_ACCESS_DENIED;
    }

    if (J804UsbRangesOverlap (Base, Base + Length, J804_XHC1_BASE, J804_XHC1_BASE + J804_XHC_SIZE)) {
      DEBUG ((
        DEBUG_ERROR,
        "J804_WINDOWS_USB refused: the xHCI window 0x%lx is RAM in the GCD map (0x%lx+0x%lx)\n",
        (UINT64)J804_XHC1_BASE,
        Base,
        Length
        ));
      Status = EFI_ACCESS_DENIED;
    }
  }

  FreePool (Map);
  if (!EFI_ERROR (Status) && (RamRanges == 0)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: no RAM in the GCD map\n"));
    Status = EFI_ACCESS_DENIED;
  }

  return Status;
}

/**
  The DMA window must be clear of the carrier's AHCI store. A malformed AHCI
  property refuses USB too: the store's position is then unknown.
**/
STATIC
EFI_STATUS
J804UsbCheckAhciStore (
  IN dt_node_t                 *Chosen,
  IN CONST J804_USB_HANDSHAKE  *Handshake
  )
{
  J804_AHCI_HANDSHAKE  Ahci;
  CONST VOID           *Value;
  size_t               Length;

  Length = 0;
  Value  = dt_prop (Chosen, J804_AHCI_ADT_PROPERTY, &Length);
  if (Value == NULL) {
    return EFI_SUCCESS;
  }

  if (J804AhciParseHandshake (Value, Length, &Ahci) != J804AhciHandshakeOk) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: /chosen/%a is malformed, the AHCI store is unknown\n", J804_AHCI_ADT_PROPERTY));
    return EFI_ACCESS_DENIED;
  }

  if (Ahci.HasStore &&
      J804UsbRangesOverlap (Handshake->DmaBase, Handshake->DmaTop, Ahci.StoreBase, Ahci.StoreTop))
  {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_USB refused: DMA window 0x%lx..0x%lx overlaps the AHCI store 0x%lx..0x%lx\n",
      Handshake->DmaBase,
      Handshake->DmaTop,
      Ahci.StoreBase,
      Ahci.StoreTop
      ));
    return EFI_ACCESS_DENIED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
J804XhciAcpiEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  J804_USB_HANDSHAKE         Handshake;
  J804_USB_HANDSHAKE_STATUS  Parsed;
  EFI_STATUS                 Status;
  dt_node_t                  *Chosen;
  CONST VOID                 *Value;
  size_t                     Length;
  UINT64                     CurrentEl;
  UINT64                     Mpidr;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  Mpidr = ArmReadMpidr () & 0xff00ffffffULL;
  if (((CurrentEl >> 2) & 3) != 1) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: CurrentEL=0x%lx, XHC1 exists only under the carrier\n", CurrentEl));
    return EFI_UNSUPPORTED;
  }

  if (Mpidr != J804_BOOT_CPU_MPIDR) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: running on MPIDR 0x%lx, not the boot cpu6\n", Mpidr));
    return EFI_UNSUPPORTED;
  }

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_USB refused: the device tree is not a J804\n"));
    return EFI_UNSUPPORTED;
  }

  Chosen = dt_get ("/chosen");
  Length = 0;
  Value  = (Chosen != NULL) ? dt_prop (Chosen, J804_USB_ADT_PROPERTY, &Length) : NULL;
  if (Value == NULL) {
    DEBUG ((DEBUG_INFO, "J804_WINDOWS_USB: no /chosen/%a, XHC1 not published this boot\n", J804_USB_ADT_PROPERTY));
    return EFI_NOT_FOUND;
  }

  Parsed = J804UsbParseHandshake (Value, Length, &Handshake);
  if (Parsed != J804UsbHandshakeOk) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_USB refused: /chosen/%a (%u bytes): %a\n",
      J804_USB_ADT_PROPERTY,
      (UINT32)Length,
      mJ804UsbHandshakeError[Parsed]
      ));
    return EFI_UNSUPPORTED;
  }

  Status = J804UsbCheckAhciStore (Chosen, &Handshake);
  if (!EFI_ERROR (Status)) {
    Status = J804UsbCheckGcd (&Handshake);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = LocateAndInstallAcpiFromFv (&mJ804Xhc1Ssdt);
  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_USB: XHC1 published: xHCI 0x%lx/0x%lx GSIV %u DMA 0x%lx..0x%lx: %r\n",
    Handshake.XhciBase,
    Handshake.XhciSize,
    Handshake.Gsiv,
    Handshake.DmaBase,
    Handshake.DmaTop,
    Status
    ));
  return Status;
}

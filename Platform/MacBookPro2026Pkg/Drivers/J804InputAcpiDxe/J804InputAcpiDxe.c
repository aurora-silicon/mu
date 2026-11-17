// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, I0: publish the carrier's keyboard and pointer
// mailbox (\_SB.INP0, J804_INPUT_HID) to Windows, only when the carrier
// really emulates it (windows-m6/INPUT-DESIGN.md; contract in J804Windows.h).
//
// INP0 lives in its own SSDT (J804Inp0Ssdt, FILE_GUID below), not in the
// DSDT, so that it can be left out at run time. This driver installs that
// SSDT only when all of these hold; otherwise Windows sees no INP0 and never
// touches the page (which the carrier hooks only after its input ATTACH):
//   - EL1 (a guest of the carrier), on the boot CPU cpu6, on a J804 device
//     tree (as J804AcpiDxe);
//   - the launcher wrote /chosen/aura,windows-input-v1 (it does so only
//     after the carrier's ATTACH returned 0) with exactly the contract's page
//     base, size, GSIV and version, all four report collections, no reserved
//     feature bit and at least one attached source;
//   - the page is NonExistent in the GCD map (not RAM, not claimed as MMIO
//     by anything in Mu).
//
// Mu itself never maps or touches the page and has no input driver: host
// frames sent while Mu runs only update the carrier's state.
//
#include <PiDxe.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/PcdLib.h>
#include <J804M1.h>
#include <J804Windows.h>
#include <J804InputHandshake.h>

//
// MacBookPro2026Pkg/AcpiTables/J804Inp0Ssdt.inf FILE_GUID.
//
STATIC CONST EFI_GUID  mJ804Inp0Ssdt = {
  0x021b562e, 0x3bd3, 0x4324, { 0xbb, 0x92, 0x25, 0xdb, 0x0e, 0xd0, 0x92, 0x23 }
};

STATIC CONST CHAR8  *mJ804InputHandshakeError[] = {
  "ok",
  "length is not 24 bytes",
  "page base/size differ from J804_INPUT_BASE/J804_INPUT_SIZE",
  "GSIV differs from J804_GSIV_INPUT",
  "version is not J804_INPUT_VERSION",
  "feature mask lacks one of the report bits 0-3",
  "feature mask sets a reserved bit (4-15 or 24-31)",
  "feature mask names no attached source (bits 16-23)",
};

/**
  The whole page must be NonExistent in the GCD map. GCD merges adjacent
  descriptors of one type, so one descriptor covering the page proves it.
**/
STATIC
EFI_STATUS
J804InputCheckGcd (
  VOID
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;
  EFI_STATUS                       Status;

  Status = gDS->GetMemorySpaceDescriptor (J804_INPUT_BASE, &Desc);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_INPUT refused: no GCD descriptor for the page 0x%lx: %r\n",
      (UINT64)J804_INPUT_BASE,
      Status
      ));
    return EFI_ACCESS_DENIED;
  }

  if ((Desc.GcdMemoryType != EfiGcdMemoryTypeNonExistent) ||
      (Desc.BaseAddress > J804_INPUT_BASE) ||
      (Desc.BaseAddress + Desc.Length < (UINT64)J804_INPUT_BASE + J804_INPUT_SIZE))
  {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_INPUT refused: the page 0x%lx+0x%x is not wholly NonExistent in the GCD map (type %u at 0x%lx+0x%lx)\n",
      (UINT64)J804_INPUT_BASE,
      J804_INPUT_SIZE,
      (UINT32)Desc.GcdMemoryType,
      Desc.BaseAddress,
      Desc.Length
      ));
    return EFI_ACCESS_DENIED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
J804InputAcpiEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  J804_INPUT_HANDSHAKE         Handshake;
  J804_INPUT_HANDSHAKE_STATUS  Parsed;
  EFI_STATUS                   Status;
  dt_node_t                    *Chosen;
  CONST VOID                   *Value;
  size_t                       Length;
  UINT64                       CurrentEl;
  UINT64                       Mpidr;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  Mpidr = ArmReadMpidr () & 0xff00ffffffULL;
  if (((CurrentEl >> 2) & 3) != 1) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_INPUT refused: CurrentEL=0x%lx, INP0 exists only under the carrier\n", CurrentEl));
    return EFI_UNSUPPORTED;
  }

  if (Mpidr != J804_BOOT_CPU_MPIDR) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_INPUT refused: running on MPIDR 0x%lx, not the boot cpu6\n", Mpidr));
    return EFI_UNSUPPORTED;
  }

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_INPUT refused: the device tree is not a J804\n"));
    return EFI_UNSUPPORTED;
  }

  Chosen = dt_get ("/chosen");
  Length = 0;
  Value  = (Chosen != NULL) ? dt_prop (Chosen, J804_INPUT_ADT_PROPERTY, &Length) : NULL;
  if (Value == NULL) {
    DEBUG ((DEBUG_INFO, "J804_WINDOWS_INPUT: no /chosen/%a, INP0 not published this boot\n", J804_INPUT_ADT_PROPERTY));
    return EFI_NOT_FOUND;
  }

  Parsed = J804InputParseHandshake (Value, Length, &Handshake);
  if (Parsed != J804InputHandshakeOk) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_INPUT refused: /chosen/%a (%u bytes): %a\n",
      J804_INPUT_ADT_PROPERTY,
      (UINT32)Length,
      mJ804InputHandshakeError[Parsed]
      ));
    return EFI_UNSUPPORTED;
  }

  Status = J804InputCheckGcd ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = LocateAndInstallAcpiFromFv (&mJ804Inp0Ssdt);
  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_INPUT: INP0 published: page 0x%lx/0x%x GSIV %u features 0x%x: %r\n",
    Handshake.Base,
    Handshake.Size,
    Handshake.Gsiv,
    Handshake.Features,
    Status
    ));
  return Status;
}

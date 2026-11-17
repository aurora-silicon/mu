// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, G0 GPU (J804_WINDOWS_GPU builds only): publish the
// carrier-admitted G18 GPU to Windows as \_SB.GPU0, only when the carrier
// really admitted it (windows-m6/WINDOWS-GPU-PLAN.md H10,
// windows-m6/G0-CARRIER-CONTRACT.md sections 7 and 8).
//
// GPU0 lives in its own SSDT (J804Gpu0Ssdt, FILE_GUID below), not in the
// DSDT, so that it can be left out at run time, and because every value in it
// is lifted from the carrier's handoff page: the template carries sentinels
// that this driver replaces (J804GpuPatchSsdt). It installs the table only
// when all of these hold; otherwise Windows sees no GPU0:
//   - EL1 (a guest of the carrier), on the boot CPU cpu6, on a J804 device
//     tree (as J804AcpiDxe);
//   - the launcher wrote /chosen/aura,windows-gpu-v1 (it does so only after
//     the carrier's ADMIT returned 0), naming one 16 KiB page outside RAM;
//   - that page validates (J804GpuParseHandoff: magic, version, CRC-32,
//     T8152/J804, admitted, layout, classes, placement);
//   - every byte of the pool is reserved memory in the GCD map (PrePi took it
//     out of RAM: proof that PrePi accepted the same page) and the sgx window,
//     the mailbox page and the handoff page are not RAM;
//   - none of those ranges overlaps the AHCI store named in
//     /chosen/aura,windows-ahci-v1, when that property is present.
//
// GPU0 lists no interrupt in this build: G0 publishes no GPU line (state bit
// 3 clear) and the lines' trigger type is unmeasured. Completion is polled.
// No power methods: the carrier owns the GPU's power (PSD), Windows never
// powers it. Mu itself never touches the GPU, the mailbox or the pool; it
// only reads the handoff page.
//
#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <Protocol/AcpiTable.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <J804M1.h>
#include <J804Windows.h>
#include <J804AhciHandshake.h>
#include <J804GpuHandoff.h>

//
// MacBookPro2026Pkg/AcpiTables/J804Gpu0Ssdt.inf FILE_GUID.
//
STATIC CONST EFI_GUID  mJ804Gpu0Ssdt = {
  0x3c230d00, 0x9c66, 0x4ca9, { 0xbc, 0xaa, 0x61, 0x4e, 0x03, 0x4e, 0xaa, 0xc2 }
};

STATIC CONST CHAR8  *CONST  mJ804GpuStatusText[] = {
  J804_GPU_STATUS_TEXT
};

STATIC J804_GPU_HANDOFF  mHandoff;

STATIC
BOOLEAN
J804GpuGcdIsRam (
  IN EFI_GCD_MEMORY_TYPE  Type
  )
{
  return (BOOLEAN)((Type == EfiGcdMemoryTypeSystemMemory) ||
                   (Type == EfiGcdMemoryTypePersistent) ||
                   (Type == EfiGcdMemoryTypeMoreReliable) ||
                   (Type == EfiGcdMemoryTypeUnaccepted));
}

/**
  Map the handoff page for reading. It is the carrier's page outside RAM;
  map it write-back so the reads are coherent with the carrier's cached
  writes (it is read-only at stage 2 whatever stage 1 says).
**/
STATIC
EFI_STATUS
J804GpuMapHandoff (
  IN UINT64  Ipa,
  IN UINT64  Size
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;
  EFI_STATUS                       Status;

  Status = gDS->GetMemorySpaceDescriptor (Ipa, &Desc);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: no GCD descriptor for the handoff page 0x%lx: %r\n", Ipa, Status));
    return Status;
  }

  if (Desc.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gDS->AddMemorySpace (
                    EfiGcdMemoryTypeMemoryMappedIo,
                    Ipa,
                    Size,
                    EFI_MEMORY_UC | EFI_MEMORY_WB | EFI_MEMORY_RO
                    );
  } else if ((Desc.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) ||
             (Desc.BaseAddress > Ipa) ||
             (Desc.BaseAddress + Desc.Length < Ipa + Size))
  {
    Status = EFI_ACCESS_DENIED;
  }

  if (!EFI_ERROR (Status)) {
    Status = gDS->SetMemorySpaceAttributes (Ipa, Size, EFI_MEMORY_WB);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_GPU refused: cannot map the handoff page 0x%lx (GCD type %u): %r\n",
      Ipa,
      (UINT32)Desc.GcdMemoryType,
      Status
      ));
  }

  return Status;
}

/**
  The pool must be reserved memory from end to end; the sgx window, the
  mailbox page and the handoff page must not be RAM.
**/
STATIC
EFI_STATUS
J804GpuCheckGcd (
  IN CONST J804_GPU_HANDOFF  *Handoff
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Map;
  EFI_STATUS                       Status;
  UINTN                            Count;
  UINTN                            Index;
  UINT64                           Base;
  UINT64                           Top;
  UINT64                           Covered;
  UINT64                           PoolTop;

  Map    = NULL;
  Status = gDS->GetMemorySpaceMap (&Count, &Map);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: no GCD memory space map: %r\n", Status));
    return Status;
  }

  Status  = EFI_SUCCESS;
  Covered = 0;
  PoolTop = Handoff->PoolBase + Handoff->PoolSize;
  for (Index = 0; Index < Count; Index++) {
    Base = Map[Index].BaseAddress;
    Top  = Base + Map[Index].Length;
    if (J804GpuOverlap (Base, Top, Handoff->PoolBase, PoolTop)) {
      if (Map[Index].GcdMemoryType != EfiGcdMemoryTypeReserved) {
        DEBUG ((
          DEBUG_ERROR,
          "J804_WINDOWS_GPU refused: pool 0x%lx..0x%lx meets GCD type %u at 0x%lx+0x%lx, not reserved memory\n",
          Handoff->PoolBase,
          PoolTop,
          (UINT32)Map[Index].GcdMemoryType,
          Base,
          Map[Index].Length
          ));
        Status = EFI_ACCESS_DENIED;
      } else {
        Covered += MIN (Top, PoolTop) - MAX (Base, Handoff->PoolBase);
      }
    }

    if (J804GpuGcdIsRam (Map[Index].GcdMemoryType) &&
        (J804GpuOverlap (Base, Top, Handoff->SgxBase, Handoff->SgxBase + Handoff->SgxSize) ||
         J804GpuOverlap (Base, Top, Handoff->MailboxBase, Handoff->MailboxBase + Handoff->MailboxSize) ||
         J804GpuOverlap (Base, Top, Handoff->HandoffIpa, Handoff->HandoffIpa + Handoff->HandoffSize)))
    {
      DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: RAM 0x%lx+0x%lx overlaps a GPU0 register or handoff range\n", Base, Map[Index].Length));
      Status = EFI_ACCESS_DENIED;
    }
  }

  FreePool (Map);
  if (!EFI_ERROR (Status) && (Covered != Handoff->PoolSize)) {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_GPU refused: only 0x%lx of the 0x%lx-byte pool is reserved memory (PrePi did not carve it)\n",
      Covered,
      Handoff->PoolSize
      ));
    Status = EFI_ACCESS_DENIED;
  }

  return Status;
}

/**
  No GPU0 range may reach the carrier's AHCI store. A malformed AHCI property
  refuses GPU0 too: the store's position is then unknown.
**/
STATIC
EFI_STATUS
J804GpuCheckAhciStore (
  IN dt_node_t               *Chosen,
  IN CONST J804_GPU_HANDOFF  *Handoff
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
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: /chosen/%a is malformed, the AHCI store is unknown\n", J804_AHCI_ADT_PROPERTY));
    return EFI_ACCESS_DENIED;
  }

  if (Ahci.HasStore &&
      (J804GpuOverlap (Handoff->PoolBase, Handoff->PoolBase + Handoff->PoolSize, Ahci.StoreBase, Ahci.StoreTop) ||
       J804GpuOverlap (Handoff->SgxBase, Handoff->SgxBase + Handoff->SgxSize, Ahci.StoreBase, Ahci.StoreTop) ||
       J804GpuOverlap (Handoff->MailboxBase, Handoff->MailboxBase + Handoff->MailboxSize, Ahci.StoreBase, Ahci.StoreTop) ||
       J804GpuOverlap (Handoff->HandoffIpa, Handoff->HandoffIpa + Handoff->HandoffSize, Ahci.StoreBase, Ahci.StoreTop)))
  {
    DEBUG ((
      DEBUG_ERROR,
      "J804_WINDOWS_GPU refused: a GPU0 range overlaps the AHCI store 0x%lx..0x%lx\n",
      Ahci.StoreBase,
      Ahci.StoreTop
      ));
    return EFI_ACCESS_DENIED;
  }

  return EFI_SUCCESS;
}

/**
  Read the template from the FV, patch it from the validated handoff and
  install it. Nothing is installed when the file is not the J804GPU0 SSDT or
  a sentinel is missing or repeated.
**/
STATIC
EFI_STATUS
J804GpuInstallSsdt (
  IN CONST J804_GPU_HANDOFF  *Handoff
  )
{
  EFI_ACPI_TABLE_PROTOCOL      *AcpiTable;
  EFI_ACPI_DESCRIPTION_HEADER  *Table;
  EFI_STATUS                   Status;
  UINTN                        Size;
  UINTN                        TableKey;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Table  = NULL;
  Size   = 0;
  Status = GetSectionFromAnyFv (&mJ804Gpu0Ssdt, EFI_SECTION_RAW, 0, (VOID **)&Table, &Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: no GPU0 SSDT template in the FV: %r\n", Status));
    return Status;
  }

  if ((Size < sizeof (*Table)) || (Table->Length > Size) ||
      (Table->Signature != EFI_ACPI_6_3_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) ||
      (Table->OemTableId != SIGNATURE_64 ('J', '8', '0', '4', 'G', 'P', 'U', '0')) ||
      !J804GpuPatchSsdt ((UINT8 *)Table, Table->Length, Handoff))
  {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: the GPU0 SSDT template is not the J804GPU0 table with each sentinel once\n"));
    FreePool (Table);
    return EFI_COMPROMISED_DATA;
  }

  TableKey = 0;
  Status   = AcpiTable->InstallAcpiTable (AcpiTable, Table, Table->Length, &TableKey);
  FreePool (Table);
  return Status;
}

EFI_STATUS
EFIAPI
J804GpuAcpiEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  J804_GPU_PROPERTY       Property;
  J804_GPU_STATUS         Parsed;
  EFI_STATUS              Status;
  dt_node_t               *Chosen;
  CONST VOID              *Value;
  size_t                  Length;
  CONST struct boot_args  *BootArgs;
  UINT64                  RamBase;
  UINT64                  RamTop;
  UINT64                  CurrentEl;
  UINT64                  Mpidr;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  Mpidr = ArmReadMpidr () & 0xff00ffffffULL;
  if (((CurrentEl >> 2) & 3) != 1) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: CurrentEL=0x%lx, GPU0 exists only under the carrier\n", CurrentEl));
    return EFI_UNSUPPORTED;
  }

  if (Mpidr != J804_BOOT_CPU_MPIDR) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: running on MPIDR 0x%lx, not the boot cpu6\n", Mpidr));
    return EFI_UNSUPPORTED;
  }

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: the device tree is not a J804\n"));
    return EFI_UNSUPPORTED;
  }

  Chosen = dt_get ("/chosen");
  Length = 0;
  Value  = (Chosen != NULL) ? dt_prop (Chosen, J804_GPU_ADT_PROPERTY, &Length) : NULL;
  if (Value == NULL) {
    DEBUG ((DEBUG_INFO, "J804_WINDOWS_GPU: no /chosen/%a, GPU0 not published this boot\n", J804_GPU_ADT_PROPERTY));
    return EFI_NOT_FOUND;
  }

  //
  // The carrier's guest RAM window is the boot_args window (PrePi's copy);
  // Mu's own RAM ends at the pool base when PrePi carved it.
  //
  BootArgs = (CONST struct boot_args *)(UINTN)FixedPcdGet64 (PcdBootArgsPointer);
  RamBase  = BootArgs->phys_base;
  RamTop   = BootArgs->phys_base + BootArgs->mem_size;
  Parsed   = J804GpuParseProperty (Value, Length, &Property);
  if ((Parsed == J804GpuOk) && J804GpuOverlap (Property.Ipa, Property.Ipa + Property.Size, RamBase, RamTop)) {
    Parsed = J804GpuBadPropertyPage;
  }

  if (Parsed != J804GpuOk) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: /chosen/%a (%u bytes): %a\n", J804_GPU_ADT_PROPERTY, (UINT32)Length, mJ804GpuStatusText[Parsed]));
    return EFI_UNSUPPORTED;
  }

  Status = J804GpuMapHandoff (Property.Ipa, Property.Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Parsed = J804GpuParseHandoff ((CONST volatile UINT8 *)(UINTN)Property.Ipa, Property.Ipa, RamBase, RamTop, &mHandoff);
  if (Parsed != J804GpuOk) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_GPU refused: handoff page 0x%lx: %a\n", Property.Ipa, mJ804GpuStatusText[Parsed]));
    return EFI_UNSUPPORTED;
  }

  if ((mHandoff.State & J804_GPU_S_IRQ_LIVE) != 0) {
    DEBUG ((DEBUG_WARN, "J804_WINDOWS_GPU: the carrier reports live GPU lines; this build still publishes none (polled)\n"));
  }

  Status = J804GpuCheckAhciStore (Chosen, &mHandoff);
  if (!EFI_ERROR (Status)) {
    Status = J804GpuCheckGcd (&mHandoff);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = J804GpuInstallSsdt (&mHandoff);
  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_GPU: GPU0 published: sgx 0x%lx+0x%lx mailbox 0x%lx pool 0x%lx+0x%lx handoff 0x%lx state 0x%x %a, no interrupts: %r\n",
    mHandoff.SgxBase,
    mHandoff.SgxSize,
    mHandoff.MailboxBase,
    mHandoff.PoolBase,
    mHandoff.PoolSize,
    mHandoff.HandoffIpa,
    mHandoff.State,
    mHandoff.Receipt,
    Status
    ));
  return Status;
}

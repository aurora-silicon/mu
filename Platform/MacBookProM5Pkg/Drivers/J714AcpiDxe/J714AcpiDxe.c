// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Installs the T6050 / J714s Windows-profile ACPI tables (MADT + GTDT + FADT +
// DSDT) from the FV. Ported from j873's J873AcpiDxe.
//
// The Windows profile requires EL1 (x1n1 enters Mu at EL1). That is a hard gate.
// This isolated Windows build uses the QEMU fixture topology.
#include <Uefi.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>

#include "../../AcpiTables/T6050J714sTopology.h"

#define J714_BOOT_CPU_MPIDR  T6050_J714S_BOOT_CPU_MPIDR
#define J714_MPIDR_AFF_MASK  0xFFFFFFFFFFULL   // aff0..aff3

STATIC CONST EFI_GUID mTables = {
  0xfd77145d, 0x448f, 0x4a8d, {0xb7, 0x33, 0x61, 0x26, 0x7a, 0x97, 0xba, 0x39}
};

EFI_STATUS EFIAPI J714AcpiEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  UINT64     El;
  UINT64     Mpidr;
  EFI_STATUS Status;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (El));
  if (El != 4) {
    DEBUG ((DEBUG_ERROR, "J714 ACPI refused: Windows profile requires EL1 (CurrentEL=0x%lx)\n", El));
    return EFI_UNSUPPORTED;
  }

  Mpidr = ArmReadMpidr () & J714_MPIDR_AFF_MASK;
  if (Mpidr != J714_BOOT_CPU_MPIDR) {
    DEBUG ((DEBUG_WARN,
      "J714 ACPI: boot MPIDR 0x%lx != expected cpu0 0x%lx; installing anyway (bring-up)\n",
      Mpidr, (UINT64)J714_BOOT_CPU_MPIDR));
  }

  Status = LocateAndInstallAcpiFromFv (&mTables);
  DEBUG ((DEBUG_INFO,
    "J714_WINDOWS_ACPI: %u CPUs, native Apple AIC (hardware=%u), "
    "timer 17/18, PSCI over HVC: %r\n", T6050_J714S_MADT_CPU_COUNT, J714_HARDWARE, Status));
  return Status;
}

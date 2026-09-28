// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Installs the T6050 / J714s Windows-profile ACPI tables (MADT + GTDT + FADT +
// DSDT) from the FV. Ported from j873's J873AcpiDxe.
//
// The Windows profile requires EL1 (x1n1 enters Mu at EL1). That is a hard gate.
// The boot-CPU MPIDR check is a WARNING only, not a refusal: what MPIDR the x1n1
// guest observes for T6050 cpu0 under QEMU has not yet been captured, and a wrong
// literal would silently block all ACPI. The expected value from the J714s ADT is
// cpu0 MPIDR 0x80040000 (low 40 bits).
#include <Uefi.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>

#define J714_BOOT_CPU_MPIDR  0x80040000ULL
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
    "J714_WINDOWS_ACPI: single boot CPU, software GIC (GICD 0xF00000000 / GICR 0xF10000000), "
    "timer 17/18, PSCI over HVC: %r\n", Status));
  return Status;
}

// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Install the J804 windows-profile ACPI tables, only where they are true:
// at EL1 (a guest of the EL2 carrier that emulates the GIC, PSCI and the
// timers these tables describe), on the boot CPU cpu6, on a J804 device
// tree. Anywhere else (for example the same FD started natively at EL2) the
// tables would describe hardware that is not there, so none are installed.
// Derived from MacMini2026Pkg J873AcpiDxe.
//
#include <Uefi.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <J804M1.h>
#include <J804Windows.h>

STATIC CONST EFI_GUID  mJ804Tables = {
  0x7d166191, 0x561b, 0x4db6, { 0x89, 0xee, 0xe0, 0xac, 0xb7, 0x0e, 0xbf, 0xa7 }
};

#if defined (J804_WINDOWS_VUART)
STATIC CONST EFI_GUID  mJ804Dbg2 = {
  0xd888a31b, 0xc636, 0x4fd3, { 0xb6, 0x3d, 0xc2, 0x57, 0x8a, 0xda, 0xdd, 0xf1 }
};
#endif

EFI_STATUS
EFIAPI
J804AcpiEntry (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64      CurrentEl;
  UINT64      Mpidr;
  EFI_STATUS  Status;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  Mpidr = ArmReadMpidr () & 0xff00ffffffULL;
  if (((CurrentEl >> 2) & 3) != 1) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_ACPI refused: CurrentEL=0x%lx, the windows tables need EL1 under the carrier\n", CurrentEl));
    return EFI_UNSUPPORTED;
  }

  if (Mpidr != J804_BOOT_CPU_MPIDR) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_ACPI refused: running on MPIDR 0x%lx, not the boot cpu6\n", Mpidr));
    return EFI_UNSUPPORTED;
  }

  if (!J804M1AdtIsJ804 ()) {
    DEBUG ((DEBUG_ERROR, "J804_WINDOWS_ACPI refused: the device tree is not a J804\n"));
    return EFI_UNSUPPORTED;
  }

  Status = LocateAndInstallAcpiFromFv (&mJ804Tables);
#if defined (J804_WINDOWS_VUART)
  if (!EFI_ERROR (Status)) {
    Status = LocateAndInstallAcpiFromFv (&mJ804Dbg2);
  }
#endif

  DEBUG ((
    DEBUG_INFO,
    "J804_WINDOWS_ACPI: 12 CPUs (cpu6 first), vGIC 0x%lx/0x%lx, timer %u/%u, no USB: %r\n",
    (UINT64)J804_GICD_BASE,
    (UINT64)J804_GICR_BASE,
    J804_TIMER_P_INTID,
    J804_TIMER_V_INTID,
    Status
    ));
  return Status;
}

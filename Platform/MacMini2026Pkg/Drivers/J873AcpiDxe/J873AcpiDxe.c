// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/AcpiLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>

STATIC CONST EFI_GUID mTables = {
  0x277fbc64, 0xea82, 0x4873, {0xb5, 0x13, 0x13, 0x92, 0xfa, 0x36, 0x36, 0x71}
};

EFI_STATUS EFIAPI J873AcpiEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  UINT64 El;
  EFI_STATUS Status;
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (El));
  if (El != 4 || (ArmReadMpidr () & 0xff00ffffffULL) != 0x10100) {
    DEBUG ((DEBUG_ERROR, "J873 ACPI refused: Windows profile requires EL1 on boot cpu6\n"));
    return EFI_UNSUPPORTED;
  }
  Status = LocateAndInstallAcpiFromFv (&mTables);
  DEBUG ((DEBUG_INFO, "J873_WINDOWS_ACPI: two P cores (6,7), software GIC, timer 17/18: %r\n", Status));
  return Status;
}

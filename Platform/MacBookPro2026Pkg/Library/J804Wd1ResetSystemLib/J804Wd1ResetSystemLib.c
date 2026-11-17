/** @file
  J804 ResetSystemLib (HwResetSystemLib) for the windows profile.

  - At EL1 (Mu as a guest of the EL2 carrier): cold and warm reset are PSCI
    SYSTEM_RESET over SMC. The carrier owns the real reset: it shuts the NVMe
    controller down first and then restarts through WD1. The guest never
    touches WD1 itself, so it cannot bypass that ordering.
  - At EL2 (no carrier above, e.g. the same FD started natively like M1):
    cold and warm reset write WD1 directly, the restart path aura and Linux
    use on J804. This firmware has no NVMe driver and writes nothing to disk.
    This path is boot-time only: no OS runs above this firmware at EL2.
  - Shutdown never powers anything off. Powered-off T8152 cores do not come
    back, and there is no qualified power-off path. The calling CPU parks in
    a branch loop with interrupts masked (no WFI); the owner removes power.
  - Platform-specific reset is a cold reset.

  WD1 register use (COUNT +0x10, ALARM +0x14, CTL +0x1c; arm the alarm, clear
  the count, enable the reset) is the sequence of aura's wdt_reboot and the
  Linux apple_wdt driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <IndustryStandard/ArmStdSmc.h>

#include <Library/ArmMonitorLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>
#include <J804Windows.h>

/**
  Library constructor. Nothing to do; kept so that BaseTools orders the
  constructors of the libraries this one depends on.
**/
RETURN_STATUS
EFIAPI
J804Wd1ResetSystemLibConstructor (
  VOID
  )
{
  return RETURN_SUCCESS;
}

STATIC
BOOLEAN
J804AtEl1 (
  VOID
  )
{
  UINT64  CurrentEl;

  __asm__ __volatile__ ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  return ((CurrentEl >> 2) & 3) == 1;
}

STATIC
VOID
J804Park (
  VOID
  )
{
  __asm__ __volatile__ ("msr daifset, #0xf" ::: "memory");
  for ( ; ; ) {
    __asm__ __volatile__ ("yield" ::: "memory");
  }
}

STATIC
VOID
J804Restart (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  if (J804AtEl1 ()) {
    Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET;
    ArmMonitorCall (&Args);
    // The carrier did not reset. Do not fall back to WD1 from the guest.
    DEBUG ((DEBUG_ERROR, "J804 reset: PSCI SYSTEM_RESET returned 0x%lx, parking\n", (UINT64)Args.Arg0));
    J804Park ();
  }

  DEBUG ((DEBUG_INFO, "J804 reset: EL2, restarting through WD1\n"));
  MmioWrite32 (J804_WD1_BASE + J804_WD1_ALARM, 0x100000);
  MmioWrite32 (J804_WD1_BASE + J804_WD1_COUNT, 0);
  MmioWrite32 (J804_WD1_BASE + J804_WD1_CTL, 4);
  __asm__ __volatile__ ("dsb sy" ::: "memory");
  J804Park ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  J804Restart ();
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  J804Restart ();
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "J804 shutdown: no power-off on this platform, parking the CPU\n"));
  J804Park ();
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  J804Restart ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
    case EfiResetCold:
    case EfiResetPlatformSpecific:
      J804Restart ();
      break;

    case EfiResetShutdown:
      ResetShutdown ();
      break;

    default:
      return;
  }
}

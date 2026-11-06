/** SPDX-License-Identifier: BSD-2-Clause-Patent */
// Reuse the pinned public dispatch unchanged under a private name. In
// particular, a rejected Aura request must not fall back to the old PMU shim.
#undef NTASI_J813_PMCCNTR_EMULATION
#define NTASI_J813_PMCCNTR_EMULATION 0
#define CommonCExceptionHandler AuraLocalExceptionHandler
#include "../../../../ARM/TIANO/ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.c"
#undef CommonCExceptionHandler
#include <Library/AuraExceptionForward.h>

VOID
EFIAPI
CommonCExceptionHandler (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  if (!AuraForwardUndefinedSystemRegister (ExceptionType, SystemContext)) {
    AuraLocalExceptionHandler (ExceptionType, SystemContext);
  }
}

/** SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef AURA_EXCEPTION_FORWARD_H
#define AURA_EXCEPTION_FORWARD_H
#include <Uefi.h>
#include <Library/CpuExceptionHandlerLib.h>

BOOLEAN
EFIAPI
AuraForwardUndefinedSystemRegister (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  );
#endif

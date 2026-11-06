/** SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Library/AuraExceptionForward.h>
#include <Library/ArmHvcLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/BaseMemoryLib.h>

#define AURA_SYSREG_REQUEST  0x4e54535247ULL
STATIC volatile UINT32  mForwarding;

BOOLEAN
EFIAPI
AuraForwardUndefinedSystemRegister (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_AARCH64  *Cpu;
  ARM_HVC_ARGS               Call;
  UINT32                     Instruction;
  UINT32                     Class;
  UINT32                     Register;
  BOOLEAN                    Accepted;

  Cpu = SystemContext.SystemContextAArch64;
  if ((ExceptionType != EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS) ||
      (Cpu == NULL) || (Cpu->ESR != 0x02000000ULL) ||
      (Cpu->ELR == 0) || ((Cpu->ELR & 3) != 0) ||
      (Cpu->ELR > MAX_UINT64 - sizeof (Instruction)))
  {
    return FALSE;
  }

  if (InterlockedCompareExchange32 (&mForwarding, 0, 1) != 0) {
    return FALSE;
  }

  Accepted = FALSE;
  Instruction = *(CONST volatile UINT32 *)(UINTN)Cpu->ELR;
  Class = Instruction & 0xffe00000U;
  // op0 >= 2 excludes immediate MSR, hints, and system operations.
  if (((Class == 0xd5200000U) || (Class == 0xd5000000U)) &&
      (((Instruction >> 19) & 3) >= 2))
  {
    Register = Instruction & 31;
    ZeroMem (&Call, sizeof (Call));
    Call.Arg0 = AURA_SYSREG_REQUEST;
    Call.Arg1 = Instruction;
    if (Register < 31) {
      CopyMem (&Call.Arg2, (CONST UINT8 *)&Cpu->X0 + Register * sizeof (UINT64), sizeof (UINT64));
    }

    ArmCallHvc (&Call);
    if (Call.Arg0 == 1) {
      if ((Class == 0xd5200000U) && (Register < 31)) {
        CopyMem ((UINT8 *)&Cpu->X0 + Register * sizeof (UINT64), &Call.Arg1, sizeof (UINT64));
      }
      Cpu->ELR += sizeof (Instruction);
      Accepted = TRUE;
    }
  }

  InterlockedCompareExchange32 (&mForwarding, 1, 0);
  return Accepted;
}

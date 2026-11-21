/** SPDX-License-Identifier: BSD-2-Clause-Patent */
// Select the J813 allocated exception-stack implementation of the
// ArmExceptionLib override only.
#undef NTASI_J813_PMCCNTR_EMULATION
#define NTASI_J813_PMCCNTR_EMULATION 1
#include "../../Override/ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c"
// Only the override declares mAllocatedEl0Stack. If the include above ever
// names Microsoft's file again, this stops the build instead of silently
// dropping the allocated EL0 stack.
_Static_assert (sizeof (mAllocatedEl0Stack) == sizeof (VOID *), "AuraArmExceptionLib must build the ArmExceptionLib override");

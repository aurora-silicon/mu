/** SPDX-License-Identifier: BSD-2-Clause-Patent */
// Select the pinned J813 allocated exception-stack implementation only.
#undef NTASI_J813_PMCCNTR_EMULATION
#define NTASI_J813_PMCCNTR_EMULATION 1
#include "../../../../ARM/TIANO/ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c"

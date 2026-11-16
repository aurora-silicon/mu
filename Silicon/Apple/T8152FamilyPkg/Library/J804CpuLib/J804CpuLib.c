/** @file
  J804 M1 CpuLib: CpuSleep without WFI.

  The M1 firmware runs natively at EL2 on J804 with nothing above it to trap
  WFI, and it is not yet known whether this CPU preserves register state
  across WFI in that configuration. CpuDxe's idle callback is the only
  CpuSleep caller and simply returns to the DXE dispatcher's idle loop, so a
  short run of YIELD hints followed by a return is a safe busy-wait idle.
  Pending interrupts are taken as soon as the caller's interrupt mask allows,
  exactly as after a WFI wake-up.

  CpuFlushTlb is unchanged from MdePkg BaseCpuLib (TLBI VMALLE1).

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/CpuLib.h>

#define IDLE_YIELDS  64

VOID
EFIAPI
CpuSleep (
  VOID
  )
{
  UINTN  Count;

  for (Count = 0; Count < IDLE_YIELDS; Count++) {
    __asm__ __volatile__ ("yield" ::: "memory");
  }
}

VOID
EFIAPI
CpuFlushTlb (
  VOID
  )
{
  __asm__ __volatile__ (
    "tlbi vmalle1\n"
    "dsb  sy\n"
    "isb\n"
    ::: "memory"
    );
}

/** SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <Library/ArmHvcLib.h>
#include <Library/BaseMemoryLib.h>
#include <IndustryStandard/AuraN1Handoff.h>

int
AuraN1Read32 (
  void      *Context,
  uint64_t   Address,
  uint32_t  *Value
  )
{
  ARM_HVC_ARGS  Call;

  (VOID)Context;
  if ((Value == NULL) || ((Address & 3) != 0)) {
    return -1;
  }

  ZeroMem (&Call, sizeof (Call));
  Call.Arg0 = AURA_N1_READ_REQUEST;
  Call.Arg1 = Address;
  Call.Arg2 = 1;
  ArmCallHvc (&Call);
  if ((Call.Arg0 != AURA_N1_READ_ACK) || (Call.Arg1 != 0) ||
      (Call.Arg2 > MAX_UINT32) || (Call.Arg3 != 1))
  {
    return -1;
  }

  *Value = (UINT32)Call.Arg2;
  return 0;
}

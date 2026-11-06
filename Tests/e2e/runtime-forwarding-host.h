/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef RUNTIME_FORWARDING_HOST_H
#define RUNTIME_FORWARDING_HOST_H
#include "memory-peim-host.h"
#define EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS 0
typedef UINTN EFI_EXCEPTION_TYPE;
typedef struct {
  UINT64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13;
  UINT64 X14, X15, X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28;
  UINT64 FP, LR, SP, ELR, ESR, SPSR, FAR;
} EFI_SYSTEM_CONTEXT_AARCH64;
typedef union { EFI_SYSTEM_CONTEXT_AARCH64 *SystemContextAArch64; } EFI_SYSTEM_CONTEXT;
typedef struct { UINTN Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7; } ARM_HVC_ARGS;
void ArmCallHvc(ARM_HVC_ARGS *args);
static inline UINT32 InterlockedCompareExchange32(volatile UINT32 *value, UINT32 compare, UINT32 exchange) {
  __atomic_compare_exchange_n(value,&compare,exchange,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
  return compare;
}
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem(d,s,n) memcpy(d,s,n)
#endif

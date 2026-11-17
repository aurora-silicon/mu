/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host stand-ins for the EDK2 types AuraExceptionForward.c uses. */
#ifndef J804_SYSREG_FORWARD_HOST_H_
#define J804_SYSREG_FORWARD_HOST_H_
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t UINT64; typedef uint32_t UINT32; typedef uint8_t UINT8;
typedef uintptr_t UINTN; typedef bool BOOLEAN; typedef void VOID;
#define TRUE true
#define FALSE false
#define IN
#define OUT
#define CONST const
#define STATIC static
#define EFIAPI
#define MAX_UINT64 UINT64_MAX
#define EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS 0
#define EXCEPT_AARCH64_IRQ 1
typedef UINTN EFI_EXCEPTION_TYPE;
typedef struct {
  UINT64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15;
  UINT64 X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28, FP, LR, SP;
  UINT64 ELR, SPSR, FPSR, ESR, FAR;
} EFI_SYSTEM_CONTEXT_AARCH64;
typedef union { EFI_SYSTEM_CONTEXT_AARCH64 *SystemContextAArch64; } EFI_SYSTEM_CONTEXT;
typedef struct { UINTN Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7; } ARM_HVC_ARGS;
void ArmCallHvc (ARM_HVC_ARGS *Args);
static inline UINT32 InterlockedCompareExchange32 (volatile UINT32 *Value, UINT32 Compare, UINT32 Exchange)
{
  __atomic_compare_exchange_n (Value, &Compare, Exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return Compare;
}
#define ZeroMem(p, n) memset ((p), 0, (n))
#define CopyMem(d, s, n) memcpy ((d), (s), (n))
#endif

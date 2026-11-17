/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host test of the imported AuraArmExceptionLib forwarding helper
 * (AuraExceptionForward.c, compiled unchanged) against a modelled carrier
 * HVC boundary: the NTSRG ABI x0 = 0x4e54535247, x1 = instruction,
 * x2 = source register; an exact x0 = 1 reply accepts and x1 is a read
 * result. Written for the J804 windows profile; it does not run AArch64
 * exception return or the real carrier.
 */
#include "sysreg_forward_host.h"
#include <Library/AuraExceptionForward.h>
#include <assert.h>
#include <stdio.h>

#define NTSRG 0x4e54535247ULL

static unsigned mCalls;
static ARM_HVC_ARGS mLast;
static UINT64 mReplyStatus = 1, mReplyValue = 0x0123456789abcdefULL;
static int mReenter;
static EFI_SYSTEM_CONTEXT mNested;

void ArmCallHvc (ARM_HVC_ARGS *Args)
{
  mCalls++;
  mLast = *Args;
  assert (Args->Arg0 == NTSRG);
  assert (Args->Arg3 == 0 && Args->Arg4 == 0 && Args->Arg5 == 0 && Args->Arg6 == 0 && Args->Arg7 == 0);
  if (mReenter) {
    /* A nested forward while one is in flight must be refused. */
    assert (!AuraForwardUndefinedSystemRegister (EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, mNested));
  }
  Args->Arg0 = mReplyStatus;
  Args->Arg1 = mReplyValue;
}

static void Fill (EFI_SYSTEM_CONTEXT_AARCH64 *Cpu, UINT32 *Insn, UINT32 Encoding)
{
  UINT64 *Gpr = &Cpu->X0;
  memset (Cpu, 0, sizeof (*Cpu));
  for (unsigned i = 0; i < 31; i++) Gpr[i] = 0x1000 + i;
  *Insn = Encoding;
  Cpu->ELR = (UINT64)(UINTN)Insn;
  Cpu->ESR = 0x02000000ULL;
  Cpu->SP = 0x5a5a;
}

static int Forward (EFI_SYSTEM_CONTEXT_AARCH64 *Cpu, EFI_EXCEPTION_TYPE Type)
{
  EFI_SYSTEM_CONTEXT Context = { Cpu };
  return AuraForwardUndefinedSystemRegister (Type, Context);
}

int main (void)
{
  EFI_SYSTEM_CONTEXT_AARCH64 Cpu, Before;
  UINT32 Insn;
  unsigned Scenarios = 0;

  /* MRS x<t>, S3_3_C15_C0_0 (an IMPDEF register) for every Rt, and XZR. */
  for (unsigned Rt = 0; Rt < 32; Rt++) {
    Fill (&Cpu, &Insn, 0xd53bf000U | Rt);
    Before = Cpu;
    mCalls = 0;
    assert (Forward (&Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS));
    assert (mCalls == 1 && mLast.Arg1 == Insn);
    assert (mLast.Arg2 == (Rt < 31 ? 0x1000 + Rt : 0));
    if (Rt < 31) ((UINT64 *)&Before.X0)[Rt] = mReplyValue;
    Before.ELR += 4;
    assert (memcmp (&Cpu, &Before, sizeof (Cpu)) == 0);
    Scenarios++;
  }

  /* MSR S3_3_C15_C0_0, x5: the value goes out, no register changes. */
  Fill (&Cpu, &Insn, 0xd51bf005U);
  Before = Cpu;
  mCalls = 0;
  assert (Forward (&Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS));
  assert (mCalls == 1 && mLast.Arg2 == 0x1005);
  Before.ELR += 4;
  assert (memcmp (&Cpu, &Before, sizeof (Cpu)) == 0);
  Scenarios++;

  /* Any reply other than exactly 1 leaves the context untouched. */
  const UINT64 Rejected[] = { 0, 2, MAX_UINT64, NTSRG };
  for (unsigned i = 0; i < sizeof (Rejected) / sizeof (Rejected[0]); i++) {
    mReplyStatus = Rejected[i];
    Fill (&Cpu, &Insn, 0xd53bf003U);
    Before = Cpu;
    assert (!Forward (&Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS));
    assert (memcmp (&Cpu, &Before, sizeof (Cpu)) == 0);
    Scenarios++;
  }
  mReplyStatus = 1;

  /* Not forwarded, and no HVC issued: wrong exception type, wrong ESR,
     bad ELR, and instructions that are not op0 >= 2 system registers. */
  struct { EFI_EXCEPTION_TYPE Type; UINT64 Esr; UINT32 Encoding; int BadElr; const char *What; } Refused[] = {
    { EXCEPT_AARCH64_IRQ, 0x02000000ULL, 0xd53bf000U, 0, "IRQ" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x96000045ULL, 0xd53bf000U, 0, "data abort ESR" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000001ULL, 0xd53bf000U, 0, "ESR with ISS bits" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0xd503201fU, 0, "NOP hint" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0xd500409fU, 0, "MSR immediate (op0 0)" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0xd5087e20U, 0, "DC (op0 1)" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0x00000000U, 0, "UDF" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0xd53bf000U, 1, "misaligned ELR" },
    { EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, 0x02000000ULL, 0xd53bf000U, 2, "zero ELR" },
  };
  for (unsigned i = 0; i < sizeof (Refused) / sizeof (Refused[0]); i++) {
    Fill (&Cpu, &Insn, Refused[i].Encoding);
    Cpu.ESR = Refused[i].Esr;
    if (Refused[i].BadElr == 1) Cpu.ELR += 2;
    if (Refused[i].BadElr == 2) Cpu.ELR = 0;
    Before = Cpu;
    mCalls = 0;
    if (Forward (&Cpu, Refused[i].Type) || mCalls != 0 || memcmp (&Cpu, &Before, sizeof (Cpu)) != 0) {
      fprintf (stderr, "forwarded or changed: %s\n", Refused[i].What);
      return 1;
    }
    Scenarios++;
  }

  /* Reentrancy: a forward from inside the HVC is refused; the outer one completes. */
  {
    EFI_SYSTEM_CONTEXT_AARCH64 Inner;
    UINT32 InnerInsn;
    Fill (&Inner, &InnerInsn, 0xd53bf001U);
    mNested.SystemContextAArch64 = &Inner;
    mReenter = 1;
    Fill (&Cpu, &Insn, 0xd53bf002U);
    assert (Forward (&Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS));
    mReenter = 0;
    assert (Cpu.X2 == mReplyValue);
    /* The guard is released: the next trap is forwarded again. */
    Fill (&Cpu, &Insn, 0xd53bf004U);
    assert (Forward (&Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS) && Cpu.X4 == mReplyValue);
    Scenarios++;
  }

  printf ("{\"scenarios\": %u, \"result\": \"pass\"}\n", Scenarios);
  return 0;
}

/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "runtime-forwarding-host.h"
#include <Library/AuraExceptionForward.h>
#include <IndustryStandard/AuraN1Handoff.h>

static unsigned calls;
static UINT64 acknowledgement=1;
static EFI_SYSTEM_CONTEXT nested;
static BOOLEAN reenter;
static UINT64 n1_ack=0x4e3141434b31ULL, n1_status, n1_version=1, n1_value=0x12345678;
void ArmCallHvc(ARM_HVC_ARGS *a) {
  calls++;
  assert(a->Arg4==0 && a->Arg5==0 && a->Arg6==0 && a->Arg7==0);
  if(a->Arg0==0x4e54535247ULL) {
    assert((a->Arg1 & 0xffe00000U)==0xd5200000U || (a->Arg1 & 0xffe00000U)==0xd5000000U);
    if(reenter) assert(!AuraForwardUndefinedSystemRegister(0,nested));
    a->Arg0=acknowledgement; a->Arg1=0x123456789abcdef0ULL;
  } else {
    assert(a->Arg0==0x4e3152443332ULL && a->Arg1==0x488028208ULL && a->Arg2==1 && a->Arg3==0);
    a->Arg0=n1_ack; a->Arg1=n1_status; a->Arg2=n1_value; a->Arg3=n1_version;
  }
}

int main(void) {
  EFI_SYSTEM_CONTEXT_AARCH64 c,original;
  EFI_SYSTEM_CONTEXT context={&c};
  UINT32 insn;
  unsigned scenarios=0;
  for(unsigned reg=0;reg<32;reg++) {
    memset(&c,0x5a,sizeof(c)); insn=0xd53b9d00U|reg;
    c.ELR=(UINTN)&insn; c.ESR=0x02000000; original=c;
    assert(AuraForwardUndefinedSystemRegister(0,context));
    if(reg<31) ((UINT64 *)&original.X0)[reg]=0x123456789abcdef0ULL;
    original.ELR+=4; assert(!memcmp(&c,&original,sizeof(c))); scenarios++;
  }
  memset(&c,0x5a,sizeof(c)); insn=0xd51b9c13; c.ELR=(UINTN)&insn; c.ESR=0x02000000;
  original=c; assert(AuraForwardUndefinedSystemRegister(0,context)); original.ELR+=4;
  assert(!memcmp(&c,&original,sizeof(c))); scenarios++;
  const UINT64 rejected[]={0,2,UINT64_MAX,0x4e54535247ULL};
  for(unsigned i=0;i<ARRAY_SIZE(rejected);i++) {
    acknowledgement=rejected[i]; original=c;
    c.ELR=(UINTN)&insn; original=c;
    assert(!AuraForwardUndefinedSystemRegister(0,context));
    assert(!memcmp(&c,&original,sizeof(c))); scenarios++;
  }
  acknowledgement=1;
  for(unsigned mode=0;mode<6;mode++) {
    insn=0xd53b9d00; c.ELR=(UINTN)&insn; c.ESR=0x02000000;
    UINTN exception=0;
    if(mode==0) exception=1;
    if(mode==1) c.ESR=0x96000000;
    if(mode==2) c.ELR++;
    if(mode==3) c.ELR=UINT64_MAX-3;
    if(mode==4) insn=0xd503201f;
    if(mode==5) c.ELR=0;
    original=c; unsigned before=calls;
    assert(!AuraForwardUndefinedSystemRegister(exception,context));
    assert(calls==before && !memcmp(&c,&original,sizeof(c))); scenarios++;
  }
  insn=0xd53b9d00; c.ELR=(UINTN)&insn; c.ESR=0x02000000;
  EFI_SYSTEM_CONTEXT_AARCH64 nested_context=c; nested.SystemContextAArch64=&nested_context;
  reenter=TRUE; unsigned before=calls;
  assert(AuraForwardUndefinedSystemRegister(0,context)); assert(calls==before+1); reenter=FALSE; scenarios++;
  UINT32 value=0;
  assert(AuraN1Read32(NULL,0x488028208ULL,&value)==0 && value==0x12345678); scenarios++;
  for(unsigned mode=0;mode<5;mode++) {
    n1_ack=0x4e3141434b31ULL; n1_status=0; n1_version=1; n1_value=0x12345678;
    if(mode==0) n1_ack=0x4e3152443332ULL;
    if(mode==1) n1_status=UINT64_MAX;
    if(mode==2) n1_status=1;
    if(mode==3) n1_version=2;
    if(mode==4) n1_value=UINT64_MAX;
    value=0xaabbccdd;
    assert(AuraN1Read32(NULL,0x488028208ULL,&value)!=0 && value==0xaabbccdd); scenarios++;
  }
  printf("{\"result\":\"passed\",\"scenarios\":%u,\"hvc_calls\":%u}\n",scenarios,calls);
  return 0;
}

/* SPDX-License-Identifier: MIT */
/* Offline Aura descriptor -> live register census -> Mu admission model. */
#include <IndustryStandard/AuraN1Handoff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AURA_N1_HVC_E2E
#include "runtime-forwarding-host.h"
#endif

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(2);}} while (0)
struct model {unsigned reads; const char *fault;};
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void put64(unsigned char *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static int read32(void *context,uint64_t address,uint32_t *value) {
    struct model *m=context; m->reads++;
    printf("{\"event\":\"read32\",\"address\":%llu}\n",(unsigned long long)address);
    if(!strcmp(m->fault,"read-failure"))return -1;
    switch(address) {
    case UINT64_C(0x488028208): *value=!strcmp(m->fault,"link-down")?0:1; break;
    case UINT64_C(0x1cb0000000): *value=!strcmp(m->fault,"root-id")?0:0x100c106b; break;
    case UINT64_C(0x1cb0100000): *value=!strcmp(m->fault,"control-id")?0:0x1900106b; break;
    case UINT64_C(0x1cb0101000): *value=!strcmp(m->fault,"radios-present")?0x1902106b:UINT32_MAX; break;
    case UINT64_C(0x1cb0102000): *value=!strcmp(m->fault,"radios-present")?0x1903106b:UINT32_MAX; break;
    case UINT64_C(0x1cb0100004):
    case UINT64_C(0x1cb0101004):
    case UINT64_C(0x1cb0102004): *value=!strcmp(m->fault,"bme")?6:2; break;
    case UINT64_C(0x1cb0100010): *value=!strcmp(m->fault,"bar")?0x90000004:0x80000004; break;
    case UINT64_C(0x1cb0100014): *value=!strcmp(m->fault,"bar-high")?1:0; break;
    case UINT64_C(0xb80008000): *value=!strcmp(m->fault,"rom")?2:0; break;
    case UINT64_C(0x488000000): *value=!strcmp(m->fault,"geometry")?0:14u<<24; break;
    case UINT64_C(0x488000008): *value=42u<<24|40u<<16|0x203; break;
    case UINT64_C(0x48800000c): *value=19; break;
    case UINT64_C(0x488000200): *value=!strcmp(m->fault,"protected")?1:0; break;
    case UINT64_C(0x488001000): *value=!strcmp(m->fault,"live-tcr")?1:0; break;
    case UINT64_C(0x488001400): *value=!strcmp(m->fault,"live-ttbr")?1:0; break;
    default: CHECK(0); return -1;
    }
    return 0;
}
#ifdef AURA_N1_HVC_E2E
static struct model *active_model;
void ArmCallHvc(ARM_HVC_ARGS *args) {
    uint32_t value=0;
    CHECK(args->Arg0==UINT64_C(0x4e3152443332) && args->Arg2==1 && args->Arg3==0);
    int status=read32(active_model,args->Arg1,&value);
    args->Arg0=UINT64_C(0x4e3141434b31);args->Arg1=status?UINT64_MAX:0;
    args->Arg2=value;args->Arg3=1;
}
static int guarded_read32(void *context,uint64_t address,uint32_t *value) {
    active_model=context;
    return AuraN1Read32(context,address,value);
}
#else
#define guarded_read32 read32
#endif
int main(int argc,char **argv) {
    static const uint64_t resources[6][2]={{UINT64_C(0x1cb0000000),0x200000},{UINT64_C(0x488000000),0x20000},
        {UINT64_C(0x488028000),0x4000},{UINT64_C(0x48f010000),0x1000},{UINT64_C(0x39a000300),4},
        {UINT64_C(0xb80000000),0x600000}};
    unsigned char storage[129]={0},*record=storage+1;size_t size=128;unsigned i;uint32_t chip=0x8142,board=0x24;
    struct model model={0,NULL};struct aura_n1_reader reader={&model,guarded_read32};int result,metadata=0;
    CHECK(argc==2);model.fault=argv[1];
    put32(record,0x4831574e);put16(record+4,1);put16(record+6,128);put32(record+8,7);
    put32(record+12,chip);put32(record+16,board);put32(record+20,0x32354776);
    put16(record+24,0x100);put16(record+26,0x101);put16(record+28,0x102);
    for(i=0;i<6;i++){put64(record+32+16*i,resources[i][0]);put64(record+40+16*i,resources[i][1]);}
    if(!strcmp(model.fault,"missing")){record=NULL;metadata=1;}
    if(!strcmp(model.fault,"truncated")){size=127;metadata=1;}
    if(!strcmp(model.fault,"version")){put16(record+4,2);metadata=1;}
    if(!strcmp(model.fault,"flags")){put32(record+8,15);metadata=1;}
    if(!strcmp(model.fault,"identity")){board=1;metadata=1;}
    if(!strcmp(model.fault,"stream")){put16(record+30,1);metadata=1;}
    if(!strcmp(model.fault,"resource")){record[33]++;metadata=1;}
    result=aura_n1_validate(record,size,chip,board,&reader);
    CHECK((result==0)==(!strcmp(model.fault,"success") || !strcmp(model.fault,"radios-present")));
    if(metadata)CHECK(model.reads==0);
    if(!strcmp(model.fault,"link-down"))CHECK(model.reads==1);
    printf("{\"case\":\"%s\",\"result\":\"passed\",\"admitted\":%s,\"reads\":%u}\n",model.fault,result==0?"true":"false",model.reads);
    return 0;
}

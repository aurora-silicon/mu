/* SPDX-License-Identifier: MIT */
/* Exact kernel 26100/GUID9B3C83B580575E719D5592619E29C053, age1.
 * HalProcessorIdle keeps its original 40-byte size and unwind prologue/epilogue.
 * CACHEALI ends at e1f100; these two aligned slots occupy mapped zero tail padding.
 */
#ifndef WINDOWS_NATIVE_WFI_H
#define WINDOWS_NATIVE_WFI_H
#include <stdint.h>
#define WIN_NATIVE_WFI_POLICY 0x49464d32UL
#define WIN_NATIVE_WFI_RVA 0x451ae0u
#define WIN_NATIVE_WFI_SIZE 40u
#define WIN_NATIVE_WFI_SLOT_RVA 0xe1f108u
#define WIN_NATIVE_WFI_FIQ_SLOT_RVA 0xe1f100u
#define WIN_NATIVE_WFI_PREPARE_RVA 0x457a18u
#define WIN_NATIVE_WFI_PREPARE_SIZE 64u
#define WIN_NATIVE_WFI_ORIGINAL_HASH 0x45f117ccffa08ea2ULL
#define WIN_NATIVE_WFI_PREPARE_HASH 0xd53730ffc76d3cd4ULL
static uint64_t win_native_wfi_hash(const void *ptr,unsigned len)
{
    const unsigned char *p=ptr;uint64_t h=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<len;i++)h=(h^p[i])*0x100000001b3ULL;
    return h;
}
static void win_native_wfi_build(uint32_t *w,uint64_t pc,uint64_t slot,uint64_t fallback)
{
    int64_t d=(int64_t)((slot&~4095ULL)-((pc+12)&~4095ULL))>>12;
    uint32_t imm=(uint32_t)d&0x1fffff;
    w[0]=0xd503237f;w[1]=0xa9bf7bfd;w[2]=0x910003fd;
    w[3]=0x90000008|((imm&3)<<29)|((imm>>2)<<5);
    w[4]=0xf9400108|(((slot&4095)>>3)<<10);
    w[5]=0xb4000008|(((uint32_t)((int64_t)(fallback-(pc+20))>>2)&0x7ffff)<<5);
    w[6]=0xd63f0100;w[7]=0xa8c17bfd;w[8]=0xd50323ff;w[9]=0xd65f03c0;
}
#endif

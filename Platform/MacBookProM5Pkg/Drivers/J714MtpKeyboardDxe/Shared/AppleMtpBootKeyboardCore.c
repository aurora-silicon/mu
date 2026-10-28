/* SPDX-License-Identifier: MIT */
#include "AppleMtpBootKeyboardCore.h"
#include <string.h>

struct global {
    uint32_t page, size, count, minimum, maximum;
    uint8_t id;
};
struct local {
    uint32_t first, last, usage;
    unsigned has_first, has_last, has_usage;
};
int apple_mtp_boot_keyboard_parse(const uint8_t *d, size_t len,
                                 struct apple_mtp_boot_keyboard *out)
{
    struct global g = {0}, stack[4];
    struct local l = {0};
    struct apple_mtp_boot_keyboard k = {0};
    uint32_t bits[256] = {0};
    unsigned sp=0, depth=0, keyboard[16]={0}, modifiers=0, keys=0, ids=0;
    if (!d || !out || !len || len>4096) return -1;
    for (size_t pos=0;pos<len;) {
        uint8_t prefix=d[pos++];
        unsigned n=prefix&3u, type=(prefix>>2)&3u, tag=prefix>>4;
        uint32_t value=0;
        if (prefix==0xfe) return -1;
        if (n==3) n=4;
        if (n>len-pos) return -1;
        for (unsigned j=0;j<n;j++) value|=(uint32_t)d[pos++]<<(8*j);
        if (type==1) {
            switch (tag) {
            case 0:g.page=value;break;
            case 1:g.minimum=value;break;
            case 2:g.maximum=value;break;
            case 7:g.size=value;break;
            case 8:if (!value || value>255) return -1;g.id=(uint8_t)value;ids=1;break;
            case 9:g.count=value;break;
            case 10:if (sp==4 || n) return -1;stack[sp++]=g;break;
            case 11:if (!sp || n) return -1;g=stack[--sp];break;
            default:break;
            }
        } else if (type==2) {
            // Extended usages encode their page in the upper half.
            uint32_t usage=n==4 ? value&0xffffu : value;
            uint32_t page=n==4 ? value>>16 : g.page;
            if (page!=g.page && (tag==0 || tag==1 || tag==2)) return -1;
            switch (tag) {
            case 0:l.usage=usage;l.has_usage++;break;
            case 1:l.first=usage;l.has_first=1;break;
            case 2:l.last=usage;l.has_last=1;break;
            default:break;
            }
        } else if (type==0) {
            if (tag==10) {
                if (depth==16 || !n) return -1;
                keyboard[depth]=(depth ? keyboard[depth-1] : 0) ||
                    (value==1 && g.page==1 && l.has_usage==1 && l.usage==6);
                depth++;
            } else if (tag==12) {
                if (!depth || n) return -1;
                depth--;
            } else if (tag==8) {
                if (!depth || !g.count || !g.size || g.size>32 || g.count>2048 ||
                    g.count*g.size>65536u-bits[g.id]) return -1;
                if (keyboard[depth-1] && g.page==7 && !(value&1)) {
                    if ((modifiers || keys) && k.report_id!=g.id) return -1;
                    k.report_id=g.id;
                    if (g.minimum!=0 || !l.has_first || !l.has_last || l.has_usage) return -1;
                    if ((value&2) && g.size==1 && g.count==8 &&
                        l.first==0xe0 && l.last==0xe7 && g.maximum==1) {
                        if (modifiers++) return -1;
                        k.modifier_bit=bits[g.id];
                    } else if (!(value&2) && g.size==8 && g.count==6 &&
                               l.first==0 && l.last>=3 && l.last<=255 && g.maximum==l.last) {
                        if (keys++) return -1;
                        k.keys_bit=bits[g.id];
                        k.maximum_key=(uint8_t)l.last;
                    } else return -1;
                    // Relative keyboard data cannot express held-key state.
                    if (value&4) return -1;
                }
                bits[g.id]+=g.count*g.size;
            }
            memset(&l,0,sizeof(l));
        }
    }
    if (depth || sp || modifiers!=1 || keys!=1 || (ids && !k.report_id)) return -1;
    k.uses_report_ids=(uint8_t)ids;
    k.report_bytes=(bits[k.report_id]+7)/8;
    if (!k.report_bytes || k.report_bytes>1024) return -1;
    *out=k;
    return 0;
}
static uint8_t byte_at(const uint8_t *p,uint32_t bit)
{
    uint32_t byte=bit/8, shift=bit%8;
    uint32_t value=p[byte];
    if (shift) value|=(uint32_t)p[byte+1]<<8;
    return (uint8_t)(value>>shift);
}
int apple_mtp_boot_keyboard_decode(const struct apple_mtp_boot_keyboard *k,
                                  const uint8_t *wire,size_t size,uint8_t boot[8])
{
    if (!k || !wire || !boot || !size || !k->report_bytes || k->report_bytes>1024) return -1;
    if (k->uses_report_ids) {
        if (*wire!=k->report_id) return 1;
        wire++;size--;
    }
    if (size!=k->report_bytes || k->modifier_bit>size*8-8 ||
        size<6 || k->keys_bit>size*8-48) return -1;
    uint8_t result[8]={byte_at(wire,k->modifier_bit),0};
    for (unsigned i=0;i<6;i++) {
        result[i+2]=byte_at(wire,k->keys_bit+8*i);
        if (result[i+2]>k->maximum_key) return -1;
    }
    memcpy(boot,result,sizeof(result));
    return 0;
}

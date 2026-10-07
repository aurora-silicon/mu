/* SPDX-License-Identifier: MIT */
/* Exact native-FIQ kernel profiles, shared by Mu and x1n1. All guards must
 * pass before text mutation. 26300 evidence: installer-26h2/
 * native-fiq-profile-evidence.json, Microsoft public PDB, OS == WinPE.
 * New profile is built/offline verified only until hardware qualification.
 */
#ifndef WINDOWS_NATIVE_KERNEL_PROFILE_H
#define WINDOWS_NATIVE_KERNEL_PROFILE_H
#include <stdint.h>
#define WIN_FIQ_IDLE_POLICY 0x49464d31UL
#define WIN_DAIF_SET2 0xd50342dfu
#define WIN_DAIF_CLR2 0xd50342ffu
#define WIN_DAIF_SET3 0xd50343dfu
#define WIN_DAIF_CLR3 0xd50343ffu
struct win_native_mask_edit { uint32_t rva, size; uint64_t old, replacement; };
struct win_native_kernel_profile {
    uint32_t timestamp, checksum, image_size, vectors_rva, fiq_rva, fiq_slot_rva;
    uint32_t idle_slot_rva, wfi_rva, prepare_rva, idle_rva, idle_size;
    uint32_t set2_count, clr2_count, edit_count;
    uint64_t wfi_hash, prepare_hash, idle_original_hash, idle_patched_hash;
    const struct win_native_mask_edit *edits;
};
static const struct win_native_mask_edit win_native_edits_26100[] = {
    /* AIC software priorities: use saved IRQL for the existing replay path. */
    {0x288d00, 4, 0x38686921ULL, 0x2a1303e1ULL},
    {0x605930, 4, 0xd5034adfULL, 0xd5034bdfULL},
    {0x60d17c, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x60d28c, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x60d2a4, 4, 0xd5034adfULL, 0xd5034bdfULL},
    {0x60d488, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x26b298, 4, 0x321a0108ULL, 0x12197908ULL},
    {0x28a20c, 4, 0x321a0108ULL, 0x12197908ULL},
    {0x8d84d0, 4, 0x321a0108ULL, 0x12197908ULL},
    {0x2b2520, 4, 0x92805008ULL, 0x92805808ULL},
    {0x2b2550, 4, 0xd2805008ULL, 0xd2805808ULL},
    {0x2b2898, 8, 0xfffffd7fULL, 0xfffffd3fULL},
    {0x2b2974, 4, 0x92805008ULL, 0x92805808ULL},
    {0x426400, 4, 0x92805008ULL, 0x92805808ULL},
    {0x4e22c4, 4, 0x92805008ULL, 0x92805808ULL},
    {0x4e24e8, 4, 0x92805008ULL, 0x92805808ULL},
};
static const struct win_native_mask_edit win_native_edits_26300[] = {
    /* 26300 ACPI requests registered GSIV ranges for interrupt model 4.
     * HalpInterruptQueryGicInfo incorrectly accepts only models 2..3 despite
     * that caller. Permit 2..4: the remaining body reads generic registered
     * StandardPin descriptors, without accessing any GIC hardware. Native AIC
     * callbacks and interrupt dispatch remain unchanged. Guard the CMP and
     * following conditional branch together. Exact captured-code host tests:
     * installer-26h2/acpi-debug/test-query-native.c. Hardware unqualified.
     */
    {0x6777c8, 8, 0x540000697100051fULL, 0x540000697100091fULL},
    /* AIC software priorities: use saved IRQL for the existing replay path. */
    {0x28acb0, 4, 0x38686921ULL, 0x2a1303e1ULL},
    {0x637934, 4, 0xd5034adfULL, 0xd5034bdfULL},
    {0x637dbc, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x637edc, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x637ef4, 4, 0xd5034adfULL, 0xd5034bdfULL},
    {0x6380e4, 4, 0xd5034affULL, 0xd5034bffULL},
    {0x2691ec, 4, 0x321a0108ULL, 0x12197908ULL},
    {0x90a7e0, 4, 0x321a0108ULL, 0x12197908ULL},
    {0x2b6058, 4, 0x92805008ULL, 0x92805808ULL},
    {0x2b6078, 4, 0xd2805008ULL, 0xd2805808ULL},
    {0x2b61c8, 4, 0xd29fafe8ULL, 0xd29fa7e8ULL},
    {0x2b6a48, 4, 0x92805008ULL, 0x92805808ULL},
    {0x454310, 4, 0x92805008ULL, 0x92805808ULL},
    {0x510aec, 4, 0x92805008ULL, 0x92805808ULL},
    {0x510d10, 4, 0x92805008ULL, 0x92805808ULL},
};
static const struct win_native_kernel_profile win_native_profiles[] = {
    {0x1149c62d, 0xac7a5a, 0x1249000, 0x604800, 0x4e2140, 0xe1f100,
     0xe1f108, 0x451ae0, 0x457a18, 0x4dc630, 0x1b0, 369, 362, 16,
     0x45f117ccffa08ea2ULL, 0xd53730ffc76d3cd4ULL,
     0x1703eff4772f4827ULL, 0x7c6f86f698f89064ULL, win_native_edits_26100},
    {0x409ce488, 0xb11d3b, 0x124a000, 0x636800, 0x510a30, 0xe21180,
     0xe21188, 0x47fe30, 0x485fc8, 0x50b540, 0x170, 406, 394, 16,
     0x2c8eab3b91cbe457ULL, 0x834dc468ee44849bULL,
     0xfde2af8c96c74178ULL, 0xfd66cccf6951faffULL, win_native_edits_26300},
};
static inline uint32_t win_native_profile_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/* Only the first page is read here; both consumers validate the PE envelope
 * and loaded RAM extent before calling. Content fingerprints follow later. */
static inline const struct win_native_kernel_profile *win_native_profile_find(
    const unsigned char *header, uint32_t size, uint32_t fiq, uint32_t slot, uint32_t vectors)
{
    uint32_t pe=win_native_profile_u32(header+0x3c);
    if(pe<0x40 || pe>0xf88 || win_native_profile_u32(header+pe)!=0x4550)return 0;
    uint32_t timestamp=win_native_profile_u32(header+pe+8);
    uint32_t checksum=win_native_profile_u32(header+pe+24+64);
    for(unsigned i=0;i<sizeof(win_native_profiles)/sizeof(win_native_profiles[0]);i++){
        const struct win_native_kernel_profile *p=&win_native_profiles[i];
        if(p->timestamp==timestamp && p->checksum==checksum && p->image_size==size &&
           p->fiq_rva==fiq && p->fiq_slot_rva==slot && p->vectors_rva==vectors)return p;
    }
    return 0;
}
#endif

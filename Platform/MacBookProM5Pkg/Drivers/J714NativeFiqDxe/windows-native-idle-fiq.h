/* SPDX-License-Identifier: MIT */
/* Exact 26100 kernel KiIdleLoop, GUID9B3C83B580575E719D5592619E29C053 age1.
 * Checked function fingerprint excludes its relocated literal at +0x1b0. */
#ifndef WINDOWS_NATIVE_IDLE_FIQ_H
#define WINDOWS_NATIVE_IDLE_FIQ_H
#define WIN_FIQ_IDLE_POLICY 0x49464d31UL
#define WIN_FIQ_IDLE_RVA 0x4dc630u
#define WIN_FIQ_IDLE_SIZE 0x1b0u
#define WIN_FIQ_IDLE_ORIGINAL_HASH 0x1703eff4772f4827ULL
#define WIN_FIQ_IDLE_PATCHED_HASH 0x7c6f86f698f89064ULL
static const unsigned win_fiq_idle_sites[9] = {0x4dc66c,0x4dc678,0x4dc690,0x4dc6a4,0x4dc6dc,0x4dc6ec,0x4dc704,0x4dc750,0x4dc764};
static unsigned long long win_fiq_idle_hash(const unsigned char *p)
{
    unsigned long long h=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<WIN_FIQ_IDLE_SIZE;i++) h=(h^p[i])*0x100000001b3ULL;
    return h;
}
#endif

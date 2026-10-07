/* Copyright (c) 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */
/*
 * AArch64 instruction encoders for the Windows timer-FIQ KiFIQException patch.
 *
 * RECONSTRUCTED for the x1n1 native-FIQ port. The upstream m1n1-aurora tree
 * keeps these encoders in this same header and cross-checks them against the
 * system assembler with tools/test_windows_fiq_patch_encoders.py. That header
 * and that test are NOT present in the ref/m1n1-aurora-main checkout on this
 * workspace, so this file was rebuilt from two authoritative sources:
 *
 *   1. hv_windows_patch.c::hv_wp_verify_replacement(), which decodes and asserts
 *      every one of the 11 emitted dwords against the resolved targets. Each
 *      encoder here is the exact inverse of a check in that function.
 *   2. The instruction layout comment in hv_windows_patch.h.
 *
 * Every encoder in this file was additionally cross-checked byte-for-byte
 * against llvm-mc (see test_windows_fiq_patch_encoders.c). Pure, freestanding,
 * no m1n1 dependencies, so it compiles both into the hypervisor and into the
 * host oracle test.
 *
 * Patch body (11 dwords), written at func+4 (patch_pc), leaving pacibsp intact:
 *
 *   patch_pc+0x00  adrp  x8, <cb_page>       ; [0]
 *   patch_pc+0x04  ldr   x8, [x8, <cb_off>]  ; [1]
 *   patch_pc+0x08  cbz   x8, patch_pc+0x20   ; [2] -> fallback bugcheck
 *   patch_pc+0x0c  str   x30, [sp,#-0x10]!   ; [3]
 *   patch_pc+0x10  blr   x8                  ; [4]
 *   patch_pc+0x14  ldr   x30, [sp],#0x10     ; [5]
 *   patch_pc+0x18  autibsp                   ; [6]
 *   patch_pc+0x1c  ret                       ; [7]
 *   patch_pc+0x20  mov   w0, #0x3d           ; [8]
 *   patch_pc+0x24  mov   x1, #0              ; [9]
 *   patch_pc+0x28  b     <bugcheck>          ; [10]
 */
#ifndef HV_WINDOWS_PATCH_ENC_H
#define HV_WINDOWS_PATCH_ENC_H

#include <stdint.h>

/* LDR (immediate, unsigned offset), 64-bit: Rt <- [Rn, #off].
 * size=11, opc=01 -> base 0xf9400000. imm12 = off>>3 (off is 8-byte aligned). */
static inline uint32_t wp_ldr_uo(unsigned rt, unsigned rn, uint32_t off)
{
    uint32_t imm12 = (off >> 3) & 0xfffu;
    return 0xf9400000u | (imm12 << 10) | ((rn & 0x1fu) << 5) | (rt & 0x1fu);
}

/* STR (immediate, pre-index), 64-bit: [Rn, #imm]! <- Rt.
 * size=11, opc=00, pre-index -> base 0xf8000c00. imm9 signed. */
static inline uint32_t wp_str_pre(unsigned rt, unsigned rn, int32_t imm)
{
    uint32_t imm9 = (uint32_t)(imm & 0x1ff);
    return 0xf8000c00u | (imm9 << 12) | ((rn & 0x1fu) << 5) | (rt & 0x1fu);
}

/* LDR (immediate, post-index), 64-bit: Rt <- [Rn], #imm.
 * size=11, opc=01, post-index -> base 0xf8400400. imm9 signed. */
static inline uint32_t wp_ldr_post(unsigned rt, unsigned rn, int32_t imm)
{
    uint32_t imm9 = (uint32_t)(imm & 0x1ff);
    return 0xf8400400u | (imm9 << 12) | ((rn & 0x1fu) << 5) | (rt & 0x1fu);
}

/* CBZ (64-bit): if Xt == 0 branch to target. sf=1 -> base 0xb4000000.
 * imm19 = (target - pc) >> 2, signed. */
static inline uint32_t wp_cbz_x(unsigned rt, uint64_t target, uint64_t pc)
{
    int64_t rel = (int64_t)(target - pc) >> 2;
    uint32_t imm19 = (uint32_t)(rel & 0x7ffff);
    return 0xb4000000u | (imm19 << 5) | (rt & 0x1fu);
}

/* ADRP: Rd <- PC-page + (imm21 << 12). base 0x90000000.
 * imm21 = (target_page - pc_page) >> 12, signed, split immlo[30:29]/immhi[23:5]. */
static inline uint32_t wp_adrp(unsigned rd, uint64_t target_page, uint64_t pc)
{
    int64_t rel = (int64_t)((target_page & ~0xfffULL) - (pc & ~0xfffULL)) >> 12;
    uint32_t imm21 = (uint32_t)(rel & 0x1fffff);
    uint32_t immlo = imm21 & 0x3u;
    uint32_t immhi = (imm21 >> 2) & 0x7ffffu;
    return 0x90000000u | (immlo << 29) | (immhi << 5) | (rd & 0x1fu);
}

/* B (unconditional): branch to target. base 0x14000000.
 * imm26 = (target - pc) >> 2, signed. */
static inline uint32_t wp_b(uint64_t pc, uint64_t target)
{
    int64_t rel = (int64_t)(target - pc) >> 2;
    uint32_t imm26 = (uint32_t)(rel & 0x3ffffff);
    return 0x14000000u | imm26;
}

/* Fixed dwords with no PC-relative fields. */
#define WP_INSN_BLR_X8   0xd63f0100u /* blr x8            */
#define WP_INSN_AUTIBSP  0xd50323ffu /* autibsp (B key)   */
#define WP_INSN_RET      0xd65f03c0u /* ret               */
#define WP_INSN_MOV_W0_3D 0x528007a0u /* mov w0, #0x3d    */
#define WP_INSN_MOV_X1_0 0xd2800001u /* mov x1, #0        */

/*
 * Build the 11-dword replacement into out[0..10].
 *   patch_pc  = KiFIQException + 4 (address of out[0]).
 *   slot      = callback QWORD VA.
 *   bugcheck  = decoded tail-call target of the original `bl`.
 */
static inline void hv_wp_build_replacement_into(uint32_t *out, uint64_t patch_pc, uint64_t slot,
                                                uint64_t bugcheck)
{
    out[0] = wp_adrp(8, slot & ~0xfffULL, patch_pc);
    out[1] = wp_ldr_uo(8, 8, (uint32_t)(slot & 0xfffULL));
    out[2] = wp_cbz_x(8, patch_pc + 0x20, patch_pc + 0x08);
    out[3] = wp_str_pre(30, 31, -0x10);
    out[4] = WP_INSN_BLR_X8;
    out[5] = wp_ldr_post(30, 31, 0x10);
    out[6] = WP_INSN_AUTIBSP;
    out[7] = WP_INSN_RET;
    out[8] = WP_INSN_MOV_W0_3D;
    out[9] = WP_INSN_MOV_X1_0;
    out[10] = wp_b(patch_pc + 0x28, bugcheck);
}

#endif /* HV_WINDOWS_PATCH_ENC_H */

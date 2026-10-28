/* Copyright (c) 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */
/*
 * Build-independent location of the Windows timer-FIQ patch targets.
 *
 * The original patch pinned three absolute VAs to ntoskrnl 26200.8037:
 * KiFIQException, its bugcheck tail-call target, and a hand-picked callback
 * slot. A Windows Update moves all three, and the guest is now on 26200.9168,
 * where the pinned KiFIQException VA lands in an unrelated function.
 *
 * Nothing here is pinned. Every target the patch needs is PC-relative, so the
 * whole patch can be built from offsets inside the image and never needs to
 * know where that image is mapped:
 *
 *   - KiFIQException is found by its body, which is build-invariant apart from
 *     one `bl`. All four FIQ vector slots call it, so it is also cross-checked
 *     against the vector table when the image base is known.
 *   - The bugcheck fallback target is decoded from that `bl` rather than
 *     pinned. It is NOT the exported KeBugCheckEx -- on both known builds the
 *     FIQ path tail-calls an internal wrapper (8037: 0x25ca90 vs export
 *     0x25d7c0), so resolving the export here would send the fallback to the
 *     wrong function.
 *   - The callback slot goes in the tail gap of a writable section: the bytes
 *     between the section's VirtualSize and the next section's RVA. That space
 *     is mapped and writable but owned by nothing, which makes it provably
 *     dead -- unlike a zero .data QWORD, which may simply be a variable that
 *     has not been initialised yet.
 *
 * Pure `static inline` with no m1n1 dependencies, so the same source compiles
 * into the hypervisor and into the host tests that run it against real
 * ntoskrnl images (tools/test_windows_fiq_resolve.c).
 */
#ifndef HV_WINDOWS_FIQ_RESOLVE_H
#define HV_WINDOWS_FIQ_RESOLVE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * KiFIQException, stock. Ten build-invariant dwords plus the `bl` at +0x24,
 * which is the only field that moves. Windows zeroes the argument registers
 * and bugchecks 0x3d (INTERRUPT_EXCEPTION_NOT_HANDLED) on any raw FIQ.
 */
#define HV_WP_BODY_DWORDS 11u
#define HV_WP_BL_INDEX    9u
#define HV_WP_BRK_INDEX   10u

static const uint32_t hv_wp_body[HV_WP_BODY_DWORDS] = {
    0xd503237f, /* pacibsp                  */
    0xa9bf7bfd, /* stp x29,x30,[sp,#-0x10]! */
    0x910003fd, /* mov x29, sp              */
    0xd2800005, /* mov x5, #0               */
    0xd2800004, /* mov x4, #0               */
    0xd2800003, /* mov x3, #0               */
    0xd2800002, /* mov x2, #0               */
    0xd2800001, /* mov x1, #0               */
    0x528007a0, /* mov w0, #0x3d            */
    0,          /* bl <bugcheck>  -- decoded, never pinned */
    0xd43e0000, /* brk #0xf000              */
};

/*
 * First 16 bytes of the Windows ARM64 exception vector table. Byte-identical
 * on 26200.8037 and 26200.9168, and the same signature drivers/AicHal uses.
 */
static const uint8_t hv_wp_vector_signature[16] = {
    0x12, 0x41, 0x38, 0xd5, 0x5f, 0xee, 0x7c, 0x92, 0xff, 0xc3, 0x0d, 0xd1, 0x92, 0xd0, 0x38, 0xd5,
};

struct hv_wp_resolved {
    uint64_t func;     /* KiFIQException                                */
    uint64_t bugcheck; /* decoded tail-call target, not the export      */
    uint64_t slot;     /* callback QWORD, in a writable section tail gap */
    uint64_t image_base;
    uint32_t image_size;
    uint32_t vectors_rva; /* 0 when the vector cross-check was not run     */
};

/* A window of memory: mem[0] lives at address base. */
struct hv_wp_view {
    const uint8_t *mem;
    uint64_t base;
    uint64_t size;
};

static inline uint32_t hv_wp_rd32(const struct hv_wp_view *v, uint64_t addr)
{
    const uint8_t *p = v->mem + (addr - v->base);

    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t hv_wp_rd16(const struct hv_wp_view *v, uint64_t addr)
{
    const uint8_t *p = v->mem + (addr - v->base);

    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline bool hv_wp_in(const struct hv_wp_view *v, uint64_t addr, uint64_t len)
{
    return addr >= v->base && addr - v->base <= v->size && len <= v->size - (addr - v->base);
}

static inline bool hv_wp_is_bl(uint32_t insn)
{
    return (insn & 0xfc000000u) == 0x94000000u;
}

static inline uint64_t hv_wp_bl_target(uint64_t pc, uint32_t insn)
{
    int32_t imm = (int32_t)(insn & 0x03ffffffu);

    if (imm & 0x02000000)
        imm -= 0x04000000;
    return pc + (int64_t)imm * 4;
}

/*
 * Does a stock KiFIQException body start at `addr`? Index HV_WP_BL_INDEX is
 * only required to be some `bl`; its target is an output, not a constant.
 */
static inline bool hv_wp_body_matches(const struct hv_wp_view *v, uint64_t addr)
{
    unsigned i;

    if (!hv_wp_in(v, addr, HV_WP_BODY_DWORDS * 4u))
        return false;
    for (i = 0; i < HV_WP_BODY_DWORDS; i++) {
        uint32_t insn = hv_wp_rd32(v, addr + i * 4u);

        if (i == HV_WP_BL_INDEX) {
            if (!hv_wp_is_bl(insn))
                return false;
            continue;
        }
        if (insn != hv_wp_body[i])
            return false;
    }
    return true;
}

/*
 * Scan for the one function whose body matches. Rejects a second match: two
 * candidates means the shape is no longer identifying and the patch must not
 * pick between them.
 */
static inline bool hv_wp_find_function(const struct hv_wp_view *v, uint64_t *out)
{
    uint64_t found = 0;
    uint64_t addr;

    if (v->size < HV_WP_BODY_DWORDS * 4u)
        return false;
    for (addr = v->base; addr <= v->base + v->size - HV_WP_BODY_DWORDS * 4u; addr += 4u) {
        /* Cheap reject on the first dword before the full compare. */
        if (hv_wp_rd32(v, addr) != hv_wp_body[0])
            continue;
        if (!hv_wp_body_matches(v, addr))
            continue;
        if (found)
            return false;
        found = addr;
    }
    if (!found)
        return false;
    *out = found;
    return true;
}

/*
 * Walk back page by page from a known-good address inside the image until the
 * PE header appears, and require that address to fall inside SizeOfImage.
 */
static inline bool hv_wp_find_image_base(const struct hv_wp_view *v, uint64_t inside,
                                         uint64_t max_scan, uint64_t *base_out, uint32_t *size_out)
{
    uint64_t page = inside & ~0xfffULL;
    uint64_t scanned;

    for (scanned = 0; scanned <= max_scan; scanned += 0x1000u) {
        uint64_t cand = page - scanned;
        uint32_t pe, image_size;

        if (cand < v->base)
            break;
        if (!hv_wp_in(v, cand, 0x1000u))
            continue;
        if (hv_wp_rd16(v, cand) != 0x5a4du) /* MZ */
            continue;
        pe = hv_wp_rd32(v, cand + 0x3c);
        if (pe < 0x40u || pe > 0x1000u)
            continue;
        if (!hv_wp_in(v, cand + pe, 0x78u))
            continue;
        if (hv_wp_rd32(v, cand + pe) != 0x00004550u) /* PE\0\0 */
            continue;
        if (hv_wp_rd16(v, cand + pe + 4) != 0xaa64u) /* ARM64 */
            continue;
        if (hv_wp_rd16(v, cand + pe + 24) != 0x020bu) /* PE32+ */
            continue;
        image_size = hv_wp_rd32(v, cand + pe + 24 + 56);
        if (image_size < 0x1000u || inside - cand >= image_size)
            continue;
        *base_out = cand;
        *size_out = image_size;
        return true;
    }
    return false;
}

/*
 * Pick the callback slot: the tail of a writable, non-executable section --
 * the bytes between its VirtualSize and the end of the page that VirtualSize
 * lands in.
 *
 * The slot must stay inside that final page. A gap that runs past the page
 * boundary is not free space, it is unmapped address range, and a gap that
 * another section starts inside is that section's, not padding. Both were real
 * mistakes: without the page bound this picks the byte where .rsrc begins.
 *
 * Deterministic, because the guest-side HAL has to derive the same address:
 * largest usable tail wins, ties broken by lowest RVA.
 */
#define HV_WP_SLOT_MIN_TAIL 16u

static inline bool hv_wp_find_slot(const struct hv_wp_view *v, uint64_t image_base,
                                   uint64_t *slot_out)
{
    uint32_t pe = hv_wp_rd32(v, image_base + 0x3c);
    uint32_t opt = pe + 24u;
    uint32_t opt_size = hv_wp_rd16(v, image_base + pe + 20);
    uint32_t count = hv_wp_rd16(v, image_base + pe + 6);
    uint64_t table = image_base + opt + opt_size;
    uint32_t best_tail = 0;
    uint32_t best_rva = 0;
    uint64_t best = 0;
    uint32_t i;

    if (count == 0u || count > 96u)
        return false;
    for (i = 0; i < count; i++) {
        uint64_t entry = table + (uint64_t)i * 40u;
        uint32_t vsize, rva, chars, end, page_end, slot, tail;
        uint32_t j;
        bool clear = true;
        bool overlapped = false;

        if (!hv_wp_in(v, entry, 40u))
            return false;
        vsize = hv_wp_rd32(v, entry + 8);
        rva = hv_wp_rd32(v, entry + 12);
        chars = hv_wp_rd32(v, entry + 36);
        /* writable, not executable */
        if (!(chars & 0x80000000u) || (chars & 0x20000000u))
            continue;
        if (vsize == 0u)
            continue;
        end = rva + vsize;
        page_end = (end + 0xfffu) & ~0xfffu;
        slot = (end + 7u) & ~7u;
        if (slot + 8u > page_end)
            continue;
        tail = page_end - slot;
        /* No other section may claim any part of this page's tail. */
        for (j = 0; j < count; j++) {
            uint32_t other;

            if (j == i)
                continue;
            other = hv_wp_rd32(v, table + (uint64_t)j * 40u + 12);
            if (other >= end && other < page_end)
                overlapped = true;
        }
        if (overlapped)
            continue;
        if (!hv_wp_in(v, image_base + slot, tail))
            continue;
        /* Every byte of the tail must be clear, or it is not padding. */
        for (j = 0; j < tail; j += 4u) {
            if (hv_wp_rd32(v, image_base + slot + j) != 0u) {
                clear = false;
                break;
            }
        }
        if (!clear)
            continue;
        if (tail < HV_WP_SLOT_MIN_TAIL)
            continue;
        if (tail > best_tail || (tail == best_tail && rva < best_rva)) {
            best_tail = tail;
            best_rva = rva;
            best = image_base + slot;
        }
    }
    if (!best)
        return false;
    *slot_out = best;
    return true;
}

/*
 * Cross-check: every FIQ vector slot that calls a function must call the one
 * we found. Costs nothing and catches a body match that is not the real
 * handler. Skipped when the vector table is not present in the view.
 */
static inline bool hv_wp_vector_confirms(const struct hv_wp_view *v, uint64_t image_base,
                                         uint32_t image_size, uint64_t func,
                                         uint32_t *vectors_rva_out)
{
    uint64_t addr;
    uint64_t limit;

    if (image_size < 0x1000u)
        return false;
    limit = image_base + image_size - sizeof(hv_wp_vector_signature);
    for (addr = image_base; addr <= limit; addr += 4u) {
        unsigned i;
        bool hit = true;
        unsigned slot;
        bool saw = false;

        if (!hv_wp_in(v, addr, 0x800u))
            continue;
        for (i = 0; i < sizeof(hv_wp_vector_signature); i++) {
            if (v->mem[(addr - v->base) + i] != hv_wp_vector_signature[i]) {
                hit = false;
                break;
            }
        }
        if (!hit)
            continue;
        /* FIQ entries sit at +0x100, +0x300, +0x500, +0x700. */
        for (slot = 0; slot < 4u; slot++) {
            uint64_t entry = addr + 0x100u + (uint64_t)slot * 0x200u;
            uint32_t off;

            for (off = 0; off < 0x80u; off += 4u) {
                uint32_t insn = hv_wp_rd32(v, entry + off);

                if (!hv_wp_is_bl(insn))
                    continue;
                if (hv_wp_bl_target(entry + off, insn) == func)
                    saw = true;
            }
        }
        if (saw) {
            *vectors_rva_out = (uint32_t)(addr - image_base);
            return true;
        }
    }
    return false;
}

/*
 * Resolve everything the patch needs from a view of loaded memory.
 * `require_vectors` cross-checks against the exception vector table; leave it
 * off when only a partial window is visible.
 */
static inline bool hv_wp_resolve(const struct hv_wp_view *v, bool require_vectors,
                                 struct hv_wp_resolved *out)
{
    struct hv_wp_resolved r = {0, 0, 0, 0, 0, 0};
    uint32_t bl;

    if (!hv_wp_find_function(v, &r.func))
        return false;
    bl = hv_wp_rd32(v, r.func + HV_WP_BL_INDEX * 4u);
    r.bugcheck = hv_wp_bl_target(r.func + HV_WP_BL_INDEX * 4u, bl);
    if (!hv_wp_find_image_base(v, r.func, 0x01000000u, &r.image_base, &r.image_size))
        return false;
    if (!hv_wp_find_slot(v, r.image_base, &r.slot))
        return false;
    if (require_vectors &&
        !hv_wp_vector_confirms(v, r.image_base, r.image_size, r.func, &r.vectors_rva))
        return false;
    *out = r;
    return true;
}

#endif /* HV_WINDOWS_FIQ_RESOLVE_H */

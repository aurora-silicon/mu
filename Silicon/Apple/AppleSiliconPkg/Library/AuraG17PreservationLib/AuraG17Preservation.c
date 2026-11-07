/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IndustryStandard/AuraG17Preservation.h>

uint32_t aura_g17_u32(const void *p)
{
    const uint8_t *b = p;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 |
           (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
uint64_t aura_g17_u64(const void *p)
{
    const uint8_t *b = p;
    return aura_g17_u32(b) | (uint64_t)aura_g17_u32(b + 4) << 32;
}
static int adt_node_bounds(const uint8_t *b, size_t size, size_t *off, unsigned depth)
{
    uint32_t props, children, length;
    unsigned i;
    if (depth > 32 || *off > size || size - *off < 8) return 0;
    props = aura_g17_u32(b + *off); children = aura_g17_u32(b + *off + 4); *off += 8;
    if (props > 4096 || children > 4096) return 0;
    for (i = 0; i < props; ++i) {
        if (size - *off < 36) return 0;
        length = aura_g17_u32(b + *off + 32) & 0xffffff; *off += 36;
        length = (length + 3) & ~3u;
        if (length > size - *off) return 0;
        *off += length;
    }
    for (i = 0; i < children; ++i)
        if (!adt_node_bounds(b, size, off, depth + 1)) return 0;
    return 1;
}
int aura_g17_adt_bounds(const void *blob, size_t size, size_t *parsed_size)
{
    size_t parsed = 0;
    if (!blob || !size || size > 0x400000 || !adt_node_bounds(blob, size, &parsed, 0)) return 0;
    if (parsed_size) *parsed_size = parsed;
    return 1;
}
void aura_g17_region(const void *record, unsigned index, struct aura_g17_region *out)
{
    const uint8_t *p = (const uint8_t *)record + 112 + 24 * index;
    out->tag = aura_g17_u32(p);
    out->classification = aura_g17_u32(p + 4);
    out->base = aura_g17_u64(p + 8);
    out->size = aura_g17_u64(p + 16);
}
static int bytes_equal(const void *a, const void *b, size_t length)
{
    const uint8_t *x = a, *y = b;
    size_t i;
    for (i = 0; i < length; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}
static uint32_t record_crc(const uint8_t *p)
{
    uint32_t crc = 0xffffffffu;
    unsigned i, bit;
    for (i = 0; i < AURA_G17_BYTES; ++i) {
        crc ^= (i >= 104 && i < 108) ? 0 : p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
int aura_g17_observe(aura_g17_property property, void *context,
                     uint64_t guest_base, uint64_t guest_size,
                     uint64_t dram_size, struct aura_g17_observation *a)
{
    static const char *bases[6] = { "gfx-handoff-base", "gfx-shared-l2-region-base",
        "gfx-shared-region-base", "gpu-region-base", "gfx-data-base", "gfx1-data-base" };
    static const char *sizes[6] = { "gfx-handoff-size", "gfx-shared-l2-region-size",
        "gfx-shared-region-size", "gpu-region-size", "gfx-data-size", "gfx1-data-size" };
    static const char *asc[2] = { "/arm-io/gfx-asc", "/arm-io/gfx1-asc" };
    static const char model[] = "Mac17,3";
    static const char segments[] = "__TEXT;__DATA";
    const uint8_t *p;
    size_t length;
    unsigned i;
    uint64_t dram_base = guest_base & ~0xffffffffULL;
    if (!property || !a || !guest_size || !dram_size ||
        guest_size > ~(uint64_t)0 - guest_base ||
        dram_size > ~(uint64_t)0 - dram_base) return 0;
    for (i = 0; i < sizeof(*a); ++i) ((uint8_t *)a)[i] = 0;
    a->guest_base = guest_base;
    a->guest_end = guest_base + guest_size;
    a->dram_base = dram_base;
    a->dram_end = dram_base + dram_size;
    p = property(context, "/chosen", "chip-id", &length);
    if (!p || length != 4) return 0;
    a->chip = aura_g17_u32(p);
    p = property(context, "/chosen", "board-id", &length);
    if (!p || length != 4) return 0;
    a->board = aura_g17_u32(p);
    p = property(context, "/", "model", &length);
    if (!p || length != sizeof(model) || !bytes_equal(p, model, length)) return 0;
    for (i = 0; i < sizeof(model); ++i) a->model[i] = (char)p[i];
    for (i = 0; i < 6; ++i) {
        p = property(context, "/arm-io/sgx", bases[i], &length);
        if (!p || length != 8) return 0;
        a->regions[i].base = aura_g17_u64(p);
        p = property(context, "/arm-io/sgx", sizes[i], &length);
        if (!p || length != 8) return 0;
        a->regions[i].size = aura_g17_u64(p);
    }
    for (i = 0; i < 2; ++i) {
        p = property(context, asc[i], "segment-names", &length);
        if (!p || length != sizeof(segments) || !bytes_equal(p, segments, length)) return 0;
        p = property(context, asc[i], "segment-ranges", &length);
        if (!p || length != 64 ||
            aura_g17_u64(p + 32) != a->regions[4 + i].base ||
            aura_g17_u32(p + 56) != a->regions[4 + i].size) return 0;
        a->regions[6 + i].base = aura_g17_u64(p);
        a->regions[6 + i].size = aura_g17_u32(p + 24);
    }
    return 1;
}
int aura_g17_validate(const void *record, size_t length,
                      const struct aura_g17_observation *a)
{
    static const char firmware[32] = "M5-RTKit-3255.160.4.release";
    static const char model[16] = "Mac17,3";
    static const uint64_t exact[4] = { 0x134000, 0x130000, 0x4c000, 0x48000 };
    const uint8_t *p = record;
    struct aura_g17_region r, previous;
    uint64_t end;
    uint32_t classification;
    unsigned i, j;
    if (!p || !a || length != AURA_G17_BYTES) return 0;
    if (!bytes_equal(p, "AG17", 4) || aura_g17_u32(p + 4) != ((AURA_G17_BYTES << 16) | 1u) ||
        aura_g17_u32(p + 8) != 0x8142 || aura_g17_u32(p + 12) != 0x24 ||
        a->chip != 0x8142 || a->board != 0x24 ||
        aura_g17_u32(p + 16) != AURA_G17_REGIONS ||
        aura_g17_u32(p + 20) != 1 || aura_g17_u32(p + 108) != 0 ||
        !bytes_equal(p + 56, firmware, sizeof(firmware)) ||
        !bytes_equal(p + 88, model, sizeof(model)) ||
        !bytes_equal(a->model, model, sizeof(model)) ||
        aura_g17_u32(p + 104) != record_crc(p)) return 0;
    if (!a->dram_base || a->dram_base >= a->dram_end ||
        a->dram_end > (1ULL << 42) || a->guest_base < a->dram_base ||
        a->guest_base >= a->guest_end || a->guest_end > a->dram_end ||
        ((a->dram_base | a->dram_end | a->guest_base | a->guest_end) & 0x3fff) ||
        aura_g17_u64(p + 24) != a->guest_base ||
        aura_g17_u64(p + 32) != a->guest_end ||
        aura_g17_u64(p + 40) != a->dram_base ||
        aura_g17_u64(p + 48) != a->dram_end) return 0;
    for (i = 0; i < AURA_G17_REGIONS; ++i) {
        aura_g17_region(p, i, &r);
        if (r.tag != i + 1 || !r.size ||
            ((r.base | r.size) & 0x3fff) || r.base < a->dram_base ||
            r.base >= a->dram_end || r.size > a->dram_end - r.base ||
            r.base != a->regions[i].base || r.size != a->regions[i].size ||
            (i == 2 && r.size < 0x44000) ||
            (i >= 4 && r.size != exact[i - 4])) return 0;
        end = r.base + r.size;
        if (r.base >= a->guest_base && end <= a->guest_end) classification = 1;
        else if (end <= a->guest_base) classification = 2;
        else if (r.base >= a->guest_end) classification = 3;
        else return 0;
        if (r.classification != classification) return 0;
        for (j = 0; j < i; ++j) {
            aura_g17_region(p, j, &previous);
            if (r.base < previous.base + previous.size && previous.base < end)
                return 0;
        }
    }
    return 1;
}
int aura_g17_preserve(const void *record, size_t length,
                      const struct aura_g17_observation *actual,
                      const struct aura_g17_ops *ops)
{
    struct aura_g17_region region;
    unsigned i;
    if (!ops || !ops->check || !ops->reserve ||
        !aura_g17_validate(record, length, actual)) return 0;
    for (i = 0; i < AURA_G17_REGIONS; ++i) {
        aura_g17_region(record, i, &region);
        if (!ops->check(ops->context, &region)) return 0;
    }
    for (i = 0; i < AURA_G17_REGIONS; ++i) {
        aura_g17_region(record, i, &region);
        if (region.classification == 1 && !ops->reserve(ops->context, &region))
            return 0;
    }
    return 1;
}

int aura_g17_early(const void *record, size_t length,
                   const struct aura_g17_observation *actual,
                   const struct aura_g17_range *boot_copy,
                   const struct aura_g17_range *adt_copy,
                   const struct aura_g17_range *fd,
                   uint64_t uefi_size, const struct aura_g17_range *framebuffer)
{
    struct aura_g17_range writes[5];
    struct aura_g17_region region;
    uint64_t fd_end, top, end;
    unsigned i, j;
    if (!boot_copy || !adt_copy || !fd || !framebuffer ||
        !aura_g17_validate(record, length, actual) || !fd->size ||
        fd->base < actual->guest_base || fd->base >= actual->guest_end ||
        fd->size > actual->guest_end - fd->base || uefi_size < 0x20040)
        return 0;
    fd_end = fd->base + fd->size;
    /* Assembly uses inclusive tops, then adds one. Its near-FD fallback sets
     * the inclusive top to FdBase, so the final exclusive top is FdBase+1. */
    top = actual->guest_end - fd_end >= uefi_size ? actual->guest_end : fd->base + 1;
    if (top < actual->guest_base || uefi_size > top - actual->guest_base) return 0;
    writes[0] = *boot_copy; writes[1] = *adt_copy; writes[2] = *fd;
    writes[3].base = top - uefi_size; writes[3].size = uefi_size;
    writes[4] = *framebuffer;
    for (i = 0; i < 5; ++i) {
        if (i == 4 && writes[i].size == 0) continue;
        if (!writes[i].size || writes[i].base < actual->dram_base ||
            writes[i].base >= actual->dram_end ||
            writes[i].size > actual->dram_end - writes[i].base) return 0;
        if (i < 2 && (writes[i].base < actual->guest_base || writes[i].base >= actual->guest_end ||
            writes[i].size > actual->guest_end - writes[i].base)) return 0;
        end = writes[i].base + writes[i].size;
        for (j = 0; j < AURA_G17_REGIONS; ++j) {
            aura_g17_region(record, j, &region);
            if (writes[i].base < region.base + region.size && region.base < end) return 0;
        }
    }
    return 1;
}

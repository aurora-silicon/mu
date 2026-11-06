/* SPDX-License-Identifier: MIT */
#include <IndustryStandard/AuraN1Handoff.h>

static uint16_t u16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t u32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t u64(const unsigned char *p)
{
    return u32(p) | (uint64_t)u32(p + 4) << 32;
}

int aura_n1_validate(const void *record, size_t size, uint32_t chip,
                     uint32_t board, const struct aura_n1_reader *reader)
{
    static const uint64_t resources[6][2] = {
        {UINT64_C(0x1cb0000000), 0x200000}, {UINT64_C(0x488000000), 0x20000},
        {UINT64_C(0x488028000), 0x4000}, {UINT64_C(0x48f010000), 0x1000},
        {UINT64_C(0x39a000300), 4}, {UINT64_C(0xb80000000), 0x600000}
    };
    const unsigned char *b = record;
    uint32_t value, p1, p3, p4;
    unsigned i;

    if (!b || size != AURA_N1_HANDOFF_BYTES || !reader || !reader->read32 ||
        chip != 0x8142 || board != 0x24 || u32(b) != 0x4831574e ||
        u16(b + 4) != 1 || u16(b + 6) != AURA_N1_HANDOFF_BYTES ||
        u32(b + 8) != 7 || u32(b + 12) != chip || u32(b + 16) != board ||
        u32(b + 20) != 0x32354776 || u16(b + 24) != 0x100 ||
        u16(b + 26) != 0x101 || u16(b + 28) != 0x102 || u16(b + 30) != 0)
        return -1;
    for (i = 0; i < 6; i++)
        if (u64(b + 32 + i * 16) != resources[i][0] ||
            u64(b + 40 + i * 16) != resources[i][1])
            return -1;

#define READ(address, output) do { \
    if (reader->read32(reader->context, (address), &(output))) return -2; \
} while (0)
    /* Endpoint access is only safe after the port confirms link-up. */
    READ(UINT64_C(0x488028208), value);
    if (!(value & 1) || value == UINT32_MAX) return -3;
    READ(UINT64_C(0x1cb0000000), value);
    if (value != 0x100c106b) return -3;
    for (i = 0; i < 3; i++) {
        uint64_t ecam = UINT64_C(0x1cb0100000) + i * 0x1000;
        READ(ecam, value);
        if (i && value == UINT32_MAX) continue; /* Radios can appear after ROM. */
        if (i ? value != ((0x1901u + i) << 16 | 0x106b) :
                value != 0x1900106b && value != 0x1901106b)
            return -3;
        READ(ecam + 4, value);
        if (value & 4) return -3;
        if (!i && !(value & 2)) return -3;
    }
    READ(UINT64_C(0x1cb0100010), value);
    if (value != 0x80000000 && value != 0x80000004) return -3;
    if (value & 4) {
        READ(UINT64_C(0x1cb0100014), value);
        if (value) return -3;
    }
    READ(UINT64_C(0xb80008000), value);
    if (value) return -3;
    READ(UINT64_C(0x488000000), p1);
    READ(UINT64_C(0x488000008), p3);
    READ(UINT64_C(0x48800000c), p4);
    if (((p1 >> 24) & 15) != 14 || ((p3 >> 24) & 63) != 42 ||
        ((p3 >> 16) & 63) < 36 || ((p3 >> 16) & 63) > 42 ||
        (p3 & 0xffff) < 0x100 || (p3 & 0xffff) >= 0x300 ||
        !(p4 & 511) || (p4 & 511) > 256)
        return -3;
    READ(UINT64_C(0x488000200), value);
    if (value & 1) return -3;
    READ(UINT64_C(0x488001000), value);
    if (value) return -3;
    READ(UINT64_C(0x488001400), value);
    if (value) return -3;
#undef READ
    return 0;
}

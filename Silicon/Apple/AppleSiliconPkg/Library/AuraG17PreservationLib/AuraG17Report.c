/* SPDX-License-Identifier: MIT */
/* Independently authored from public J813 Linux/Aura hardware descriptions.
 * See Tools/g17-report-e2e/PROTOCOL.md for exact public pins and wire format.
 * The existing preservation validator retains its BSD-2-Clause-Patent license.
 */
#include <IndustryStandard/AuraG17Report.h>
#include <IndustryStandard/AuraG17Preservation.h>

static unsigned get16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static int equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    size_t i;
    for (i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}
static uint32_t payload_crc(const uint8_t *p)
{
    uint32_t c = 0xffffffffu;
    unsigned i, bit;
    for (i = 0; i < AURA_G17_REPORT_BYTES; ++i) {
        c ^= i >= 28 && i < 32 ? 0 : p[i];
        for (bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}
int aura_g17_report_validate(const void *payload, size_t length, const void *expected)
{
    static const uint64_t bases[6] = { 0x580000000ULL, 0x582600000ULL,
        0x582050000ULL, 0x582e00000ULL, 0x582850000ULL, 0x580e80000ULL };
    static const uint64_t sizes[6] = { 0x4000000, 0x88000, 8, 0x88000, 8, 0x30000 };
    static const unsigned roles[6] = { 0, 1, 1, 2, 2, 0 };
    static const uint32_t irqs[4] = { 1123, 1126, 1127, 1130 };
    static const uint32_t power[8] = { 0, 6, 0, 0x110, 0x10000, 1, 0, 0 };
    const uint8_t *p = payload, *r, *e;
    struct aura_g17_observation observation = { 0 };
    struct aura_g17_region region;
    unsigned i;
    if (!p || length != AURA_G17_REPORT_BYTES || !equal(p, "G17R", 4) ||
        get16(p + 4) != 1 || get16(p + 6) != 32 ||
        aura_g17_u32(p + 8) != AURA_G17_REPORT_BYTES || aura_g17_u32(p + 12) != 0 ||
        aura_g17_u32(p + 16) != AURA_G17_BYTES || aura_g17_u32(p + 20) != 6 ||
        aura_g17_u32(p + 24) != 4 || aura_g17_u32(p + 28) != payload_crc(p)) return 0;
    r = p + 32;
    if (expected && !equal(r, expected, AURA_G17_BYTES)) return 0;
    observation.chip = aura_g17_u32(r + 8); observation.board = aura_g17_u32(r + 12);
    observation.guest_base = aura_g17_u64(r + 24); observation.guest_end = aura_g17_u64(r + 32);
    observation.dram_base = aura_g17_u64(r + 40); observation.dram_end = aura_g17_u64(r + 48);
    for (i = 0; i < 16; ++i) observation.model[i] = (char)r[88 + i];
    for (i = 0; i < AURA_G17_REGIONS; ++i) {
        aura_g17_region(r, i, &region);
        observation.regions[i].base = region.base; observation.regions[i].size = region.size;
    }
    if (!aura_g17_validate(r, AURA_G17_BYTES, &observation)) return 0;
    for (i = 0; i < 6; ++i) {
        e = p + 336 + 32 * i;
        if (get16(e) != i + 1 || get16(e + 2) != roles[i] ||
            get16(e + 4) != (i ? 1u : 0u) || get16(e + 6) != 0 ||
            aura_g17_u64(e + 8) != bases[i] || aura_g17_u64(e + 16) != sizes[i] ||
            aura_g17_u64(e + 24) != 0) return 0;
        if (i && (bases[i] < bases[0] || sizes[i] > sizes[0] ||
                  bases[i] - bases[0] > sizes[0] - sizes[i])) return 0;
    }
    for (i = 0; i < 4; ++i) {
        e = p + 528 + 24 * i;
        if (get16(e) != i / 2 + 1 || get16(e + 2) != i % 2 + 1 ||
            aura_g17_u32(e + 4) != irqs[i] || aura_g17_u64(e + 8) != 0 ||
            aura_g17_u32(e + 16) != 1 || aura_g17_u32(e + 20) != 0) return 0;
    }
    e = p + 624;
    if (get16(e) != 42 || get16(e + 2) != 42 || get16(e + 4) != 14 || get16(e + 6) != 2 ||
        aura_g17_u32(e + 8) != 3 || aura_g17_u32(e + 12) != 4 ||
        aura_g17_u64(e + 16) != 0x40000 || aura_g17_u64(e + 24) != 0) return 0;
    for (i = 0; i < 8; ++i) if (aura_g17_u32(p + 656 + i * 4) != power[i]) return 0;
    return 1;
}
static const uint8_t header[36] = {
    'A','G','1','7', 0xd4,2,0,0, 1,0, 'A','U','R','O','R','A',
    'G','1','7','R','P','T','V','1', 1,0,0,0, 'A','U','R','A', 1,0,0,0
};
int aura_g17_report_table(const void *payload, size_t length, const void *expected,
                          void *table, size_t table_size)
{
    uint8_t *t = table;
    const uint8_t *p = payload;
    unsigned i, sum = 0;
    if (!t || table_size != AURA_G17_REPORT_TABLE_BYTES ||
        !aura_g17_report_validate(p, length, expected)) return 0;
    for (i = 0; i < sizeof(header); ++i) t[i] = header[i];
    for (i = 0; i < AURA_G17_REPORT_BYTES; ++i) t[36 + i] = p[i];
    for (i = 0; i < AURA_G17_REPORT_TABLE_BYTES; ++i) sum += t[i];
    t[9] = (uint8_t)(0u - sum);
    return 1;
}
int aura_g17_report_table_validate(const void *table, size_t length)
{
    const uint8_t *t = table;
    unsigned i, sum = 0;
    if (!t || length != AURA_G17_REPORT_TABLE_BYTES) return 0;
    for (i = 0; i < length; ++i) {
        sum += t[i];
        if (i < sizeof(header) && i != 9 && t[i] != header[i]) return 0;
    }
    return (sum & 255) == 0 && aura_g17_report_validate(t + 36, length - 36, NULL);
}

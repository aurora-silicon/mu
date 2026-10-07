/* Shared, read-only Mu/Windows RAM-bank handoff. Addresses are guest IPAs. */
#ifndef WINDOWS_GUEST_MEMORY_H
#define WINDOWS_GUEST_MEMORY_H
#include <stdint.h>
#define WIN_RAM_MAX_BANKS 128u
#define WIN_RAM_MAGIC 0x57524d3100000001ULL
#define WIN_RAM_INFO_OFFSET 0x1000u
#define WIN_RAM_MAGIC_OFFSET 0x0000u
#define WIN_RAM_COUNT_OFFSET 0x0008u
#define WIN_RAM_TOTAL_OFFSET 0x0010u
#define WIN_RAM_BANK_OFFSET 0x0100u
#define WIN_RAM_HIGH_BASE 0x10000000000ULL
struct win_ram_bank { uint64_t base, size; };
/* The bootstrap bank remains low and contiguous. Additional banks must be
 * sorted, disjoint, page aligned and outside every low peripheral aperture.
 * SPTM independently excludes host storage and firmware before publishing. */
static inline int win_ram_validate(const struct win_ram_bank *banks,
    uint32_t count, uint64_t bootstrap, uint64_t bootstrap_size, uint64_t total)
{
    uint64_t end=0, sum=0;
    if (!count || count>WIN_RAM_MAX_BANKS || banks[0].base!=bootstrap ||
        banks[0].size!=bootstrap_size) return 0;
    for (uint32_t i=0;i<count;i++) {
        uint64_t base=banks[i].base,size=banks[i].size;
        if (!size || ((base|size)&0x3fff) || base>=(1ULL<<44) ||
            size>(1ULL<<44)-base || (i && (base<end || base<WIN_RAM_HIGH_BASE)) ||
            size>UINT64_MAX-sum) return 0;
        end=base+size;sum+=size;
    }
    return sum==total;
}
#endif

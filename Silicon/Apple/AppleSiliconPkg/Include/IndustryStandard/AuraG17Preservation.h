/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef AURA_G17_PRESERVATION_H
#define AURA_G17_PRESERVATION_H
#include <stddef.h>
#include <stdint.h>
#define AURA_G17_PROPERTY "aura,windows-g17-preserve-v1"
#define AURA_G17_BYTES 304u
#define AURA_G17_REGIONS 8u
#define AURA_G17_HOB_GUID { 0x740cd16a, 0x0d8e, 0x4a29, { 0xbd, 0x4a, 0x1c, 0x30, 0xd7, 0xc1, 0x89, 0x41 } }
struct aura_g17_range { uint64_t base, size; };
struct aura_g17_observation {
    uint32_t chip, board;
    char model[16];
    uint64_t guest_base, guest_end, dram_base, dram_end;
    struct aura_g17_range regions[AURA_G17_REGIONS];
};
struct aura_g17_region { uint32_t tag, classification; uint64_t base, size; };
struct aura_g17_ops {
    void *context;
    int (*check)(void *, const struct aura_g17_region *);
    int (*reserve)(void *, const struct aura_g17_region *);
};
typedef const void *(*aura_g17_property)(void *, const char *, const char *, size_t *);
int aura_g17_observe(aura_g17_property property, void *context,
                     uint64_t guest_base, uint64_t guest_size,
                     uint64_t dram_size, struct aura_g17_observation *actual);
uint32_t aura_g17_u32(const void *p);
uint64_t aura_g17_u64(const void *p);
int aura_g17_adt_bounds(const void *blob, size_t size, size_t *parsed_size);
void aura_g17_region(const void *record, unsigned index, struct aura_g17_region *out);
int aura_g17_validate(const void *record, size_t length,
                      const struct aura_g17_observation *actual);
/* All checks complete before the first mutation; a failure never releases RAM. */
int aura_g17_preserve(const void *record, size_t length,
                      const struct aura_g17_observation *actual,
                      const struct aura_g17_ops *ops);
/* Mirrors the pre-PEI destinations and ModuleEntryPoint workspace calculation. */
int aura_g17_early(const void *record, size_t length,
                   const struct aura_g17_observation *actual,
                   const struct aura_g17_range *boot_copy,
                   const struct aura_g17_range *adt_copy,
                   const struct aura_g17_range *fd,
                   uint64_t uefi_size, const struct aura_g17_range *framebuffer);
#endif

/* SPDX-License-Identifier: MIT */
#ifndef AURA_N1_HANDOFF_H
#define AURA_N1_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#define AURA_N1_HANDOFF_BYTES 128u
#define AURA_N1_HANDOFF_PROPERTY "aura,windows-n1-v1"
#define AURA_N1_ACPI_TABLE_ID "AURA813N"
#define AURA_N1_READ_REQUEST UINT64_C(0x4e3152443332)
#define AURA_N1_READ_ACK UINT64_C(0x4e3141434b31)

/* Guarded EL2 census read; never falls back to direct guest MMIO. */
int AuraN1Read32(void *context, uint64_t address, uint32_t *value);

struct aura_n1_reader {
    void *context;
    int (*read32)(void *context, uint64_t address, uint32_t *value);
};

/* Zero authorizes publication. Every rejection withholds the whole table.
 * The caller has already obtained chip/board from the current ADT. */
int aura_n1_validate(const void *record, size_t size, uint32_t chip,
                     uint32_t board, const struct aura_n1_reader *reader);

#endif

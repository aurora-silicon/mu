/* SPDX-License-Identifier: MIT */
/* Public diagnostic ABI; none of these fields authorizes hardware access. */
#ifndef AURA_G17_REPORT_H
#define AURA_G17_REPORT_H
#include <stddef.h>
#include <stdint.h>
#define AURA_G17_REPORT_PROPERTY "aura,windows-g17-report-v1"
#define AURA_G17_REPORT_BYTES 688u
#define AURA_G17_REPORT_TABLE_BYTES 724u
/* expected_preservation may be NULL for a standalone diagnostic reader. That
 * validates structure, not live firmware identity or reservation ownership. */
int aura_g17_report_validate(const void *payload, size_t length,
                             const void *expected_preservation);
int aura_g17_report_table(const void *payload, size_t length,
                          const void *expected_preservation,
                          void *table, size_t table_size);
int aura_g17_report_table_validate(const void *table, size_t length);
#endif

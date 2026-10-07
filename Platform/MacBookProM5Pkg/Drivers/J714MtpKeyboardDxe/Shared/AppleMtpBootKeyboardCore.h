/* SPDX-License-Identifier: MIT */
#ifndef APPLE_MTP_BOOT_KEYBOARD_CORE_H
#define APPLE_MTP_BOOT_KEYBOARD_CORE_H
#include <stddef.h>
#include <stdint.h>
struct apple_mtp_boot_keyboard {
    uint32_t modifier_bit, keys_bit, report_bytes;
    uint8_t report_id, uses_report_ids, maximum_key;
};
/* Accept descriptor-defined boot keyboard fields, including reports with
 * trailing consumer/vendor data. Other layouts require a future translator. */
int apple_mtp_boot_keyboard_parse(const uint8_t *descriptor, size_t size,
                                 struct apple_mtp_boot_keyboard *layout);
/* Wire input includes its report ID when the descriptor declares IDs.
 * 0: boot report produced; 1: another report ID, output untouched; -1: invalid. */
int apple_mtp_boot_keyboard_decode(const struct apple_mtp_boot_keyboard *layout,
                                  const uint8_t *wire, size_t size, uint8_t boot[8]);
#endif

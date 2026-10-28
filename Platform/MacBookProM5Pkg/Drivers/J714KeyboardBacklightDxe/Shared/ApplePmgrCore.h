/* SPDX-License-Identifier: MIT */
#ifndef NTASI_APPLE_PMGR_CORE_H
#define NTASI_APPLE_PMGR_CORE_H

/*
 * Apple PMGR (power manager) device power-state contract, host-testable seam.
 *
 * Ported from the MIT-licensed m1n1 driver (repos/m1n1/src/pmgr.c + pmgr.h).
 * Each managed device has a 32-bit power-state register (PS reg); this seam
 * models the field layout, the exact register value m1n1 writes to request a
 * mode, and the "has the device settled?" predicate. No MMIO/UEFI/WDK deps.
 *
 * Windows use (driver #3, the driver roadmap): a platform/ACPI power
 * path can compute the same write value and poll condition; the ADT supplies
 * the per-device PS-register addresses and parent topology.
 *
 * m1n1 pmgr_set_mode():
 *   mask32(addr, AUTO_ENABLE | WAS_CLKGATED | WAS_PWRGATED | PS_TARGET,
 *          FIELD_PREP(PS_TARGET, target));         // (old & ~clear) | set
 *   poll32(addr, PS_ACTUAL, FIELD_PREP(PS_ACTUAL, target), timeout);
 */

#include <stdbool.h>
#include <stdint.h>

/* PS register bitfields (m1n1 pmgr.c). */
#define APPLE_PMGR_RESET        (1u << 31)
#define APPLE_PMGR_AUTO_ENABLE  (1u << 28)
#define APPLE_PMGR_PS_AUTO_MASK (0xfu << 24)
#define APPLE_PMGR_PARENT_OFF   (1u << 11)
#define APPLE_PMGR_DEV_DISABLE  (1u << 10)
#define APPLE_PMGR_WAS_CLKGATED (1u << 9)
#define APPLE_PMGR_WAS_PWRGATED (1u << 8)
#define APPLE_PMGR_PS_ACTUAL_SHIFT 4u
#define APPLE_PMGR_PS_ACTUAL_MASK  (0xfu << 4)
#define APPLE_PMGR_PS_TARGET_SHIFT 0u
#define APPLE_PMGR_PS_TARGET_MASK  (0xfu << 0)

/* PS mode values (m1n1 pmgr.h). */
#define APPLE_PMGR_PS_ACTIVE  0xfu
#define APPLE_PMGR_PS_CLKGATE 0x4u
#define APPLE_PMGR_PS_PWRGATE 0x0u

/* m1n1 pmgr.c poll timeout (loop iterations). */
#define APPLE_PMGR_POLL_TIMEOUT 10000u

/* Extract the target/actual 4-bit power-state fields from a PS-reg snapshot. */
uint8_t apple_pmgr_ps_target(uint32_t reg);
uint8_t apple_pmgr_ps_actual(uint32_t reg);

/*
 * The value m1n1 writes to request `target_mode`, given the current register
 * `old`: clears AUTO_ENABLE, the two WAS_* status bits, and the target field,
 * then sets the target field to the low 4 bits of target_mode. Everything else
 * in the register is preserved.
 */
uint32_t apple_pmgr_set_mode_value(uint32_t old, uint8_t target_mode);

/* True once the device has settled into `target_mode` (PS_ACTUAL == target). */
bool apple_pmgr_mode_reached(uint32_t reg, uint8_t target_mode);

/* Status-bit decoders. */
bool apple_pmgr_was_clkgated(uint32_t reg);
bool apple_pmgr_was_pwrgated(uint32_t reg);
bool apple_pmgr_dev_disabled(uint32_t reg);
bool apple_pmgr_parent_off(uint32_t reg);

#endif

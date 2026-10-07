/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#include "AppleKbdBacklightCore.h"

#include <stddef.h>

#define APPLE_KBL_NSEC_PER_SEC UINT64_C(1000000000)

bool apple_kbl_duty_ns_for_brightness(uint32_t period_ns, uint32_t brightness,
                                      uint32_t max_brightness,
                                      uint32_t *duty_ns)
{
    uint64_t duty;

    if (duty_ns == NULL)
        return false;
    *duty_ns = 0u;
    if (period_ns == 0u || max_brightness == 0u ||
        brightness > max_brightness)
        return false;

    /* leds-pwm led_pwm_set(): duty = period * brightness; do_div(duty, max) */
    duty = (uint64_t)period_ns * brightness;
    duty /= max_brightness;
    *duty_ns = (uint32_t)duty;
    return true;
}

bool apple_kbl_cycles_for_brightness(uint64_t clock_hz, uint32_t period_ns,
                                     uint32_t brightness,
                                     uint32_t max_brightness,
                                     APPLE_KBL_CYCLES *cycles)
{
    uint32_t duty_ns;
    uint64_t on_cycles;
    uint64_t total_cycles;

    if (cycles == NULL)
        return false;
    cycles->OnCycles = 0u;
    cycles->OffCycles = 0u;

    /* pwm-apple probe guard: clkrate above NSEC_PER_SEC is rejected. */
    if (clock_hz == 0u || clock_hz > APPLE_KBL_NSEC_PER_SEC)
        return false;
    if (!apple_kbl_duty_ns_for_brightness(period_ns, brightness,
                                          max_brightness, &duty_ns))
        return false;

    /*
     * pwm-apple apple_pwm_apply():
     *   on  = clkrate * duty_cycle / NSEC_PER_SEC        (truncating)
     *   off = clkrate * period / NSEC_PER_SEC - on       (truncating)
     * clock_hz <= 1e9 (< 2^30) and the ns values are 32-bit, so the
     * products fit u64 with room to spare.
     */
    on_cycles = (clock_hz * duty_ns) / APPLE_KBL_NSEC_PER_SEC;
    total_cycles = (clock_hz * period_ns) / APPLE_KBL_NSEC_PER_SEC;
    if (total_cycles == 0u || total_cycles > UINT32_MAX)
        return false;

    cycles->OnCycles = (uint32_t)on_cycles;
    cycles->OffCycles = (uint32_t)(total_cycles - on_cycles);
    return true;
}

bool apple_kbl_brightness_from_cycles(uint32_t on_cycles, uint32_t off_cycles,
                                      uint32_t max_brightness,
                                      uint32_t *brightness)
{
    uint64_t total;
    uint64_t value;

    if (brightness == NULL)
        return false;
    *brightness = 0u;
    if (max_brightness == 0u)
        return false;
    total = (uint64_t)on_cycles + off_cycles;
    if (total == 0u)
        return false;

    /* Nearest brightness: round(on * max / total), clamped to max. */
    value = ((uint64_t)on_cycles * max_brightness + total / 2u) / total;
    if (value > max_brightness)
        value = max_brightness;
    *brightness = (uint32_t)value;
    return true;
}

/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef NTASI_APPLE_KBD_BACKLIGHT_CORE_H
#define NTASI_APPLE_KBD_BACKLIGHT_CORE_H

/*
 * J414s keyboard-backlight FPWM contract, host-testable seam.
 *
 * The MacBook Pro 14" (M2 Pro, J414s) internal keyboard backlight is a white
 * LED string driven by channel 0 of the SoC "fixed PWM" block fpwm0.  It is
 * not an SMC key and not an MTP/DockChannel HID report; it needs no
 * interrupt, no DART, no firmware, and no coprocessor.
 *
 * Provenance (every number below is anchored, per the repository's
 * verify-struct-offsets-not-headers rule):
 *  - Register offsets and control bits: mainline drivers/pwm/pwm-apple.c
 *    ("Dual MIT/GPL", Asahi Linux contributors), lines 23-32.
 *  - Brightness -> duty formula: drivers/leds/leds-pwm.c led_pwm_set()
 *    (duty = period * brightness / max_brightness, truncating), with
 *    active_low unset for this LED node.
 *  - Duty -> cycles formula: pwm-apple.c apple_pwm_apply()
 *    (on = clk * duty_ns / 1e9; off = clk * period_ns / 1e9 - on, truncating).
 *  - Platform data: Linux arch/arm64/boot/dts/apple/
 *    t600x-j314-j316.dtsi:55-65 (led-controller, pwms = <&fpwm0 0 40000>,
 *    max-brightness 255), included by t602x-j414-j416.dtsi:18 for J414s;
 *    t602x-die0.dtsi:696-703 (fpwm0 at 0x39b030000, size 0x4000, clkref);
 *    t602x-dieX.dtsi:121-126 (pmgr_east at 0x290280000);
 *    t602x-pmgr.dtsi:1221-1228 and :1268-1275 (ps_sio at pmgr_east+0x1c0,
 *    ps_fpwm0 at pmgr_east+0x1e8, chain ps_fpwm0 <- ps_sio <- ps_afnc2_lw1,
 *    where ps_afnc2_lw1 is apple,always-on), all at Asahi linux-asahi commit
 *    030248d39b401c94695c9f7df2fed630d35120cd.
 *  - Cross-generation corroboration: the live macOS ADT on a Mac17,9 exposes
 *    /arm-io/pwm0 (compatible "fpwm,s5l8920x") with child kbd-backlight@0,
 *    pwm-frequency 24000000, and high-period + low-period = 4 + 956 = 960
 *    cycles = 40000 ns at 24 MHz, matching the Linux period exactly.
 *
 * No MMIO/UEFI/WDK dependencies here; the KMDF driver supplies register I/O.
 */

#include <stdbool.h>
#include <stdint.h>

/* FPWM register block (pwm-apple.c). */
#define APPLE_KBL_FPWM_CTRL        0x00u
#define APPLE_KBL_FPWM_OFF_CYCLES  0x18u
#define APPLE_KBL_FPWM_ON_CYCLES   0x1cu

#define APPLE_KBL_FPWM_CTRL_ENABLE        (1u << 0)
#define APPLE_KBL_FPWM_CTRL_MODE          (1u << 2)
#define APPLE_KBL_FPWM_CTRL_UPDATE        (1u << 5)
#define APPLE_KBL_FPWM_CTRL_TRIGGER       (1u << 9)
#define APPLE_KBL_FPWM_CTRL_INVERT        (1u << 10)
#define APPLE_KBL_FPWM_CTRL_OUTPUT_ENABLE (1u << 14)

/*
 * The exact control value apple_pwm_apply() writes to run the PWM, and the
 * exact value it writes to stop it (constant-low output).  Cycle registers
 * are shadowed until the control write, so the driver must write ON, then
 * OFF, then CTRL.
 */
#define APPLE_KBL_FPWM_CTRL_RUN                                        \
    (APPLE_KBL_FPWM_CTRL_ENABLE | APPLE_KBL_FPWM_CTRL_OUTPUT_ENABLE | \
     APPLE_KBL_FPWM_CTRL_UPDATE)
#define APPLE_KBL_FPWM_CTRL_STOP 0u

/* J414s platform facts (see provenance above; ACPI republishes these). */
#define APPLE_KBL_T6020_FPWM0_BASE      UINT64_C(0x39B030000)
#define APPLE_KBL_T6020_FPWM_SIZE       0x4000u
#define APPLE_KBL_T6020_PMGR_EAST_BASE  UINT64_C(0x290280000)
#define APPLE_KBL_T6020_PMGR_PS_WINDOW  0x1000u
#define APPLE_KBL_T6020_PS_SIO_OFFSET   0x1c0u
#define APPLE_KBL_T6020_PS_FPWM0_OFFSET 0x1e8u

/* PWM parameters pinned by the J414s device tree and the live macOS ADT. */
#define APPLE_KBL_CLOCK_HZ       24000000u
#define APPLE_KBL_PERIOD_NS      40000u
#define APPLE_KBL_MAX_BRIGHTNESS 255u
/* 24 MHz * 40000 ns = 960 cycles per period. */
#define APPLE_KBL_TOTAL_CYCLES   960u

typedef struct APPLE_KBL_CYCLES {
    uint32_t OnCycles;
    uint32_t OffCycles;
} APPLE_KBL_CYCLES;

/*
 * leds-pwm led_pwm_set(): duty_ns = period_ns * brightness / max_brightness,
 * truncating integer division.  false on invalid input (zero period, zero
 * max, brightness above max).
 */
bool apple_kbl_duty_ns_for_brightness(uint32_t period_ns, uint32_t brightness,
                                      uint32_t max_brightness,
                                      uint32_t *duty_ns);

/*
 * pwm-apple apple_pwm_apply() for the enabled case, composed with the
 * leds-pwm brightness scaling: computes the exact ON_CYCLES/OFF_CYCLES
 * register values.  Rejects a clock above 1 GHz (the pwm-apple probe guard),
 * a zero clock/period/max, brightness above max, and a period that yields
 * zero total cycles.  On success OnCycles + OffCycles equals the fixed total
 * cycle count for (clock_hz, period_ns).
 */
bool apple_kbl_cycles_for_brightness(uint64_t clock_hz, uint32_t period_ns,
                                     uint32_t brightness,
                                     uint32_t max_brightness,
                                     APPLE_KBL_CYCLES *cycles);

/*
 * Inverse mapping for get-state readback: nearest brightness for a register
 * snapshot.  Total period is taken from the registers themselves
 * (on + off), so it round-trips every brightness produced by
 * apple_kbl_cycles_for_brightness at the J414s parameters.  false when the
 * registers describe an empty period.
 */
bool apple_kbl_brightness_from_cycles(uint32_t on_cycles, uint32_t off_cycles,
                                      uint32_t max_brightness,
                                      uint32_t *brightness);

#endif

/* SPDX-License-Identifier: MIT */
#include "ApplePmgrCore.h"

uint8_t apple_pmgr_ps_target(uint32_t reg)
{
    return (uint8_t)((reg & APPLE_PMGR_PS_TARGET_MASK) >> APPLE_PMGR_PS_TARGET_SHIFT);
}

uint8_t apple_pmgr_ps_actual(uint32_t reg)
{
    return (uint8_t)((reg & APPLE_PMGR_PS_ACTUAL_MASK) >> APPLE_PMGR_PS_ACTUAL_SHIFT);
}

uint32_t apple_pmgr_set_mode_value(uint32_t old, uint8_t target_mode)
{
    uint32_t clear = APPLE_PMGR_AUTO_ENABLE | APPLE_PMGR_WAS_CLKGATED |
                     APPLE_PMGR_WAS_PWRGATED | APPLE_PMGR_PS_TARGET_MASK;
    uint32_t set = ((uint32_t)target_mode << APPLE_PMGR_PS_TARGET_SHIFT) &
                   APPLE_PMGR_PS_TARGET_MASK;
    return (old & ~clear) | set;
}

bool apple_pmgr_mode_reached(uint32_t reg, uint8_t target_mode)
{
    return apple_pmgr_ps_actual(reg) == (uint8_t)(target_mode & 0xfu);
}

bool apple_pmgr_was_clkgated(uint32_t reg)
{
    return (reg & APPLE_PMGR_WAS_CLKGATED) != 0u;
}

bool apple_pmgr_was_pwrgated(uint32_t reg)
{
    return (reg & APPLE_PMGR_WAS_PWRGATED) != 0u;
}

bool apple_pmgr_dev_disabled(uint32_t reg)
{
    return (reg & APPLE_PMGR_DEV_DISABLE) != 0u;
}

bool apple_pmgr_parent_off(uint32_t reg)
{
    return (reg & APPLE_PMGR_PARENT_OFF) != 0u;
}

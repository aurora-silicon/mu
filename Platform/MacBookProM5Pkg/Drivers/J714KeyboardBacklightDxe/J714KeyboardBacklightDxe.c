/* SPDX-License-Identifier: MIT */
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include "Shared/ApplePmgrCore.h"
#include "Shared/AppleKbdBacklightCore.h"

#define PWM_BASE  0x62610000UL
#define PMGR_BASE 0x62614000UL

STATIC EFI_STATUS
Activate (UINTN Offset)
{
  UINT32 Value = MmioRead32 (PMGR_BASE + Offset);
  UINTN Count;
  if (apple_pmgr_dev_disabled (Value)) return EFI_DEVICE_ERROR;
  if (apple_pmgr_mode_reached (Value, APPLE_PMGR_PS_ACTIVE) &&
      apple_pmgr_ps_target (Value) == APPLE_PMGR_PS_ACTIVE) return EFI_SUCCESS;
  MmioWrite32 (PMGR_BASE + Offset, apple_pmgr_set_mode_value (Value, APPLE_PMGR_PS_ACTIVE));
  for (Count = 0; Count < 10000; Count++) {
    Value = MmioRead32 (PMGR_BASE + Offset);
    if (apple_pmgr_mode_reached (Value, APPLE_PMGR_PS_ACTIVE)) return EFI_SUCCESS;
    if (apple_pmgr_parent_off (Value)) break;
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}

EFI_STATUS EFIAPI
J714KeyboardBacklightEntry (EFI_HANDLE Image, EFI_SYSTEM_TABLE *System)
{
  CONST UINTN Domains[] = {0x288, 0x290, 0x2a8};
  APPLE_KBL_CYCLES Cycles;
  EFI_STATUS Status;
  UINTN Index;
  // x1n1 validates the live ADT PWM, period, PMGR parents and mappings.
  if (MmioRead64 (0x61f00000) != 0x78316e31 ||
      MmioRead64 (0x61f00008) != 1 || MmioRead64 (0x61f00050) != 1)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < ARRAY_SIZE (Domains); Index++) {
    Status = Activate (Domains[Index]);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "J714_KBL: PMGR +%x failed: %r\n", (UINT32)Domains[Index], Status));
      return Status;
    }
  }
  if (!apple_kbl_cycles_for_brightness (24000000, 40000, 128, 255, &Cycles))
    return EFI_INVALID_PARAMETER;
  MmioWrite32 (PWM_BASE + APPLE_KBL_FPWM_ON_CYCLES, Cycles.OnCycles);
  MmioWrite32 (PWM_BASE + APPLE_KBL_FPWM_OFF_CYCLES, Cycles.OffCycles);
  MmioWrite32 (PWM_BASE + APPLE_KBL_FPWM_CTRL, APPLE_KBL_FPWM_CTRL_RUN);
  // Keep the illumination on for the next OS; its normal PWM driver owns
  // subsequent changes. There are no shared buffers, timers or interrupts.
  DEBUG ((DEBUG_INFO, "J714_KBL: firmware keyboard illumination 128/255\n"));
  return EFI_SUCCESS;
}

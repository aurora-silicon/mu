/**
 * @file AppleAicDxe.c
 * @author amarioguy (arminders208@outlook.com)
 * 
 * This file implements the selector logic for the AIC DXE Driver. Based off ArmGicDxe
 * 
 * @version 1.0
 * @date 2022-08-16
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 * 
 * Credits to the Asahi Linux contributors for the initial AIC implementations in m1n1 and linux.
 * 
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */

#include <PiDxe.h>

#include "AppleAicDxe.h"

/**
 * Initialize the AIC protocol.
 * 
 * @param ImageHandle 
 * @param SystemTable 
 * @return EFI_STATUS 
 */

EFI_STATUS
InterruptDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  APPLE_AIC_VERSION Version;

  Version = AppleArmGetAicVersion();
  switch(Version) {
    case APPLE_AIC_VERSION_1:
    case APPLE_AIC_VERSION_2:
    case APPLE_AIC_VERSION_3:
      //
      // AICv2 and AICv3 (T6050 / M5-class) share one MMIO-programming engine
      // whose register geometry is derived from the ADT; AppleAicV2DxeInit()
      // handles both. AICv1 is currently a stub (AppleAicV1DxeInit is unused).
      //
      Status = AppleAicV2DxeInit(ImageHandle, SystemTable, Version);
      break;
    default:
      DEBUG((DEBUG_INFO, "Unsupported AIC revision, exiting\n"));
      Status = EFI_UNSUPPORTED;
  }

  return Status;
}
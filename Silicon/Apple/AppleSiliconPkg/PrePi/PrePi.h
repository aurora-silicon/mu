/** @file

  Copyright (c) 2011 - 2020, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _PREPI_H_
#define _PREPI_H_

#include <PiPei.h>

#include <Library/PcdLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PrePiLibProgress.h>

EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize
  );

EFI_STATUS
EFIAPI
PlatformPeim (
  VOID
  );

//
// J804 windows profile, G0 GPU (AdtParser.c): describe the GPU pool that
// EarlySetup took out of RAM as reserved memory. Called on the line of an
// existing statement in PrePi.c, so that builds without J804_WINDOWS_GPU keep
// every __LINE__ (ASSERT) of PrePi.c and stay byte-identical.
//
#if defined (J804_WINDOWS_GPU)
VOID
J804GpuReservePoolHob (
  VOID
  );

#define J804_GPU_RESERVE_POOL_HOB()  J804GpuReservePoolHob ()
#else
#define J804_GPU_RESERVE_POOL_HOB()
#endif


#endif /* _PREPI_H_ */
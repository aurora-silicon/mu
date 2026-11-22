/** @file
*  Exception Handling support specific for AArch64
*
*  Copyright (c) 2016 HP Development Company, L.P.
*  Copyright (c) 2021, Arm Limited. All rights reserved.<BR>
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Uefi.h>

#include <AArch64/AArch64.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/DebugSupport.h> // for MAX_AARCH64_EXCEPTION

UINTN                   gMaxExceptionNumber                           = MAX_AARCH64_EXCEPTION;
EFI_EXCEPTION_CALLBACK  gExceptionHandlers[MAX_AARCH64_EXCEPTION + 1] = { 0 };
PHYSICAL_ADDRESS        gExceptionVectorAlignmentMask                 = ARM_VECTOR_TABLE_ALIGNMENT;

#define EL0_STACK_SIZE  EFI_PAGES_TO_SIZE(2)
#if NTASI_J813_PMCCNTR_EMULATION
STATIC VOID  *mAllocatedEl0Stack;
#else
STATIC UINTN  mNewStackBase[EL0_STACK_SIZE / sizeof (UINTN)];
#endif

VOID
RegisterEl0Stack (
  IN  VOID  *Stack
  );

RETURN_STATUS
ArchVectorConfig (
  IN  UINTN  VectorBaseAddress
  )
{
  UINTN  HcrReg;
  VOID   *El0StackBase;

#if NTASI_J813_PMCCNTR_EMULATION
  // ArmCpuDxe's image data can become read-protected before bootmgfw probes
  // PMCCNTR_EL0. Keep the recoverable exception frame on dedicated RW pages.
  if (mAllocatedEl0Stack == NULL) {
    mAllocatedEl0Stack = AllocatePages (EFI_SIZE_TO_PAGES (EL0_STACK_SIZE));
    if (mAllocatedEl0Stack == NULL) {
      return RETURN_OUT_OF_RESOURCES;
    }
  }

  El0StackBase = mAllocatedEl0Stack;
#else
  El0StackBase = mNewStackBase;
#endif

  // Round down sp by 16 bytes alignment
  RegisterEl0Stack (
    // CLANGPDB uses LLP64: unsigned long is 32 bits, while UINTN is 64.
    // Keep the alignment mask pointer-sized for firmware above 4 GiB.
    (VOID *)(((UINTN)El0StackBase + EL0_STACK_SIZE) & ~((UINTN)0xF))
    );

  if (ArmReadCurrentEL () == AARCH64_EL2) {
    HcrReg = ArmReadHcr ();

    // Trap General Exceptions. All exceptions that would be routed to EL1 are routed to EL2
    HcrReg |= ARM_HCR_TGE;

    ArmWriteHcr (HcrReg);
  }

  return RETURN_SUCCESS;
}

/* SPDX-License-Identifier: MIT */
#ifndef AURA_G17_GPU_DEVICE_H_
#define AURA_G17_GPU_DEVICE_H_

#include <IndustryStandard/Acpi.h>

/* TRUE for every table except the J813 G17 GPU SSDT ("AURA817G"), which is
 * admitted (and patched in place) only under the conditions in the .c file. */
BOOLEAN
AppleG17GpuTableAdmitted (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN     UINTN                        Size
  );

#endif

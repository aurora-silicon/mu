/* SPDX-License-Identifier: MIT */
#ifndef AURA_G17_GPU_DEVICE_H_
#define AURA_G17_GPU_DEVICE_H_

#include <IndustryStandard/Acpi.h>

/* TRUE for every table except the J813 G17 GPU SSDTs ("AURA817G", and the
 * unified "AURA817F" and "AURA817P" with the local-memory pool), which are
 * admitted (and patched in place) only under the conditions in the .c file. */
BOOLEAN
AppleG17GpuTableAdmitted (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN     UINTN                        Size
  );

#endif

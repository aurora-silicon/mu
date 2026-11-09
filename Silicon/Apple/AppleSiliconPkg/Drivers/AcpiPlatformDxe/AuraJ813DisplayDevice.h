/* SPDX-License-Identifier: MIT */
#ifndef AURA_J813_DISPLAY_DEVICE_H_
#define AURA_J813_DISPLAY_DEVICE_H_

#include <IndustryStandard/Acpi.h>

/* TRUE for every table except the J813 DCP SSDT ("AURA818D"), which is
 * admitted only under the conditions in the .c file. */
BOOLEAN
AppleJ813DisplayTableAdmitted (
  IN CONST EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN       UINTN                        Size
  );

#endif

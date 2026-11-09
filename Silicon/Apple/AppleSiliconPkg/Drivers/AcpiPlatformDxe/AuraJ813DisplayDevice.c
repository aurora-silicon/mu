/* SPDX-License-Identifier: MIT */
/*
 * J813 display coprocessor device (Platform/MacBookAir2026Pkg/AcpiTables/
 * DISP.asl, ACPI\AURA8180).
 *
 * The SSDT is admitted only in images built with PcdAppleJ813DisplayDevice,
 * on chip 0x8142 / board 0x24, when the ADT describes /arm-io/dcp and
 * /arm-io/disp0. Its seven MMIO windows are fixed T8142 addresses and are
 * not patched. Any doubt withholds the device; BasicDisplay is unaffected
 * either way.
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>

#include "AuraJ813DisplayDevice.h"

STATIC
BOOLEAN
ReadU32 (
  IN  CONST CHAR8  *Node,
  IN  CONST CHAR8  *Name,
  OUT UINT32       *Value
  )
{
  CONST VOID  *Property;
  size_t      Length;

  Property = dt_get_prop (Node, Name, &Length);
  if ((Property == NULL) || (Length != sizeof (*Value))) {
    return FALSE;
  }

  CopyMem (Value, Property, sizeof (*Value));
  return TRUE;
}

STATIC
BOOLEAN
NodePresent (
  IN CONST CHAR8  *Node
  )
{
  size_t  Length;

  return dt_get_prop (Node, "compatible", &Length) != NULL && Length != 0;
}

BOOLEAN
AppleJ813DisplayTableAdmitted (
  IN CONST EFI_ACPI_DESCRIPTION_HEADER  *Table,
  IN       UINTN                        Size
  )
{
  UINT32  Value;

  if ((Size < sizeof (*Table)) || (Table->Length > Size)) {
    return FALSE;
  }

  if ((Table->Signature != SIGNATURE_32 ('S', 'S', 'D', 'T')) ||
      (CompareMem (&Table->OemTableId, "AURA818D", 8) != 0))
  {
    return TRUE;
  }

  if (!FixedPcdGetBool (PcdAppleJ813DisplayDevice)) {
    return FALSE;
  }

  if (!ReadU32 ("/chosen", "chip-id", &Value) || (Value != 0x8142) ||
      !ReadU32 ("/chosen", "board-id", &Value) || (Value != 0x24))
  {
    DEBUG ((DEBUG_ERROR, "Aura J813 display: not J813; AURA8180 withheld\n"));
    return FALSE;
  }

  if (!NodePresent ("/arm-io/dcp") || !NodePresent ("/arm-io/disp0")) {
    DEBUG ((DEBUG_ERROR, "Aura J813 display: ADT has no dcp/disp0; AURA8180 withheld\n"));
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "Aura J813 display: publishing AURA8180\n"));
  return TRUE;
}

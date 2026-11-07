/* SPDX-License-Identifier: MIT */
#include <Uefi.h>
#include <IndustryStandard/AuraG17Report.h>
#include <Library/AppleDTLib.h>
#include <Library/AuraG17PreservationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>

EFI_STATUS AuraG17VerifyCurrentPreservation (VOID);
STATIC BOOLEAN mReportInstalled;

/* ADT is bounded and copied by EarlySetup. Unlike a first-match lookup, count
 * both admission markers so ambiguous duplicate properties fail closed. */
STATIC UINTN
PropertyCount (CONST CHAR8 *Name)
{
  dt_node_t *Node = dt_get ("/chosen");
  dt_prop_t *Property;
  UINTN Index, Count = 0;
  if (Node == NULL) return 0;
  Property = (dt_prop_t *)Node->prop;
  for (Index = 0; Index < Node->nprop; ++Index) {
    if (AsciiStrnCmp (Property->key, Name, DT_KEY_LEN) == 0) ++Count;
    Property = (dt_prop_t *)(Property->val + (((Property->len & 0xffffff) + 3) & ~3u));
  }
  return Count;
}

EFI_STATUS
AuraG17VerifyPublishedReport (VOID);

STATIC EFI_STATUS
ReportInventory (CONST UINT8 *Expected)
{
  EFI_ACPI_SDT_PROTOCOL *Sdt;
  EFI_ACPI_SDT_HEADER *Existing;
  EFI_ACPI_TABLE_VERSION Version;
  EFI_STATUS Status;
  UINTN Index, Key, Count = 0;
  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&Sdt);
  if (EFI_ERROR (Status)) return Status;
  for (Index = 0; Index < 4096; ++Index) {
    Status = Sdt->GetAcpiTable (Index, &Existing, &Version, &Key);
    if (Status == EFI_NOT_FOUND) break;
    if (EFI_ERROR (Status)) return Status;
    if (Existing == NULL) return EFI_COMPROMISED_DATA;
    if (Existing->Signature != SIGNATURE_32 ('A', 'G', '1', '7')) continue;
    if (Expected == NULL || ++Count != 1 || Existing->Length != AURA_G17_REPORT_TABLE_BYTES ||
        CompareMem (Existing, Expected, AURA_G17_REPORT_TABLE_BYTES) != 0)
      return EFI_COMPROMISED_DATA;
  }
  if (Index == 4096 || (Expected != NULL && Count != 1)) return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
BuildCurrentTable (UINT8 *Table)
{
  UINTN PreserveLength;
  size_t ReportLength;
  CONST VOID *Report, *Preserve;
  if (PropertyCount (AURA_G17_REPORT_PROPERTY) != 1 ||
      PropertyCount (AURA_G17_PROPERTY) != 1) return EFI_COMPROMISED_DATA;
  Report = dt_get_prop ("/chosen", AURA_G17_REPORT_PROPERTY, &ReportLength);
  Preserve = AuraG17Record (&PreserveLength);
  if (Preserve == NULL || PreserveLength != AURA_G17_BYTES ||
      !aura_g17_report_table (Report, ReportLength, Preserve, Table, AURA_G17_REPORT_TABLE_BYTES))
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

EFI_STATUS
AuraG17VerifyPublishedReport (VOID)
{
  EFI_STATUS Status;
  UINT8 Table[AURA_G17_REPORT_TABLE_BYTES];
  if (!mReportInstalled)
    return PropertyCount (AURA_G17_REPORT_PROPERTY) == 0 ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
  Status = BuildCurrentTable (Table);
  return EFI_ERROR (Status) ? Status : ReportInventory (Table);
}

EFI_STATUS
AuraG17PublishReport (EFI_ACPI_TABLE_PROTOCOL *Acpi)
{
  EFI_STATUS Status;
  UINTN Key;
  UINT8 Table[AURA_G17_REPORT_TABLE_BYTES];
  if (PropertyCount (AURA_G17_REPORT_PROPERTY) == 0) return EFI_SUCCESS;
  if (mReportInstalled) return EFI_ALREADY_STARTED;
  if (Acpi == NULL) return EFI_INVALID_PARAMETER;
  /* Recheck current ADT, exact single PEI HOB and reserved memory map here.
   * Calling this entry directly cannot bypass the earlier DXE admission. */
  Status = AuraG17VerifyCurrentPreservation ();
  if (EFI_ERROR (Status)) return Status;
  Status = BuildCurrentTable (Table);
  if (EFI_ERROR (Status)) return Status;
  Status = ReportInventory (NULL);
  if (EFI_ERROR (Status)) return Status;
  Status = Acpi->InstallAcpiTable (Acpi, Table, sizeof (Table), &Key);
  if (!EFI_ERROR (Status)) mReportInstalled = TRUE;
  return Status;
}

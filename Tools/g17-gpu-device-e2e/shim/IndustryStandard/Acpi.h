#pragma once
#include <Uefi.h>
#pragma pack(1)
typedef struct { UINT32 Signature, Length; UINT8 Revision, Checksum; UINT8 OemId[6];
                 UINT64 OemTableId; UINT32 OemRevision, CreatorId, CreatorRevision; } EFI_ACPI_DESCRIPTION_HEADER;
#pragma pack()

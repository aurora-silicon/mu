/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host definitions shared by acpi-table-loop-host.c and the AcpiPlatformDxe
 * admission sources it compiles verbatim (AuraG17GpuDevice.c,
 * AuraJ813DisplayDevice.c, AuraJ813UnifiedDisplay.c; acpi-table-loop.py
 * drops their angle-bracket includes and inlines their own headers).
 */
#ifndef ACPI_TABLE_LOOP_HOST_H_
#define ACPI_TABLE_LOOP_HOST_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN
#define OUT
#define OPTIONAL
#define CONST    const
#define STATIC   static
#define VOID     void
#define EFIAPI
#define TRUE     true
#define FALSE    false
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef char      CHAR8;
typedef bool      BOOLEAN;
typedef UINTN     EFI_STATUS;
typedef UINT8     EFI_SECTION_TYPE;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef void      *EFI_HANDLE;
typedef struct { UINT32 a; UINT16 b, c; UINT8 d[8]; } EFI_GUID;

#define ENCODE_ERROR(x)            ((EFI_STATUS)(((UINTN)1 << 63) | (x)))
#define EFI_SUCCESS                0
#define EFI_WARN_BUFFER_TOO_SMALL  4
#define EFI_INVALID_PARAMETER      ENCODE_ERROR (2)
#define EFI_NOT_FOUND              ENCODE_ERROR (14)
#define EFI_ABORTED                ENCODE_ERROR (21)
#define EFI_COMPROMISED_DATA       ENCODE_ERROR (33)
#define EFI_ERROR(s)               ((INTN)(s) < 0)
#define EFI_SECTION_RAW            0x19
#define SIGNATURE_32(a, b, c, d)   ((UINT32)(a) | ((UINT32)(b) << 8) | ((UINT32)(c) << 16) | ((UINT32)(d) << 24))
#define OFFSET_OF(t, m)            offsetof (t, m)
#define CompareMem(a, b, n)        memcmp ((a), (b), (n))
#define CopyMem(d, s, n)           memcpy ((d), (s), (n))

#define DEBUG_INIT     0x00000001
#define DEBUG_WARN     0x00000002
#define DEBUG_LOAD     0x00000004
#define DEBUG_INFO     0x00000040
#define DEBUG_VERBOSE  0x00400000
#define DEBUG_ERROR    0x80000000
void  HostDebugPrint (UINTN Level, const char *Format, ...);
#define DEBUG(x)       HostDebugPrint x
void  Violation (const char *Format, ...);
#define ASSERT(x)      do { if (!(x)) Violation ("ASSERT (%s) failed", #x); } while (0)

/* FixedAtBuild PCDs the admission code reads; set per run by main(). */
extern BOOLEAN  HostPcd_PcdAppleG17GpuProbe;
extern BOOLEAN  HostPcd_PcdAppleJ813DisplayDevice;
extern BOOLEAN  HostPcd_PcdAppleJ813UnifiedDisplay;
extern BOOLEAN  HostPcd_PcdAppleN1BusHandoff;
extern BOOLEAN  HostPcd_PcdAppleJ813GpuLocalPool;
#define FixedPcdGetBool(x)  (HostPcd_##x)
#define STATIC_ASSERT(x, m) _Static_assert (x, m)

/* HobLib's GUID-HOB walk, over the HOB list the host loads (hobs=FILE). */
typedef struct {
  UINT16 HobType;
  UINT16 HobLength;
  UINT32 Reserved;
} EFI_HOB_GENERIC_HEADER;
typedef struct {
  EFI_HOB_GENERIC_HEADER Header;
  EFI_GUID               Name;
} EFI_HOB_GUID_TYPE;
#define GET_HOB_LENGTH(h)          (((EFI_HOB_GENERIC_HEADER *)(h))->HobLength)
#define GET_GUID_HOB_DATA(h)       ((VOID *)((UINT8 *)(h) + sizeof (EFI_HOB_GUID_TYPE)))
#define GET_GUID_HOB_DATA_SIZE(h)  ((UINT16)(GET_HOB_LENGTH (h) - sizeof (EFI_HOB_GUID_TYPE)))
#define GET_NEXT_HOB(h)            ((VOID *)((UINT8 *)(h) + GET_HOB_LENGTH (h)))
VOID  *GetFirstGuidHob (CONST EFI_GUID *Guid);
VOID  *GetNextGuidHob (CONST EFI_GUID *Guid, CONST VOID *HobStart);

/* AppleDTLib: a modeled ADT (see acpi-table-loop-host.c). */
typedef struct HOST_DT_NODE dt_node_t;
dt_node_t  *dt_get (const char *Name);
void       *dt_prop (dt_node_t *Node, const char *Key, size_t *Length);
void       *dt_get_prop (const char *Device, const char *Prop, size_t *Size);

/* MemoryAllocationLib / PrintLib / DXE services used by the G17 helper. */
VOID   FreePool (VOID *Buffer);
UINTN  AsciiSPrint (CHAR8 *Buffer, UINTN Size, CONST CHAR8 *Format, ...);
typedef enum {
  EfiGcdMemoryTypeNonExistent,
  EfiGcdMemoryTypeReserved,
  EfiGcdMemoryTypeSystemMemory,
  EfiGcdMemoryTypeMemoryMappedIo,
  EfiGcdMemoryTypePersistent,
  EfiGcdMemoryTypeMoreReliable,
  EfiGcdMemoryTypeUnaccepted,
  EfiGcdMemoryTypeMaximum
} EFI_GCD_MEMORY_TYPE;
typedef struct {
  EFI_PHYSICAL_ADDRESS  BaseAddress;
  UINT64                Length;
  UINT64                Capabilities;
  UINT64                Attributes;
  EFI_GCD_MEMORY_TYPE   GcdMemoryType;
  EFI_HANDLE            ImageHandle;
  EFI_HANDLE            DeviceHandle;
} EFI_GCD_MEMORY_SPACE_DESCRIPTOR;
typedef struct {
  EFI_STATUS (EFIAPI *GetMemorySpaceMap)(UINTN *, EFI_GCD_MEMORY_SPACE_DESCRIPTOR **);
} HOST_DXE_SERVICES;
extern HOST_DXE_SERVICES  *gDS;

#pragma pack(1)
typedef struct {
  UINT32 Signature;
  UINT32 Length;
} EFI_ACPI_COMMON_HEADER;
typedef struct {
  UINT32 Signature;
  UINT32 Length;
  UINT8  Revision;
  UINT8  Checksum;
  UINT8  OemId[6];
  UINT64 OemTableId;
  UINT32 OemRevision;
  UINT32 CreatorId;
  UINT32 CreatorRevision;
} EFI_ACPI_DESCRIPTION_HEADER;
#pragma pack()

/* The admission helpers outside AcpiPlatform.c, compiled from the revision
 * under test.  AppleJ813UnifiedDisplaySelected exists only in revisions with
 * the unified selection; older revisions never reference it. */
BOOLEAN AppleG17GpuTableAdmitted (IN OUT EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size);
BOOLEAN AppleJ813DisplayTableAdmitted (IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size);
BOOLEAN AppleJ813UnifiedDisplaySelected (VOID);

#endif

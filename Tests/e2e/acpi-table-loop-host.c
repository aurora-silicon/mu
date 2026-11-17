/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host harness for AcpiPlatform's device-table loop.  loop.inc and
 * helpers.inc are cut verbatim from AcpiPlatform.c by acpi-table-loop.py;
 * everything below models the firmware services around them:
 *
 *   FV2 ReadSection  follows DxeCore FwVolReadSection: a NULL *Buffer is
 *                    allocated from pool, a non-NULL one is filled in place
 *                    (truncated with EFI_WARN_BUFFER_TOO_SMALL), and
 *                    *BufferSize returns the section size.
 *   gBS->FreePool    pool memory is never returned to the host allocator, so
 *                    a double free or a write into freed pool is observed and
 *                    reported instead of corrupting the harness.
 *   InstallAcpiTable copies the table, as AcpiTableDxe does.
 *   ADT /chosen      cpufreq marker, chip-id, board-id, MTP policy.
 *
 * Output: one JSON line on stdout.
 */
#include <setjmp.h>
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
#define DEBUG(x)                   do { } while (0)
#define ASSERT(x)                  do { if (!(x)) Violation ("ASSERT (%s) failed", #x); } while (0)
#define PcdGetPtr(x)               (&(x))
#define CompareMem(a, b, n)        memcmp ((a), (b), (n))
#define CopyMem(d, s, n)           memcpy ((d), (s), (n))

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

typedef struct EFI_FIRMWARE_VOLUME2_PROTOCOL EFI_FIRMWARE_VOLUME2_PROTOCOL;
struct EFI_FIRMWARE_VOLUME2_PROTOCOL {
  EFI_STATUS (EFIAPI *ReadSection)(CONST EFI_FIRMWARE_VOLUME2_PROTOCOL *, CONST EFI_GUID *,
                                   EFI_SECTION_TYPE, UINTN, VOID **, UINTN *, UINT32 *);
};
typedef struct EFI_ACPI_TABLE_PROTOCOL EFI_ACPI_TABLE_PROTOCOL;
struct EFI_ACPI_TABLE_PROTOCOL {
  EFI_STATUS (EFIAPI *InstallAcpiTable)(EFI_ACPI_TABLE_PROTOCOL *, VOID *, UINTN, UINTN *);
};
typedef struct {
  EFI_STATUS (EFIAPI *FreePool)(VOID *);
} HOST_BOOT_SERVICES;

/* ---- recorded outcome ---------------------------------------------------- */
#define MAX_RECORDS  64
static char     *Violations[MAX_RECORDS];
static unsigned ViolationCount;
static jmp_buf  AbortLoop;
static unsigned ReadCalls, ReadLimit;
static char     *Installed[MAX_RECORDS];
static unsigned InstalledCount;

static void
Violation (const char *Format, ...)
{
  char    Text[256];
  va_list Args;

  va_start (Args, Format);
  vsnprintf (Text, sizeof (Text), Format, Args);
  va_end (Args);
  if (ViolationCount < MAX_RECORDS) {
    Violations[ViolationCount++] = strdup (Text);
  }
}

/* ---- pool model ---------------------------------------------------------- */
typedef struct { UINT8 *Base; UINTN Size; bool Freed; } POOL_BLOCK;
static POOL_BLOCK Pool[MAX_RECORDS * 4];
static unsigned   PoolCount;

static POOL_BLOCK *
PoolFind (const void *Pointer)
{
  for (unsigned Index = 0; Index < PoolCount; Index++) {
    if (Pool[Index].Base == Pointer) {
      return &Pool[Index];
    }
  }
  return NULL;
}

static void *
PoolAllocate (UINTN Size)
{
  if (PoolCount == sizeof (Pool) / sizeof (Pool[0])) {
    Violation ("pool model exhausted");
    longjmp (AbortLoop, 1);
  }
  Pool[PoolCount].Base  = calloc (1, Size);
  Pool[PoolCount].Size  = Size;
  Pool[PoolCount].Freed = false;
  return Pool[PoolCount++].Base;
}

static EFI_STATUS EFIAPI
HostFreePool (VOID *Buffer)
{
  POOL_BLOCK *Block = PoolFind (Buffer);

  if (Block == NULL) {
    Violation ("FreePool of a pointer the pool never returned");
    longjmp (AbortLoop, 1);
  }
  if (Block->Freed) {
    /* DxeCore asserts here (Pool.c), which is how boot 145 ended. */
    Violation ("double free of a %u-byte table (F1)", (unsigned)Block->Size);
    longjmp (AbortLoop, 1);
  }
  Block->Freed = true;
  memset (Block->Base, 0xAF, Block->Size);
  return EFI_SUCCESS;
}

static HOST_BOOT_SERVICES  HostBs = { HostFreePool };
static HOST_BOOT_SERVICES  *gBS   = &HostBs;

/* ---- storage file -------------------------------------------------------- */
static EFI_GUID PcdDeviceAcpiTableStorageFile = { 0x7E374E25, 0x8E01, 0x4FEE, { 0x87, 0xF2, 0x39, 0x0C, 0x23, 0xC6, 0x06, 0xCD } };
static UINT8    *Tables[MAX_RECORDS];
static UINTN    TableSizes[MAX_RECORDS];
static unsigned TableCount;

static EFI_STATUS EFIAPI
HostReadSection (
  CONST EFI_FIRMWARE_VOLUME2_PROTOCOL  *This,
  CONST EFI_GUID                       *NameGuid,
  EFI_SECTION_TYPE                     SectionType,
  UINTN                                SectionInstance,
  VOID                                 **Buffer,
  UINTN                                *BufferSize,
  UINT32                               *AuthenticationStatus
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  UINTN       CopySize;

  (void)This;
  if (++ReadCalls > ReadLimit) {
    Violation ("ReadSection called %u times for %u tables: instance %u never advances (F3)",
               ReadCalls, TableCount, (unsigned)SectionInstance);
    longjmp (AbortLoop, 1);
  }
  if ((NameGuid != &PcdDeviceAcpiTableStorageFile) || (SectionType != EFI_SECTION_RAW)) {
    Violation ("ReadSection for an unexpected file or section type");
  }
  if (SectionInstance >= TableCount) {
    return EFI_NOT_FOUND;
  }
  CopySize = TableSizes[SectionInstance];
  if (*Buffer != NULL) {
    POOL_BLOCK *Block = PoolFind (*Buffer);
    if (Block == NULL) {
      Violation ("ReadSection given a caller buffer the pool never returned");
      longjmp (AbortLoop, 1);
    }
    if (Block->Freed) {
      Violation ("ReadSection writes instance %u into a freed table buffer (F2)", (unsigned)SectionInstance);
    }
    if (*BufferSize < CopySize) {
      Status   = EFI_WARN_BUFFER_TOO_SMALL;
      CopySize = *BufferSize;
    }
    if (CopySize > Block->Size) {
      Violation ("ReadSection caller buffer smaller than its claimed size");
      longjmp (AbortLoop, 1);
    }
  } else {
    *Buffer = PoolAllocate (CopySize);
  }
  memcpy (*Buffer, Tables[SectionInstance], CopySize);
  *BufferSize           = TableSizes[SectionInstance];
  *AuthenticationStatus = 0;
  return Status;
}

/* ---- ACPI table protocol ------------------------------------------------- */
static EFI_STATUS EFIAPI
HostInstallAcpiTable (EFI_ACPI_TABLE_PROTOCOL *This, VOID *Buffer, UINTN Size, UINTN *Handle)
{
  POOL_BLOCK *Block = PoolFind (Buffer);
  char       *Hex;

  (void)This;
  if ((Block == NULL) || Block->Freed || (Size > Block->Size)) {
    Violation ("InstallAcpiTable from memory that is not a live table buffer");
  }
  Hex = calloc (1, Size * 2 + 1);
  for (UINTN Index = 0; Index < Size; Index++) {
    snprintf (Hex + Index * 2, 3, "%02x", ((UINT8 *)Buffer)[Index]);
  }
  if (InstalledCount < MAX_RECORDS) {
    Installed[InstalledCount++] = Hex;
  }
  *Handle = InstalledCount;
  return EFI_SUCCESS;
}

/* ---- ADT /chosen ---------------------------------------------------------- */
static UINT32      ChipId = 0x8142, BoardId = 0x24, CpufMarker = 1, MtpDisabled;
static bool        CpufMarkerPresent = true, CpufMarkerShort, MtpPolicyPresent;
static UINT16      CpufMarkerShortValue = 1;
static char        RejectOemTableId[9];
static const char  *UsbPolicy = "none";

static void *
dt_get_prop (const char *Device, const char *Prop, size_t *Size)
{
  if (strcmp (Device, "/chosen") != 0) {
    return NULL;
  }
  if (!strcmp (Prop, "aura,windows-cpufreq-v1") && CpufMarkerPresent) {
    if (CpufMarkerShort) {
      *Size = sizeof (CpufMarkerShortValue);
      return &CpufMarkerShortValue;
    }
    *Size = sizeof (CpufMarker);
    return &CpufMarker;
  }
  if (!strcmp (Prop, "chip-id")) {
    *Size = sizeof (ChipId);
    return &ChipId;
  }
  if (!strcmp (Prop, "board-id")) {
    *Size = sizeof (BoardId);
    return &BoardId;
  }
  if (!strcmp (Prop, "aura,windows-mtp-disabled") && MtpPolicyPresent) {
    *Size = sizeof (MtpDisabled);
    return &MtpDisabled;
  }
  return NULL;
}

/* ---- calls the loop makes that are outside the extracted text ------------ */
static UINT8
CalculateCheckSum8 (CONST UINT8 *Buffer, UINTN Size)
{
  UINT8 Sum = 0;

  for (UINTN Index = 0; Index < Size; Index++) {
    Sum = (UINT8)(Sum + Buffer[Index]);
  }
  return (UINT8)(0x100 - Sum);
}

static BOOLEAN
HostWithheldById (CONST EFI_ACPI_DESCRIPTION_HEADER *Table)
{
  return RejectOemTableId[0] && (memcmp (&Table->OemTableId, RejectOemTableId, 8) == 0);
}

static BOOLEAN
AppleN1TableAdmitted (IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size)
{
  (void)Size;
  return !HostWithheldById (Table);
}

static BOOLEAN
AppleG17GpuTableAdmitted (IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size)
{
  (void)Table;
  (void)Size;
  return TRUE;
}

static BOOLEAN
AppleJ813DisplayTableAdmitted (IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size)
{
  (void)Table;
  (void)Size;
  return TRUE;
}

/* The production policy rewrites the J813 DSDT through AmlLib and hands back a
 * new pool buffer, freeing the old one; failure leaves *Table untouched. */
static EFI_STATUS
AppleApplyUsbDisabledPolicy (IN OUT EFI_ACPI_COMMON_HEADER **Table)
{
  EFI_ACPI_DESCRIPTION_HEADER *Header = (EFI_ACPI_DESCRIPTION_HEADER *)*Table;
  UINT8                       *Updated;

  if ((Header->Signature != SIGNATURE_32 ('D', 'S', 'D', 'T')) ||
      (memcmp (&Header->OemTableId, "J813", 4) != 0) || !strcmp (UsbPolicy, "none"))
  {
    return EFI_SUCCESS;
  }
  if (!strcmp (UsbPolicy, "fail")) {
    return EFI_COMPROMISED_DATA;
  }
  Updated = PoolAllocate (Header->Length);
  memcpy (Updated, Header, Header->Length);
  Updated[Header->Length - 1] ^= 0xFF;
  gBS->FreePool (*Table);
  *Table = (EFI_ACPI_COMMON_HEADER *)Updated;
  return EFI_SUCCESS;
}

#include "helpers.inc"

static EFI_STATUS
RunDeviceTableLoop (EFI_ACPI_TABLE_PROTOCOL *AcpiTable, EFI_FIRMWARE_VOLUME2_PROTOCOL *FwVol)
{
  EFI_STATUS              Status       = EFI_SUCCESS;
  INTN                    Instance     = 0;
  EFI_ACPI_COMMON_HEADER  *CurrentTable = NULL;
  UINTN                   TableHandle  = 0;
  UINT32                  FvStatus;
  UINTN                   TableSize;
  UINTN                   Size;

  (void)TableHandle;
#include "loop.inc"
  return Status;
}

static const char *
StatusName (EFI_STATUS Status)
{
  static char Text[32];

  if (Status == EFI_SUCCESS) return "EFI_SUCCESS";
  if (Status == EFI_NOT_FOUND) return "EFI_NOT_FOUND";
  if (Status == EFI_ABORTED) return "EFI_ABORTED";
  snprintf (Text, sizeof (Text), "0x%llx", (unsigned long long)Status);
  return Text;
}

int
main (int Argc, char **Argv)
{
  EFI_FIRMWARE_VOLUME2_PROTOCOL  FwVol     = { HostReadSection };
  EFI_ACPI_TABLE_PROTOCOL        AcpiTable = { HostInstallAcpiTable };
  const char                     *Outcome  = "returned";
  EFI_STATUS                     Status    = EFI_SUCCESS;
  unsigned                       Live      = 0;
  FILE                           *File;
  UINT32                         Count, Length;

  if (Argc < 2 || (File = fopen (Argv[1], "rb")) == NULL ||
      fread (&Count, 4, 1, File) != 1 || Count > MAX_RECORDS)
  {
    fprintf (stderr, "usage: %s TABLES [marker=1|0|absent|short] [chip=N] [board=N] "
                     "[reject=OEMTABLE] [mtp=N] [usb=none|replace|fail]\n", Argv[0]);
    return 2;
  }
  for (TableCount = 0; TableCount < Count; TableCount++) {
    if (fread (&Length, 4, 1, File) != 1) return 2;
    Tables[TableCount]     = malloc (Length);
    TableSizes[TableCount] = Length;
    if (fread (Tables[TableCount], 1, Length, File) != Length) return 2;
  }
  fclose (File);
  for (int Index = 2; Index < Argc; Index++) {
    const char *Arg = Argv[Index];
    if (!strcmp (Arg, "marker=absent")) CpufMarkerPresent = false;
    else if (!strcmp (Arg, "marker=short")) CpufMarkerShort = true;
    else if (!strncmp (Arg, "marker=", 7)) CpufMarker = (UINT32)strtoul (Arg + 7, NULL, 0);
    else if (!strncmp (Arg, "chip=", 5)) ChipId = (UINT32)strtoul (Arg + 5, NULL, 0);
    else if (!strncmp (Arg, "board=", 6)) BoardId = (UINT32)strtoul (Arg + 6, NULL, 0);
    else if (!strncmp (Arg, "reject=", 7)) snprintf (RejectOemTableId, sizeof (RejectOemTableId), "%-8.8s", Arg + 7);
    else if (!strncmp (Arg, "mtp=", 4)) { MtpPolicyPresent = true; MtpDisabled = (UINT32)strtoul (Arg + 4, NULL, 0); }
    else if (!strncmp (Arg, "usb=", 4)) UsbPolicy = Arg + 4;
    else { fprintf (stderr, "unknown flag %s\n", Arg); return 2; }
  }
  ReadLimit = 4 * TableCount + 8;

  if (setjmp (AbortLoop) == 0) {
    Status = RunDeviceTableLoop (&AcpiTable, &FwVol);
  } else {
    Outcome = "aborted";
  }
  for (unsigned Index = 0; Index < PoolCount; Index++) {
    Live += !Pool[Index].Freed;
  }

  printf ("{\"outcome\":\"%s\",\"loop_status\":\"%s\",\"read_calls\":%u,\"allocations\":%u,"
          "\"live_allocations\":%u,\"installed\":[",
          Outcome, Outcome[0] == 'a' ? "aborted" : StatusName (Status), ReadCalls, PoolCount, Live);
  for (unsigned Index = 0; Index < InstalledCount; Index++) {
    printf ("%s\"%s\"", Index ? "," : "", Installed[Index]);
  }
  printf ("],\"violations\":[");
  for (unsigned Index = 0; Index < ViolationCount; Index++) {
    printf ("%s\"%s\"", Index ? "," : "", Violations[Index]);
  }
  printf ("]}\n");
  return 0;
}

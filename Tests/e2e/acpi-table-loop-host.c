/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host harness for AcpiPlatform's device-table loop.  loop.inc and
 * helpers.inc are cut verbatim from AcpiPlatform.c by acpi-table-loop.py, and
 * the G17, J813 display and unified-selection admission sources are compiled
 * verbatim beside this file; everything below models the firmware services
 * around them:
 *
 *   FV2 ReadSection  follows DxeCore FwVolReadSection: a NULL *Buffer is
 *                    allocated from pool, a non-NULL one is filled in place,
 *                    and *BufferSize returns the section size.  A truncating
 *                    fill (a warning the loop ignores before overrunning the
 *                    buffer) is reported as F2 and stops the run.
 *   gBS->FreePool    pool memory is never returned to the host allocator, so
 *   FreePool         a double free or a write into freed pool is observed and
 *                    reported instead of corrupting the harness.
 *   InstallAcpiTable copies the table, as AcpiTableDxe does.
 *   ADT              /chosen (cpufreq and unified markers, chip-id, board-id,
 *                    MTP policy), /arm-io/sgx regions, /arm-io/dcp, /disp0.
 *                    Missing properties print the "Missing DeviceTree prop"
 *                    warning dt_get_prop prints.
 *   GCD map          one system-memory range (boot 147's UEFI RAM).
 *
 * Output: one JSON line on stdout.
 */
#include <setjmp.h>
#include "acpi-table-loop-host.h"

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
#define MAX_LOG      256
static char     *Violations[MAX_RECORDS];
static unsigned ViolationCount;
static jmp_buf  AbortLoop;
static unsigned ReadCalls, ReadLimit, MarkerReads;
static char     *Installed[MAX_RECORDS];
static unsigned InstalledCount;
static char     *LogText[MAX_LOG];
static UINTN    LogLevel[MAX_LOG];
static unsigned LogCount;

void
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

/* Translate the EDK II specifiers the admission code uses to host printf. */
static void
HostFormat (char *Out, size_t OutSize, const char *Format)
{
  size_t Used = 0;

  while (*Format != '\0' && Used + 4 < OutSize) {
    if (Format[0] == '%' && Format[1] == 'a') {
      Out[Used++] = '%'; Out[Used++] = 's'; Format += 2;
    } else if (Format[0] == '%' && (Format[1] == 'l' || Format[1] == 'L') && Format[2] == 'x') {
      Out[Used++] = '%'; Out[Used++] = 'l'; Out[Used++] = 'l'; Out[Used++] = 'x'; Format += 3;
    } else if (Format[0] == '%' && Format[1] == 'r') {
      Out[Used++] = '%'; Out[Used++] = 'l'; Out[Used++] = 'l'; Out[Used++] = 'x'; Format += 2;
    } else {
      Out[Used++] = *Format++;
    }
  }
  Out[Used] = '\0';
}

void
HostDebugPrint (UINTN Level, const char *Format, ...)
{
  char    HostFmt[512];
  char    Text[512];
  va_list Args;
  char    *Nl;

  HostFormat (HostFmt, sizeof (HostFmt), Format);
  va_start (Args, Format);
  vsnprintf (Text, sizeof (Text), HostFmt, Args);
  va_end (Args);
  while ((Nl = strpbrk (Text, "\r\n\"\\")) != NULL) {
    *Nl = (*Nl == '"') ? '\'' : ((*Nl == '\\') ? '/' : ' ');
  }
  if (LogCount < MAX_LOG) {
    LogLevel[LogCount] = Level;
    LogText[LogCount++] = strdup (Text);
  }
}

UINTN
AsciiSPrint (CHAR8 *Buffer, UINTN Size, CONST CHAR8 *Format, ...)
{
  char    HostFmt[256];
  va_list Args;
  int     Count;

  HostFormat (HostFmt, sizeof (HostFmt), Format);
  va_start (Args, Format);
  Count = vsnprintf (Buffer, Size, HostFmt, Args);
  va_end (Args);
  return Count < 0 ? 0 : (UINTN)Count;
}

/* ---- pool model ---------------------------------------------------------- */
typedef struct { UINT8 *Base; UINTN Size; bool Freed; } POOL_BLOCK;
static POOL_BLOCK Pool[MAX_RECORDS * 8];
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
  Pool[PoolCount].Base  = calloc (1, Size ? Size : 1);
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
    Violation ("double free of a %u-byte buffer (F1)", (unsigned)Block->Size);
    longjmp (AbortLoop, 1);
  }
  Block->Freed = true;
  memset (Block->Base, 0xAF, Block->Size);
  return EFI_SUCCESS;
}

VOID
FreePool (VOID *Buffer)
{
  (void)HostFreePool (Buffer);
}

static HOST_BOOT_SERVICES  HostBs = { HostFreePool };
static HOST_BOOT_SERVICES  *gBS   = &HostBs;

/* ---- GCD memory map ------------------------------------------------------ */
static UINT64  RamBase = 0x1001AB80000ULL, RamTop = 0x103DC600000ULL;  /* boot 147 */

static EFI_STATUS EFIAPI
HostGetMemorySpaceMap (UINTN *Count, EFI_GCD_MEMORY_SPACE_DESCRIPTOR **Map)
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  *Out = PoolAllocate (3 * sizeof (*Out));

  Out[0] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){ 0, RamBase, 0, 0, EfiGcdMemoryTypeMemoryMappedIo, NULL, NULL };
  Out[1] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){ RamBase, RamTop - RamBase, 0, 0, EfiGcdMemoryTypeSystemMemory, NULL, NULL };
  Out[2] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){ RamTop, (1ULL << 42) - RamTop, 0, 0, EfiGcdMemoryTypeReserved, NULL, NULL };
  *Count = 3;
  *Map   = Out;
  return EFI_SUCCESS;
}

static HOST_DXE_SERVICES  HostDs = { HostGetMemorySpaceMap };
HOST_DXE_SERVICES         *gDS   = &HostDs;

/* ---- build-time PCDs (gpu-disp profile) ---------------------------------- */
BOOLEAN  HostPcd_PcdAppleG17GpuProbe        = TRUE;
BOOLEAN  HostPcd_PcdAppleJ813DisplayDevice  = TRUE;
BOOLEAN  HostPcd_PcdAppleJ813UnifiedDisplay = TRUE;
BOOLEAN  HostPcd_PcdAppleN1BusHandoff       = TRUE;

/* ---- storage file -------------------------------------------------------- */
static EFI_GUID PcdDeviceAcpiTableStorageFile = { 0x7E374E25, 0x8E01, 0x4FEE, { 0x87, 0xF2, 0x39, 0x0C, 0x23, 0xC6, 0x06, 0xCD } };
#define PcdGetPtr(x)  (&(x))
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
      /* FwVolReadSection truncates with a warning, which the loop does not
       * treat as an error: it then checksums and installs Length bytes from
       * a smaller buffer.  Firmware corrupts pool there; stop the model. */
      Violation ("ReadSection truncates instance %u into a %u-byte caller buffer the loop then overruns (F2)",
                 (unsigned)SectionInstance, (unsigned)*BufferSize);
      longjmp (AbortLoop, 1);
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

/* ---- ADT ------------------------------------------------------------------ */
struct HOST_DT_NODE { const char *Path; };
static struct HOST_DT_NODE  ChosenNode = { "/chosen" };

static UINT32      ChipId = 0x8142, BoardId = 0x24, CpufMarker = 1, MtpDisabled;
static bool        CpufMarkerPresent = true, CpufMarkerShort, MtpPolicyPresent;
static UINT16      CpufMarkerShortValue = 1;
static const char  *UnifiedMode = "absent";    /* absent | 1 | 0 | 2 | short | long */
static UINT32      UnifiedValue;
static UINT16      UnifiedShort = 1;
static UINT64      UnifiedLong = 1;
static bool        DcpPresent = true, Disp0Present = true;
static char        RejectOemTableId[9];
static const char  *UsbPolicy = "none";
static const char  *SgxNames[6] = { "gfx-handoff", "gpu-region", "gfx-shared-region",
                                    "gfx-shared-l2-region", "gfx-data", "gfx1-data" };
/* Boot 147's AURA8170 ranges 3-8 as Windows listed them (GPU.asl order). */
static UINT64      SgxBase[6] = { 0x103fff30000ULL, 0x103fffb8000ULL, 0x103fff38000ULL,
                                  0x103fff34000ULL, 0x1000249c000ULL, 0x100025d0000ULL };
static UINT64      SgxSize[6] = { 0x4000, 0x4000, 0x80000, 0x4000, 0x134000, 0x130000 };
static const char  Compatible[] = "host,model";

dt_node_t *
dt_get (const char *Name)
{
  if (strcmp (Name, "/chosen") == 0) {
    return &ChosenNode;
  }
  HostDebugPrint (DEBUG_WARN, "Missing DeviceTree node: %a\n", Name);
  return NULL;
}

void *
dt_prop (dt_node_t *Node, const char *Key, size_t *Length)
{
  if ((Node != &ChosenNode) || (strcmp (Key, "aura,windows-unified-display-v1") != 0)) {
    return NULL;
  }
  MarkerReads++;
  if (strcmp (UnifiedMode, "absent") == 0) {
    return NULL;
  }
  if (strcmp (UnifiedMode, "short") == 0) {
    *Length = sizeof (UnifiedShort);
    return &UnifiedShort;
  }
  if (strcmp (UnifiedMode, "long") == 0) {
    *Length = sizeof (UnifiedLong);
    return &UnifiedLong;
  }
  UnifiedValue = (UINT32)strtoul (UnifiedMode, NULL, 0);
  *Length      = sizeof (UnifiedValue);
  return &UnifiedValue;
}

static void *
ChosenProp (const char *Prop, size_t *Size)
{
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
  if (!strcmp (Prop, "aura,windows-unified-display-v1")) {
    return dt_prop (&ChosenNode, Prop, Size);
  }
  return NULL;
}

static void *
SgxProp (const char *Prop, size_t *Size)
{
  char Name[64];

  for (unsigned Index = 0; Index < 6; Index++) {
    snprintf (Name, sizeof (Name), "%s-base", SgxNames[Index]);
    if (!strcmp (Prop, Name)) {
      *Size = sizeof (UINT64);
      return &SgxBase[Index];
    }
    snprintf (Name, sizeof (Name), "%s-size", SgxNames[Index]);
    if (!strcmp (Prop, Name)) {
      *Size = sizeof (UINT64);
      return &SgxSize[Index];
    }
  }
  return NULL;
}

void *
dt_get_prop (const char *Device, const char *Prop, size_t *Size)
{
  void  *Value = NULL;
  bool  Node   = true;

  if (!strcmp (Device, "/chosen")) {
    Value = ChosenProp (Prop, Size);
  } else if (!strcmp (Device, "/arm-io/sgx")) {
    Value = SgxProp (Prop, Size);
  } else if (!strcmp (Device, "/arm-io/dcp") || !strcmp (Device, "/arm-io/disp0")) {
    Node = !strcmp (Device, "/arm-io/dcp") ? DcpPresent : Disp0Present;
    if (Node && !strcmp (Prop, "compatible")) {
      *Size = sizeof (Compatible);
      Value = (void *)Compatible;
    }
  } else {
    Node = false;
  }
  if (!Node) {
    HostDebugPrint (DEBUG_WARN, "Missing DeviceTree node: %a\n", Device);
  }
  if (Value == NULL) {
    HostDebugPrint (DEBUG_WARN, "Missing DeviceTree prop: %a\n", Prop);
  }
  return Value;
}

/* ---- calls the loop makes that are outside the compiled sources --------- */
static UINT8
CalculateCheckSum8 (CONST UINT8 *Buffer, UINTN Size)
{
  UINT8 Sum = 0;

  for (UINTN Index = 0; Index < Size; Index++) {
    Sum = (UINT8)(Sum + Buffer[Index]);
  }
  return (UINT8)(0x100 - Sum);
}

/* The real N1 admission reads the controller over MMIO; modeled by OEM id. */
static BOOLEAN
AppleN1TableAdmitted (IN CONST EFI_ACPI_DESCRIPTION_HEADER *Table, IN UINTN Size)
{
  (void)Size;
  return !(RejectOemTableId[0] && (memcmp (&Table->OemTableId, RejectOemTableId, 8) == 0));
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
    fprintf (stderr, "usage: %s TABLES [marker=1|0|absent|short] [chip=N] [board=N] [reject=OEMTABLE] "
                     "[mtp=N] [usb=none|replace|fail] [unified=absent|1|0|2|short|long] [gate=0|1] "
                     "[dcp=0] [disp0=0] [region=I:BASE:SIZE]\n", Argv[0]);
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
    else if (!strncmp (Arg, "unified=", 8)) UnifiedMode = Arg + 8;
    else if (!strncmp (Arg, "gate=", 5)) HostPcd_PcdAppleJ813UnifiedDisplay = Arg[5] == '1';
    else if (!strcmp (Arg, "dcp=0")) DcpPresent = false;
    else if (!strcmp (Arg, "disp0=0")) Disp0Present = false;
    else if (!strncmp (Arg, "region=", 7)) {
      char           *End;
      unsigned long  Region = strtoul (Arg + 7, &End, 0);
      if (Region > 5 || *End != ':') return 2;
      SgxBase[Region] = strtoull (End + 1, &End, 0);
      if (*End != ':') return 2;
      SgxSize[Region] = strtoull (End + 1, NULL, 0);
    }
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
          "\"live_allocations\":%u,\"marker_reads\":%u,\"installed\":[",
          Outcome, Outcome[0] == 'a' ? "aborted" : StatusName (Status), ReadCalls, PoolCount, Live,
          MarkerReads);
  for (unsigned Index = 0; Index < InstalledCount; Index++) {
    printf ("%s\"%s\"", Index ? "," : "", Installed[Index]);
  }
  printf ("],\"violations\":[");
  for (unsigned Index = 0; Index < ViolationCount; Index++) {
    printf ("%s\"%s\"", Index ? "," : "", Violations[Index]);
  }
  printf ("],\"log\":[");
  for (unsigned Index = 0; Index < LogCount; Index++) {
    printf ("%s[%llu,\"%s\"]", Index ? "," : "", (unsigned long long)LogLevel[Index], LogText[Index]);
  }
  printf ("]}\n");
  return 0;
}

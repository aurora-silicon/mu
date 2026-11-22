/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PEI stage of Tests/e2e/j813-gpu-pool.py: runs the production T8142
 * MemoryPeim (MemoryInitPeiLib.c of the revision under test, compiled
 * verbatim) over a J813-shaped guest RAM at its real physical addresses and
 * prints the HOB list it leaves as one JSON line.
 *
 * Modeled, not reimplemented: the HOB store (resource, allocation and GUID
 * HOBs kept in build order), the ADT (/chosen with the unified marker,
 * chip-id and board-id; /chosen/carveout-memory-map), the PCDs, the MMU
 * installation and PEI's page allocator.  RAM is a lazily zero-filled 16 GiB
 * mapping at 0x10000000000, the J813's DRAM, so MemoryPeim's reads of the
 * copied boot arguments (fixed PCD 0x10220000000) and of the FD tail land on
 * real addresses.  ASan cannot shadow that address range, so this host is
 * built with UBSan only, as memory-peim.py builds its MTP cases.
 */
#include "memory-peim-host.h"
#include <Library/AppleDTLib.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

#define DRAM_BASE  0x10000000000ULL
#define DRAM_SIZE  0x400000000ULL

UINT64   PcdSystemMemoryBase, PcdSystemMemorySize, PcdFdBaseAddress, PcdFdSize;
UINT64   PcdBootArgsPointer, PcdAdtPointer, PcdFrameBufferAddress, PcdFrameBufferSize;
UINT32   PcdCoreCount;
BOOLEAN  HostPcd_PcdAppleJ813GpuLocalPool;
BOOLEAN  HostPcd_PcdAppleJ813UnifiedDisplay = TRUE;

/* ---- HOB store ----------------------------------------------------------- */
static EFI_HOB_RESOURCE_DESCRIPTOR  Resources[64];
static size_t                       ResourceCount;
static struct { UINT64 Base, Size; int Type; } Allocations[16];
static size_t                       AllocationCount;
static struct { EFI_GUID Guid; UINT8 Data[256]; UINTN Size; } Guids[8];
static size_t                       GuidCount;
static bool                         MmuInstalled;
static ARM_MEMORY_REGION_DESCRIPTOR MemoryMap[128];

void *GetHobList (void) { return Resources; }

void *
GetNextHob (int Type, void *Hob)
{
  assert (Type == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR);
  return (EFI_HOB_RESOURCE_DESCRIPTOR *)Hob < Resources + ResourceCount ? Hob : NULL;
}

void
BuildResourceDescriptorHob (int Type, UINT64 Attributes, UINT64 Base, UINT64 Size)
{
  assert (ResourceCount < ARRAY_SIZE (Resources));
  Resources[ResourceCount++] = (EFI_HOB_RESOURCE_DESCRIPTOR){ Type, Attributes, Base, Size };
}

void
BuildMemoryAllocationHob (UINT64 Base, UINT64 Size, int Type)
{
  assert (AllocationCount < ARRAY_SIZE (Allocations));
  Allocations[AllocationCount].Base = Base;
  Allocations[AllocationCount].Size = Size;
  Allocations[AllocationCount++].Type = Type;
}

void *
BuildGuidDataHob (const EFI_GUID *Guid, const void *Data, UINTN Size)
{
  if ((GuidCount == ARRAY_SIZE (Guids)) || (Size > sizeof (Guids[0].Data))) {
    return NULL;
  }
  Guids[GuidCount].Guid = *Guid;
  memcpy (Guids[GuidCount].Data, Data, Size);
  Guids[GuidCount].Size = Size;
  return Guids[GuidCount++].Data;
}

void *
AllocatePages (UINTN Pages)
{
  assert (Pages <= sizeof (MemoryMap) / EFI_PAGE_SIZE);
  return MemoryMap;
}

RETURN_STATUS
ArmConfigureMmu (ARM_MEMORY_REGION_DESCRIPTOR *Table, void **Base, UINTN *Size)
{
  assert (Table == MemoryMap);
  MmuInstalled = true;
  *Base = Table;
  *Size = 4096;
  return 0;
}

UINTN
AsciiSPrint (char *Str, UINTN Size, const char *Format, ...)
{
  va_list Args;
  int     Count;

  va_start (Args, Format);
  Count = vsnprintf (Str, Size, Format, Args);
  va_end (Args);
  return Count < 0 ? 0 : (UINTN)Count;
}

void BuildMemoryTypeInformationHob (void) { abort (); }

/* ---- ADT ------------------------------------------------------------------ */
static dt_node_t    Chosen, CarveoutMap;
static const char  *UnifiedMode = "absent";          /* absent | 1 | 0 | 2 | short | long */
static UINT32       UnifiedValue;
static UINT16       UnifiedShort = 1;
static UINT64       UnifiedLong = 1;
static UINT32       ChipId = 0x8142, BoardId = 0x24;
static const char  *CarveoutMode = "absent";         /* absent | above | top | malformed */
static UINT64       Carveout2[2], Carveout4[2];
static UINT32       CarveoutShort[2];
static unsigned     MarkerReads;

dt_node_t *
dt_get (const char *Name)
{
  if (!strcmp (Name, "/chosen")) {
    return &Chosen;
  }
  if (!strcmp (Name, "/chosen/carveout-memory-map") && strcmp (CarveoutMode, "absent")) {
    return &CarveoutMap;
  }
  return NULL;
}

void *
dt_prop (dt_node_t *Node, const char *Key, size_t *Length)
{
  if (Node == &Chosen) {
    if (!strcmp (Key, "aura,windows-unified-display-v1")) {
      MarkerReads++;
      if (!strcmp (UnifiedMode, "absent")) {
        return NULL;
      }
      if (!strcmp (UnifiedMode, "short")) {
        *Length = sizeof (UnifiedShort);
        return &UnifiedShort;
      }
      if (!strcmp (UnifiedMode, "long")) {
        *Length = sizeof (UnifiedLong);
        return &UnifiedLong;
      }
      UnifiedValue = (UINT32)strtoul (UnifiedMode, NULL, 0);
      *Length = sizeof (UnifiedValue);
      return &UnifiedValue;
    }
    if (!strcmp (Key, "chip-id")) {
      *Length = sizeof (ChipId);
      return &ChipId;
    }
    if (!strcmp (Key, "board-id")) {
      *Length = sizeof (BoardId);
      return &BoardId;
    }
    return NULL;
  }
  if (Node == &CarveoutMap) {
    if (!strcmp (CarveoutMode, "malformed") && !strcmp (Key, "region-id-2")) {
      *Length = sizeof (CarveoutShort);
      return CarveoutShort;
    }
    if (!strcmp (Key, "region-id-2")) {
      *Length = sizeof (Carveout2);
      return Carveout2;
    }
    if (!strcmp (Key, "region-id-4")) {
      *Length = sizeof (Carveout4);
      return Carveout4;
    }
    return NULL;
  }
  abort ();
}

void *
dt_node_prop (dt_node_t *Node, const char *Name, size_t *Length)
{
  (void)Length;
  /* The J813 MTP policy property is absent: the legacy handoff is admitted. */
  assert (Node == &Chosen && !strcmp (Name, "aura,windows-mtp-disabled"));
  return NULL;
}

EFI_STATUS MemoryPeim (EFI_PHYSICAL_ADDRESS Base, UINT64 Size);

static UINT64
Number (const char *Text)
{
  return strtoull (Text, NULL, 0);
}

int
main (int Argc, char **Argv)
{
  /* Boot 147's guest RAM (Aura heap_top .. iBoot phys_base + mem_size). */
  UINT64      RamBase = 0x1001AB80000ULL, RamTop = 0x103DC600000ULL;
  UINT64      FdBase = 0, UefiBase, UefiSize = 0x8000000ULL;
  void        *Arena;
  EFI_STATUS  Status;

  for (int Index = 1; Index < Argc; Index++) {
    const char *Arg = Argv[Index];
    if (!strncmp (Arg, "marker=", 7)) UnifiedMode = Arg + 7;
    else if (!strncmp (Arg, "pool=", 5)) HostPcd_PcdAppleJ813GpuLocalPool = Arg[5] == '1';
    else if (!strncmp (Arg, "unified-pcd=", 12)) HostPcd_PcdAppleJ813UnifiedDisplay = Arg[12] == '1';
    else if (!strncmp (Arg, "chip=", 5)) ChipId = (UINT32)Number (Arg + 5);
    else if (!strncmp (Arg, "board=", 6)) BoardId = (UINT32)Number (Arg + 6);
    else if (!strncmp (Arg, "ram-base=", 9)) RamBase = Number (Arg + 9);
    else if (!strncmp (Arg, "ram-top=", 8)) RamTop = Number (Arg + 8);
    else if (!strncmp (Arg, "fd-base=", 8)) FdBase = Number (Arg + 8);
    else if (!strncmp (Arg, "carveouts=", 10)) CarveoutMode = Arg + 10;
    else if (!strncmp (Arg, "carveout2=", 10)) {
      char *End;
      Carveout2[0] = strtoull (Arg + 10, &End, 0);
      Carveout2[1] = strtoull (End + 1, NULL, 0);
    } else if (!strncmp (Arg, "carveout4=", 10)) {
      char *End;
      Carveout4[0] = strtoull (Arg + 10, &End, 0);
      Carveout4[1] = strtoull (End + 1, NULL, 0);
    } else {
      fprintf (stderr, "unknown flag %s\n", Arg);
      return 2;
    }
  }

#ifdef __APPLE__
  mach_vm_address_t Address = DRAM_BASE;
  Arena = mach_vm_allocate (mach_task_self (), &Address, DRAM_SIZE, VM_FLAGS_FIXED) == KERN_SUCCESS ?
          (void *)(UINTN)Address : MAP_FAILED;
#else
  Arena = mmap ((void *)(UINTN)DRAM_BASE, DRAM_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANON | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
#endif
  if (Arena != (void *)(UINTN)DRAM_BASE) {
    fprintf (stderr, "cannot map the J813 DRAM range at its physical address\n");
    return 9;
  }

  /* Aura's raw loader: the FD starts 64-KiB aligned above the 16-MiB gap,
   * the relocated ADT and TrustCache (predict_windows_layout). */
  if (FdBase == 0) {
    FdBase = ALIGN_VALUE (RamBase + 0x1000000ULL + 0x80000ULL + 0x10000ULL, 0x10000ULL);
  }
  PcdSystemMemoryBase   = RamBase;
  PcdSystemMemorySize   = RamTop - RamBase;
  PcdFdBaseAddress      = FdBase;
  PcdFdSize             = 0x1E00000ULL;                  /* MacBookAir2026.fdf */
  PcdBootArgsPointer    = 0x10220000000ULL;              /* T8142FamilyPkg.dsc.inc */
  PcdAdtPointer         = 0x10220004000ULL;
  PcdFrameBufferAddress = 0;
  PcdFrameBufferSize    = 0;
  PcdCoreCount          = 0;
  UefiBase              = RamTop - UefiSize;             /* PrePi: top of RAM */
  ((struct boot_args *)(UINTN)PcdBootArgsPointer)->devtree_size = 0x80000;

  Status = MemoryPeim (UefiBase, UefiSize);

  printf ("{\"status\":%d,\"mmu\":%s,\"marker_reads\":%u,\"uefi\":[%llu,%llu],\"fd\":[%llu,%llu],"
          "\"ram\":[%llu,%llu],\"resources\":[",
          Status, MmuInstalled ? "true" : "false", MarkerReads,
          (unsigned long long)UefiBase, (unsigned long long)UefiSize,
          (unsigned long long)PcdFdBaseAddress, (unsigned long long)PcdFdSize,
          (unsigned long long)RamBase, (unsigned long long)RamTop);
  for (size_t Index = 0; Index < ResourceCount; Index++) {
    printf ("%s[%d,%llu,%llu,%llu]", Index ? "," : "", Resources[Index].ResourceType,
            (unsigned long long)Resources[Index].ResourceAttribute,
            (unsigned long long)Resources[Index].PhysicalStart,
            (unsigned long long)Resources[Index].ResourceLength);
  }
  printf ("],\"allocations\":[");
  for (size_t Index = 0; Index < AllocationCount; Index++) {
    printf ("%s[%llu,%llu,%d]", Index ? "," : "", (unsigned long long)Allocations[Index].Base,
            (unsigned long long)Allocations[Index].Size, Allocations[Index].Type);
  }
  printf ("],\"guid_hobs\":[");
  for (size_t Index = 0; Index < GuidCount; Index++) {
    const UINT8 *Guid = (const UINT8 *)&Guids[Index].Guid;
    printf ("%s[\"", Index ? "," : "");
    for (size_t Byte = 0; Byte < sizeof (EFI_GUID); Byte++) {
      printf ("%02x", Guid[Byte]);
    }
    printf ("\",\"");
    for (size_t Byte = 0; Byte < Guids[Index].Size; Byte++) {
      printf ("%02x", Guids[Index].Data[Byte]);
    }
    printf ("\"]");
  }
  printf ("]}\n");
  return 0;
}

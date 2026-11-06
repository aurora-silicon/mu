/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "memory-peim-host.h"
#include <Library/AppleDTLib.h>
#include <AppendedRamdisk.h>

UINT64 PcdSystemMemoryBase, PcdSystemMemorySize, PcdFdBaseAddress, PcdFdSize;
UINT64 PcdBootArgsPointer, PcdAdtPointer, PcdFrameBufferAddress, PcdFrameBufferSize;
UINT32 PcdCoreCount;
static EFI_HOB_RESOURCE_DESCRIPTOR hobs[64];
static size_t count;
static bool installed_mmu;
static unsigned allocation_count;
static ARM_MEMORY_REGION_DESCRIPTOR memory_map[128];
static UINT64 allocation_base, allocation_size;

void *GetHobList(void) { return hobs; }
void *GetNextHob(int type, void *hob) {
  assert(type == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR);
  return (EFI_HOB_RESOURCE_DESCRIPTOR *)hob < hobs + count ? hob : NULL;
}
void BuildResourceDescriptorHob(int type, UINT64 attr, UINT64 base, UINT64 size) {
  assert(count < ARRAY_SIZE(hobs));
  hobs[count++] = (EFI_HOB_RESOURCE_DESCRIPTOR){type, attr, base, size};
}
void BuildMemoryAllocationHob(UINT64 base, UINT64 size, int type) {
  assert(type == EfiBootServicesData);
  allocation_count++; allocation_base=base; allocation_size=size;
}
void *BuildGuidDataHob(const EFI_GUID *guid, const void *data, UINTN size) {
  (void)guid; (void)size; return (void *)data;
}
void *AllocatePages(UINTN pages) {
  assert(pages <= sizeof(memory_map) / EFI_PAGE_SIZE); return memory_map;
}
RETURN_STATUS ArmConfigureMmu(ARM_MEMORY_REGION_DESCRIPTOR *table, void **base, UINTN *size) {
  assert(table == memory_map); installed_mmu=true; *base=table; *size=4096; return 0;
}
UINTN AsciiSPrint(char *str, UINTN size, const char *format, ...) {
  va_list args; va_start(args,format); int n=vsnprintf(str,size,format,args); va_end(args); return n;
}
dt_node_t *dt_get(const char *name) { (void)name; return NULL; }
void *dt_node_prop(dt_node_t *node, const char *name, size_t *length) {
  (void)node; (void)name; (void)length; abort();
}
void BuildMemoryTypeInformationHob(void) { abort(); }
EFI_STATUS MemoryPeim(EFI_PHYSICAL_ADDRESS base, UINT64 size);

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *name=argv[1];
  void *arena=aligned_alloc(4096, 64U*1024*1024);
  assert(arena); memset(arena,0,64U*1024*1024);
  UINT64 base=(UINTN)arena;
  PcdSystemMemoryBase=base; PcdSystemMemorySize=64U*1024*1024;
  PcdFdBaseAddress=base+2U*1024*1024; PcdFdSize=1024*1024;
  PcdBootArgsPointer=base+32U*1024*1024; PcdAdtPointer=PcdBootArgsPointer+0x4000;
  UINT64 uefi_base=base+48U*1024*1024, uefi_size=16U*1024*1024;
  bool expected_ok=!strcmp(name,"normal") || !strcmp(name,"at-base") || !strcmp(name,"at-end");
  if (!strcmp(name,"at-base")) {PcdBootArgsPointer=base; PcdAdtPointer=base+0x4000;}
  if (!strcmp(name,"at-end")) {
    PcdBootArgsPointer=base+PcdSystemMemorySize-0x84000;
    PcdAdtPointer=PcdBootArgsPointer+0x4000;
    uefi_base=base+40U*1024*1024; uefi_size=8U*1024*1024;
  }
  struct boot_args *ba=(void *)(UINTN)PcdBootArgsPointer;
  ba->devtree_size=0x80000;
  if (!strcmp(name,"zero-adt")) ba->devtree_size=0;
  if (!strcmp(name,"oversize-adt")) ba->devtree_size=0x400001;
  if (!strcmp(name,"unaligned")) PcdAdtPointer++;
  if (!strcmp(name,"outside-ram")) PcdSystemMemorySize=16U*1024*1024;
  if (!strcmp(name,"overlap-fd")) {PcdFdBaseAddress=PcdBootArgsPointer;}
  if (!strcmp(name,"overlap-uefi")) {uefi_base=PcdBootArgsPointer; uefi_size=8U*1024*1024;}
  if (!strcmp(name,"overlap-existing-hole")) {
    BuildResourceDescriptorHob(EFI_RESOURCE_SYSTEM_MEMORY,63,base,32U*1024*1024);
    BuildResourceDescriptorHob(EFI_RESOURCE_MEMORY_RESERVED,0,PcdBootArgsPointer,0x100000);
    BuildResourceDescriptorHob(EFI_RESOURCE_SYSTEM_MEMORY,63,PcdBootArgsPointer+0x100000,31U*1024*1024);
  }
  if (!strcmp(name,"overlap-appended")) {
    NTASI_APPENDED_RAMDISK_HEADER *h=(void *)(UINTN)(PcdFdBaseAddress+PcdFdSize);
    h->Signature=NTASI_APPENDED_RAMDISK_SIGNATURE;
    h->Version=NTASI_APPENDED_RAMDISK_VERSION;
    h->HeaderSize=4096; h->ImageSize=32U*1024*1024;
    h->HeaderCrc32=NtasiAppendedRamdiskCrc32(h,offsetof(NTASI_APPENDED_RAMDISK_HEADER,HeaderCrc32));
  }
  UINT64 scratch_start=PcdBootArgsPointer, scratch_end=ALIGN_VALUE(PcdAdtPointer+ba->devtree_size,4096);
  EFI_STATUS result=MemoryPeim(uefi_base,uefi_size);
  if (expected_ok) {
    if (result != EFI_SUCCESS || !installed_mmu) return 2;
    bool reserved=false;
    UINT64 total=0;
    for (size_t i=0;i<count;i++) {
      UINT64 end=hobs[i].PhysicalStart+hobs[i].ResourceLength;
      total+=hobs[i].ResourceLength;
      if (hobs[i].ResourceType == EFI_RESOURCE_SYSTEM_MEMORY &&
          hobs[i].PhysicalStart < scratch_end && scratch_start < end) {
        fprintf(stderr,"copied handoff remains allocatable in case %s\n",name); return 3;
      }
      if (hobs[i].ResourceType == EFI_RESOURCE_MEMORY_RESERVED &&
          hobs[i].PhysicalStart == scratch_start && end == scratch_end) reserved=true;
    }
    if (!reserved || total != PcdSystemMemorySize || allocation_count != 1 ||
        allocation_base != PcdFdBaseAddress || allocation_size != PcdFdSize) return 4;
  } else if (result == EFI_SUCCESS || installed_mmu) {
    fprintf(stderr,"unsafe handoff accepted: %s\n",name); return 5;
  }
  printf("{\"case\":\"%s\",\"result\":\"passed\",\"resource_hobs\":%zu}\n",name,count);
  free(arena); return 0;
}

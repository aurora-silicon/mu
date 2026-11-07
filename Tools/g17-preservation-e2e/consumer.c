/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Executes production code; substitutes only host HOB/allocation/EFI services. */
#include <Uefi.h>
#include <PiPei.h>
#include <Library/AppleDTLib.h>
#include <Library/AuraG17PreservationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UINT64 PcdFrameBufferAddress, PcdFrameBufferSize;
UINT64 PcdFdBaseAddress, PcdFdSize=0x1e00000, PcdSystemMemoryUefiRegionSize=0x8000000;
UINT64 PcdBootArgsPointer, PcdAdtPointer, PcdSystemMemoryBase, PcdSystemMemorySize;
static union { UINT64 alignment; unsigned char bytes[65536]; } hobs;
static size_t used;
static EFI_EVENT_NOTIFY ready;
static int map_fault, ready_map_fault, duplicate_gap, halt_status=22;
static UINT64 expected_boot_dest, expected_adt_dest;
static struct boot_args copied_args;
static void *copied_adt;
static unsigned copy_calls;
BOOLEAN EarlySetup(VOID *, UINT64 *, UINT64 *, VOID *, VOID *);
EFI_STATUS ReserveG17Handoff(UINT64, UINT64);
EFI_STATUS AuraG17VerifyPreservation(VOID);

VOID EFIAPI DebugPrint(UINTN Level, CONST CHAR8 *Format, ...) { (void)Level; (void)Format; }
BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(UINTN Level) { (void)Level; return FALSE; }
BOOLEAN EFIAPI DebugAssertEnabled(VOID) { return TRUE; }
VOID EFIAPI DebugAssert(CONST CHAR8 *File, UINTN Line, CONST CHAR8 *Description)
{ fprintf(stderr,"assert %s:%zu %s\n",File,(size_t)Line,Description); abort(); }
VOID EFIAPI CpuDeadLoop(VOID) { fprintf(stderr,"production fail-closed halt\n"); exit(halt_status); }
INTN EFIAPI AsciiStrCmp(CONST CHAR8 *a, CONST CHAR8 *b) { return strcmp(a,b); }
INTN EFIAPI AsciiStrnCmp(CONST CHAR8 *a, CONST CHAR8 *b, UINTN n) { return strncmp(a,b,n); }
UINTN EFIAPI AsciiStrLen(CONST CHAR8 *a) { return strlen(a); }
CHAR8 *EFIAPI AsciiStrStr(CONST CHAR8 *a, CONST CHAR8 *b) { return (CHAR8 *)strstr(a,b); }
VOID *EFIAPI CopyMem(VOID *dest, CONST VOID *source, UINTN length)
{
  ++copy_calls;
  if((uintptr_t)dest==expected_boot_dest) {
    assert(length==sizeof(copied_args));memcpy(&copied_args,source,length);
  } else {
    assert((uintptr_t)dest==expected_adt_dest && length<=0x400000);
    copied_adt=malloc(length);assert(copied_adt);memcpy(copied_adt,source,length);
  }
  return dest;
}
int HostDtCheck(void *memory, size_t length, size_t *parsed)
{
  assert((uintptr_t)memory==expected_adt_dest && copied_adt);
  return dt_check(copied_adt,length,parsed);
}
INTN EFIAPI CompareMem(CONST VOID *a, CONST VOID *b, UINTN n) { return memcmp(a,b,n); }
VOID *EFIAPI AllocatePool(UINTN n) { return malloc(n); }
VOID EFIAPI FreePool(VOID *p) { free(p); }
VOID *EFIAPI GetHobList(VOID) { return hobs.bytes; }
static VOID *new_hob(UINT16 type, size_t n)
{
  EFI_HOB_GENERIC_HEADER *h;
  n = (n+7)&~7ULL;
  assert(n <= UINT16_MAX && used+n+sizeof(*h) <= sizeof(hobs.bytes));
  h = (void *)(hobs.bytes+used); memset(h,0,n); h->HobType=type; h->HobLength=(UINT16)n;
  used+=n; ((EFI_HOB_GENERIC_HEADER *)(hobs.bytes+used))->HobType=EFI_HOB_TYPE_END_OF_HOB_LIST;
  return h;
}
VOID *EFIAPI GetNextHob(UINT16 type, CONST VOID *start)
{
  EFI_PEI_HOB_POINTERS h; h.Raw=(void *)start;
  for (; !END_OF_HOB_LIST(h); h.Raw=GET_NEXT_HOB(h)) if (GET_HOB_TYPE(h)==type) return h.Raw;
  return NULL;
}
VOID EFIAPI BuildResourceDescriptorHob(EFI_RESOURCE_TYPE type, EFI_RESOURCE_ATTRIBUTE_TYPE attrs,
                                     EFI_PHYSICAL_ADDRESS base, UINT64 length)
{
  EFI_HOB_RESOURCE_DESCRIPTOR *h=new_hob(EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,sizeof(*h));
  h->ResourceType=type; h->ResourceAttribute=attrs; h->PhysicalStart=base; h->ResourceLength=length;
}
VOID EFIAPI BuildMemoryAllocationHob(EFI_PHYSICAL_ADDRESS base, UINT64 length, EFI_MEMORY_TYPE type)
{
  EFI_HOB_MEMORY_ALLOCATION *h=new_hob(EFI_HOB_TYPE_MEMORY_ALLOCATION,sizeof(*h));
  h->AllocDescriptor.MemoryBaseAddress=base; h->AllocDescriptor.MemoryLength=length;
  h->AllocDescriptor.MemoryType=type;
}
VOID *EFIAPI BuildGuidDataHob(CONST EFI_GUID *guid, VOID *data, UINTN n)
{
  EFI_HOB_GUID_TYPE *h=new_hob(EFI_HOB_TYPE_GUID_EXTENSION,sizeof(*h)+n);
  h->Name=*guid; memcpy(h+1,data,n); return h+1;
}
VOID *EFIAPI GetNextGuidHob(CONST EFI_GUID *guid, CONST VOID *start)
{
  EFI_PEI_HOB_POINTERS h; h.Raw=(void *)start;
  for (; !END_OF_HOB_LIST(h); h.Raw=GET_NEXT_HOB(h))
    if (GET_HOB_TYPE(h)==EFI_HOB_TYPE_GUID_EXTENSION && !memcmp(&h.Guid->Name,guid,sizeof(*guid))) return h.Raw;
  return NULL;
}
VOID *EFIAPI GetFirstGuidHob(CONST EFI_GUID *guid) { return GetNextGuidHob(guid,GetHobList()); }
static EFI_STATUS EFIAPI get_map(UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key,
                                UINTN *stride, UINT32 *version)
{
  EFI_PEI_HOB_POINTERS h; size_t n=0; UINTN capacity=*size; int duplicated=0;
  *stride=sizeof(*map); *key=1; *version=EFI_MEMORY_DESCRIPTOR_VERSION;
  for (h.Raw=GetHobList(); !END_OF_HOB_LIST(h); h.Raw=GET_NEXT_HOB(h)) {
    if (GET_HOB_TYPE(h)!=EFI_HOB_TYPE_RESOURCE_DESCRIPTOR || !h.ResourceDescriptor->ResourceLength) continue;
    if ((n+1)*sizeof(*map)<=capacity && map) {
      memset(&map[n],0,sizeof(*map)); map[n].PhysicalStart=h.ResourceDescriptor->PhysicalStart;
      map[n].NumberOfPages=h.ResourceDescriptor->ResourceLength/EFI_PAGE_SIZE;
      map[n].Type=h.ResourceDescriptor->ResourceType==EFI_RESOURCE_MEMORY_RESERVED ?
                  EfiReservedMemoryType : EfiConventionalMemory;
      if (map_fault && map[n].Type==EfiReservedMemoryType) map[n].Type=EfiBootServicesData;
    }
    if(duplicate_gap && !duplicated && h.ResourceDescriptor->ResourceType==EFI_RESOURCE_MEMORY_RESERVED) {
      if(map && (n+2)*sizeof(*map)<=capacity) {
        map[n].NumberOfPages/=2; map[n+1]=map[n];
      }
      ++n;duplicated=1;
    }
    ++n;
  }
  *size=n*sizeof(*map); return capacity<*size || !map ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS;
}
static EFI_BOOT_SERVICES services={.GetMemoryMap=get_map};
EFI_BOOT_SERVICES *gBS=&services;
EFI_STATUS EFIAPI EfiCreateEventReadyToBootEx(EFI_TPL tpl, EFI_EVENT_NOTIFY notify,
                                            VOID *context, EFI_EVENT *event)
{ (void)tpl;(void)context;ready=notify;*event=(void *)1;return EFI_SUCCESS; }

static uint64_t number(const char *s)
{
  char *end; unsigned long long n; errno=0;n=strtoull(s,&end,0);
  if(errno || !*s || *end){fprintf(stderr,"invalid integer\n");exit(2);}return n;
}
int main(int argc,char **argv)
{
  FILE *f,*out; long length; unsigned char *adt; size_t off=0, checked=0;
  struct boot_args args={0}; UINTN record_size; CONST VOID *record; EFI_STATUS status;
  uint64_t uefi_base=0,uefi_size=0,boot_dest=0x10220000000ULL,adt_dest=0x10220004000ULL;
  const char *bootargs_path=NULL,*bootargs_hash=""; UINT64 original_devtree=0,original_virt=0,actual_dram=0;
  size_t bootargs_length=0,minimum=0; unsigned i; struct aura_g17_region r;
  if(argc<6){fprintf(stderr,"usage: consumer ADT guest_base guest_size mem_size_actual output.json [--allocated base size] [--uefi base size] [--map-fault] [--ready-map-fault]\n");return 2;}
  f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);length=ftell(f);rewind(f);
  if(length<=0 || length>0x400000){fclose(f);return 2;}adt=malloc((size_t)length);assert(adt);
  if(fread(adt,1,(size_t)length,f)!=(size_t)length){fclose(f);free(adt);return 2;}fclose(f);
  if(!aura_g17_adt_bounds(adt,(size_t)length,&off) || off!=(size_t)length ||
     dt_check(adt,(size_t)length,&checked) || checked!=(size_t)length){free(adt);return 10;}
  for(i=6;i<(unsigned)argc;++i) {
    if((!strcmp(argv[i],"--bootargs") || !strcmp(argv[i],"--bootargs-sha256")) && i+1<(unsigned)argc) {
      if(!strcmp(argv[i],"--bootargs"))bootargs_path=argv[i+1];else bootargs_hash=argv[i+1];++i;
    }
  }
  if(bootargs_path) {
    long bytes;f=fopen(bootargs_path,"rb");if(!f){free(adt);return 2;}
    fseek(f,0,SEEK_END);bytes=ftell(f);rewind(f);
    if(bytes<4 || bytes>(long)sizeof(args)){fclose(f);free(adt);return 19;}
    bootargs_length=(size_t)bytes;
    if(fread(&args,1,bootargs_length,f)!=bootargs_length){fclose(f);free(adt);return 2;}fclose(f);
    switch(args.revision) {
      case 1:minimum=offsetof(struct boot_args,rv1.mem_size_actual)+8;actual_dram=args.rv1.mem_size_actual;break;
      case 2:minimum=offsetof(struct boot_args,rv2.mem_size_actual)+8;actual_dram=args.rv2.mem_size_actual;break;
      case 3:minimum=offsetof(struct boot_args,rv3.mem_size_actual)+8;actual_dram=args.rv3.mem_size_actual;break;
      default:free(adt);return 19;
    }
    if(bootargs_length<minimum || args.version<1 || args.version>3 || args.devtree_size!=(UINT32)length ||
       args.phys_base!=number(argv[2]) || args.mem_size!=number(argv[3]) || actual_dram!=number(argv[4])){free(adt);return 19;}
    if(strlen(bootargs_hash)!=64 || strspn(bootargs_hash,"0123456789abcdef")!=64){free(adt);return 2;}
    original_devtree=(uintptr_t)args.devtree;original_virt=args.virt_base;
  } else {
    args.revision=3;args.version=2;args.phys_base=number(argv[2]);args.mem_size=number(argv[3]);
    args.rv3.mem_size_actual=number(argv[4]);args.devtree_size=(UINT32)length;
  }
  // Host-only address translation: keep every extent/identity/video field from
  // the serialized artifact and replace only the pointer mapping into owned RAM.
  args.virt_base=args.phys_base;args.devtree=adt;
  PcdAdtPointer=(uintptr_t)adt;PcdBootArgsPointer=(uintptr_t)&args;
  PcdSystemMemoryBase=args.phys_base;PcdSystemMemorySize=args.mem_size;
  ((EFI_HOB_GENERIC_HEADER *)hobs.bytes)->HobType=EFI_HOB_TYPE_END_OF_HOB_LIST;
  BuildResourceDescriptorHob(EFI_RESOURCE_SYSTEM_MEMORY,0,args.phys_base,args.mem_size);
  for(i=6;i<(unsigned)argc;++i){
    if(!strcmp(argv[i],"--bootargs") || !strcmp(argv[i],"--bootargs-sha256")){++i;continue;}
    if(!strcmp(argv[i],"--map-fault")){map_fault=1;continue;}
    if(!strcmp(argv[i],"--ready-map-fault")){ready_map_fault=1;continue;}
    if(!strcmp(argv[i],"--map-duplicate-gap")){duplicate_gap=1;continue;}
    if(!strcmp(argv[i],"--fd-base") || !strcmp(argv[i],"--boot-dest") || !strcmp(argv[i],"--adt-dest")) {
      if(i+1>=(unsigned)argc){free(adt);return 2;}
      if(!strcmp(argv[i],"--fd-base"))PcdFdBaseAddress=number(argv[i+1]);
      if(!strcmp(argv[i],"--boot-dest"))boot_dest=number(argv[i+1]);
      if(!strcmp(argv[i],"--adt-dest"))adt_dest=number(argv[i+1]);
      ++i;continue;
    }
    if(i+2>=(unsigned)argc){free(adt);return 2;}
    if(!strcmp(argv[i],"--allocated"))BuildMemoryAllocationHob(number(argv[i+1]),number(argv[i+2]),EfiBootServicesData);
    else if(!strcmp(argv[i],"--uefi")){uefi_base=number(argv[i+1]);uefi_size=number(argv[i+2]);}
    else {free(adt);return 2;}i+=2;
  }
  expected_boot_dest=boot_dest;expected_adt_dest=adt_dest;halt_status=19;
  if(!EarlySetup(&args,&PcdSystemMemoryBase,&PcdSystemMemorySize,(void *)(uintptr_t)boot_dest,(void *)(uintptr_t)adt_dest)) {
    fprintf(stderr,"early admission rejected\n");free(adt);return 19;
  }
  assert(copy_calls==2 && copied_adt);halt_status=22;
  PcdBootArgsPointer=(uintptr_t)&copied_args;PcdAdtPointer=(uintptr_t)copied_adt;
  status=ReserveG17Handoff(uefi_base,uefi_size);
  if(EFI_ERROR(status)){fprintf(stderr,"PEI rejected\n");free(copied_adt);free(adt);return 20;}
  status=AuraG17VerifyPreservation();
  if(EFI_ERROR(status)){fprintf(stderr,"DXE rejected\n");free(copied_adt);free(adt);return 21;}
  if(ready_map_fault)map_fault=1;
  if(ready)ready(NULL,NULL);
  record=AuraG17Record(&record_size);out=fopen(argv[5],"w");if(!out){free(adt);return 2;}
  fprintf(out,"{\"schema\":\"mu.g17.exclusions.v1\",\"hardware_touched\":false,\"marker_present\":%s,\"early_copy_calls\":2,\"early_pei_dxe_ready_to_boot_passed\":true,\"regions\":[",record?"true":"false");
  if(record)for(i=0;i<AURA_G17_REGIONS;++i){aura_g17_region(record,i,&r);fprintf(out,"%s{\"tag\":%u,\"classification\":%u,\"base\":%llu,\"size\":%llu}",i?",":"",r.tag,r.classification,(unsigned long long)r.base,(unsigned long long)r.size);}
  fprintf(out,"],\"bootargs_input\":{\"artifact\":%s,\"revision\":%u,\"version\":%u,\"serialized_bytes\":%zu,\"declared_adt_bytes\":%u,\"sha256_from_orchestrator\":\"%s\",\"original_devtree\":%llu,\"original_virt_base\":%llu,\"owned_host_pointer_translation\":true},\"resource_hobs\":[",
          bootargs_path?"true":"false",args.revision,args.version,bootargs_length,args.devtree_size,bootargs_hash,
          (unsigned long long)original_devtree,(unsigned long long)original_virt);
  { EFI_PEI_HOB_POINTERS h; unsigned count=0;
    for(h.Raw=GetHobList();!END_OF_HOB_LIST(h);h.Raw=GET_NEXT_HOB(h)) {
      if(GET_HOB_TYPE(h)!=EFI_HOB_TYPE_RESOURCE_DESCRIPTOR)continue;
      fprintf(out,"%s{\"type\":%u,\"base\":%llu,\"size\":%llu}",count++?",":"",h.ResourceDescriptor->ResourceType,
              (unsigned long long)h.ResourceDescriptor->PhysicalStart,(unsigned long long)h.ResourceDescriptor->ResourceLength);
    }
  }
  fprintf(out,"],\"efi_memory_map\":[");
  { EFI_MEMORY_DESCRIPTOR *map; UINTN size=0,key,stride; UINT32 version;
    assert(get_map(&size,NULL,&key,&stride,&version)==EFI_BUFFER_TOO_SMALL);
    map=malloc(size);assert(map);assert(get_map(&size,map,&key,&stride,&version)==EFI_SUCCESS);
    for(i=0;i<size/stride;++i)fprintf(out,"%s{\"type\":%u,\"base\":%llu,\"pages\":%llu}",i?",":"",map[i].Type,
        (unsigned long long)map[i].PhysicalStart,(unsigned long long)map[i].NumberOfPages);
    free(map);
  }
  fprintf(out,"]}\n");fclose(out);free(copied_adt);free(adt);return 0;
}

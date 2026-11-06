/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host substitutes for firmware services, not for MemoryPeim's implementation. */
#ifndef MEMORY_PEIM_HOST_H
#define MEMORY_PEIM_HOST_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static
#define VOID void
#define EFIAPI
#define TRUE true
#define FALSE false
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef intptr_t INTN;
typedef char CHAR8;
typedef bool BOOLEAN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_RESOURCE_ATTRIBUTE_TYPE;
typedef int EFI_STATUS;
typedef int RETURN_STATUS;
typedef struct { UINT32 a; UINT16 b,c; UINT8 d[8]; } EFI_GUID;
#define EFI_SUCCESS 0
#define EFI_COMPROMISED_DATA 1
#define EFI_OUT_OF_RESOURCES 2
#define EFI_ERROR(x) ((x) != 0)
#define EFI_PAGE_SIZE 4096ULL
#define EFI_PAGE_MASK 4095ULL
#define EFI_SIZE_TO_PAGES(x) (((x) + 4095) / 4096)
#define EFI_PAGES_TO_SIZE(x) ((x) * 4096)
#define MAX_UINT32 UINT32_MAX
#define MAX_UINT64 UINT64_MAX
#define MAX_UINTN UINTPTR_MAX
#define ALIGN_VALUE(x,a) (((x) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BIT0 1U
#define SIGNATURE_64(a,b,c,d,e,f,g,h) ((UINT64)(a)|((UINT64)(b)<<8)|((UINT64)(c)<<16)|((UINT64)(d)<<24)|((UINT64)(e)<<32)|((UINT64)(f)<<40)|((UINT64)(g)<<48)|((UINT64)(h)<<56))
#define ASSERT(x) assert(x)
#define STATIC_ASSERT(x,m) _Static_assert(x,m)
#define OFFSET_OF(t,m) offsetof(t,m)
#define SIZE_2GB 0x80000000ULL
#define SIZE_8GB 0x200000000ULL
#define SIZE_1GB 0x40000000ULL
#define SIZE_512MB 0x20000000ULL
#define SIZE_256MB 0x10000000ULL
#define SIZE_128MB 0x8000000ULL
#define SIZE_64MB 0x4000000ULL
#define DEBUG(x) do {} while (0)
#define EFI_RESOURCE_SYSTEM_MEMORY 1
#define EFI_RESOURCE_MEMORY_RESERVED 2
#define EFI_RESOURCE_ATTRIBUTE_PRESENT 1
#define EFI_RESOURCE_ATTRIBUTE_INITIALIZED 2
#define EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE 4
#define EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE 8
#define EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE 16
#define EFI_RESOURCE_ATTRIBUTE_TESTED 32
#define EFI_HOB_TYPE_RESOURCE_DESCRIPTOR 3
#define EfiBootServicesData 4
#define EfiReservedMemoryType 0
typedef struct {
  int ResourceType;
  EFI_RESOURCE_ATTRIBUTE_TYPE ResourceAttribute;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  UINT64 ResourceLength;
} EFI_HOB_RESOURCE_DESCRIPTOR;
typedef union { void *Raw; EFI_HOB_RESOURCE_DESCRIPTOR *ResourceDescriptor; } EFI_PEI_HOB_POINTERS;
#define GET_NEXT_HOB(x) ((void *)((x).ResourceDescriptor + 1))
typedef int ARM_MEMORY_REGION_ATTRIBUTES;
typedef struct { UINT64 PhysicalBase, VirtualBase, Length; ARM_MEMORY_REGION_ATTRIBUTES Attributes; } ARM_MEMORY_REGION_DESCRIPTOR;
#define ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK 1
#define ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED 2
#define ARM_MEMORY_REGION_ATTRIBUTE_DEVICE 3
extern UINT64 PcdSystemMemoryBase, PcdSystemMemorySize, PcdFdBaseAddress, PcdFdSize;
extern UINT64 PcdBootArgsPointer, PcdAdtPointer, PcdFrameBufferAddress, PcdFrameBufferSize;
extern UINT32 PcdCoreCount;
#define PcdGet64(x) (x)
#define PcdGet32(x) (x)
#define FixedPcdGet64(x) (x)
#define FixedPcdGet32(x) (x)
#define FeaturePcdGet(x) false
void *GetHobList(void);
void *GetNextHob(int type, void *hob);
void BuildResourceDescriptorHob(int type, UINT64 attributes, UINT64 base, UINT64 size);
void BuildMemoryAllocationHob(UINT64 base, UINT64 size, int type);
void *BuildGuidDataHob(const EFI_GUID *guid, const void *data, UINTN size);
void *AllocatePages(UINTN pages);
RETURN_STATUS ArmConfigureMmu(ARM_MEMORY_REGION_DESCRIPTOR *table, void **base, UINTN *size);
UINTN AsciiSPrint(char *str, UINTN size, const char *format, ...);
#endif

#pragma once
#include <Uefi.h>
typedef enum { EfiGcdMemoryTypeNonExistent, EfiGcdMemoryTypeReserved, EfiGcdMemoryTypeSystemMemory,
               EfiGcdMemoryTypeMemoryMappedIo, EfiGcdMemoryTypePersistent, EfiGcdMemoryTypeMoreReliable } EFI_GCD_MEMORY_TYPE;
typedef struct { UINT64 BaseAddress, Length, Capabilities, Attributes; EFI_GCD_MEMORY_TYPE GcdMemoryType; } EFI_GCD_MEMORY_SPACE_DESCRIPTOR;
typedef struct { EFI_STATUS (*GetMemorySpaceMap)(UINTN *, EFI_GCD_MEMORY_SPACE_DESCRIPTOR **); } HOST_DXE_SERVICES;
extern HOST_DXE_SERVICES *gDS;

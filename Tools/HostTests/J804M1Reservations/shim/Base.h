/* Host shim: just enough of EDK2's Base.h for the J804 M1 reservation code. */
#ifndef HOST_SHIM_BASE_H_
#define HOST_SHIM_BASE_H_
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint64_t UINT64; typedef int64_t INT64; typedef uint32_t UINT32; typedef int32_t INT32;
typedef uint16_t UINT16; typedef uint8_t UINT8; typedef uintptr_t UINTN; typedef intptr_t INTN;
typedef char CHAR8; typedef uint16_t CHAR16; typedef unsigned char BOOLEAN; typedef void VOID;
typedef UINTN RETURN_STATUS; typedef RETURN_STATUS EFI_STATUS; typedef UINT64 EFI_PHYSICAL_ADDRESS;
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static
#define EFIAPI
#define MAX_UINT64 UINT64_MAX
#define MAX_UINT32 UINT32_MAX
#define EFI_PAGE_MASK 0xFFFULL
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#define EFI_SUCCESS 0
#endif

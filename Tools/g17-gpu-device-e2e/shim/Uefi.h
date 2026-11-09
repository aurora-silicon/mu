/* Host shim: just the UEFI types AuraG17GpuDevice.c uses. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32; typedef uint64_t UINT64;
typedef size_t UINTN; typedef char CHAR8; typedef unsigned char BOOLEAN; typedef void VOID;
typedef UINTN EFI_STATUS;
#define IN
#define OUT
#define CONST const
#define STATIC static
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_ERROR(s) ((s) != 0)
#define SIGNATURE_16(a, b) ((a) | ((b) << 8))
#define SIGNATURE_32(a, b, c, d) (SIGNATURE_16(a, b) | (SIGNATURE_16(c, d) << 16))

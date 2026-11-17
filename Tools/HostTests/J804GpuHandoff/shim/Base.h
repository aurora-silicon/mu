/* Host shim: just enough of EDK2's Base.h for J804GpuHandoff.h. */
#ifndef HOST_SHIM_BASE_H_
#define HOST_SHIM_BASE_H_
#include <stdint.h>
#include <stddef.h>
typedef uint64_t UINT64; typedef uint32_t UINT32; typedef uint16_t UINT16; typedef uint8_t UINT8;
typedef uintptr_t UINTN; typedef unsigned char BOOLEAN; typedef void VOID; typedef char CHAR8;
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static
#endif

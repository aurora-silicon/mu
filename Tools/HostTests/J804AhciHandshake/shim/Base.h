/* Host shim: just enough of EDK2's Base.h for AppendedRamdisk.h. */
#ifndef HOST_SHIM_BASE_H_
#define HOST_SHIM_BASE_H_
#include <stdint.h>
#include <stddef.h>
typedef uint64_t UINT64; typedef uint32_t UINT32; typedef uint16_t UINT16; typedef uint8_t UINT8;
typedef uintptr_t UINTN; typedef unsigned char BOOLEAN; typedef void VOID; typedef UINT64 EFI_PHYSICAL_ADDRESS;
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static
#define MAX_UINT32 UINT32_MAX
#define MAX_UINT64 UINT64_MAX
#define MAX_UINTN UINTPTR_MAX
#define EFI_PAGE_SIZE 0x1000
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#define OFFSET_OF(t, f) offsetof (t, f)
#define ALIGN_VALUE(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define STATIC_ASSERT _Static_assert
#define SIGNATURE_16(A, B) ((A) | ((B) << 8))
#define SIGNATURE_32(A, B, C, D) (SIGNATURE_16 (A, B) | (SIGNATURE_16 (C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
  (SIGNATURE_32 (A, B, C, D) | ((UINT64)(SIGNATURE_32 (E, F, G, H)) << 32))
#endif

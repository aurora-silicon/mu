/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Native firmware-service boundary for the complete production phase driver. */
#ifndef PHASE_INDICATOR_HOST_H
#define PHASE_INDICATOR_HOST_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IN
#define OUT
#define OPTIONAL
#define STATIC static
#define VOID void
#define EFIAPI
#define TRUE true
#define FALSE false
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uintptr_t UINTN;
typedef uint16_t CHAR16;
typedef bool BOOLEAN;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void EFI_SYSTEM_TABLE;
typedef struct { UINT32 Id; } EFI_GUID;
typedef struct HOST_EVENT *EFI_EVENT;
typedef void (*EFI_EVENT_NOTIFY)(EFI_EVENT, void *);
typedef struct { int unused; } EDKII_VARIABLE_POLICY_PROTOCOL;
#define EFI_SUCCESS ((EFI_STATUS)0)
#define EFI_INVALID_PARAMETER ((EFI_STATUS)0x8000000000000002ULL)
#define EFI_OUT_OF_RESOURCES ((EFI_STATUS)0x8000000000000009ULL)
#define EFI_WRITE_PROTECTED ((EFI_STATUS)0x8000000000000008ULL)
#define EFI_ABORTED ((EFI_STATUS)0x8000000000000015ULL)
#define EFI_ERROR(x) (((EFI_STATUS)(x) >> 63) != 0)
#define EFI_VARIABLE_NON_VOLATILE 1U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2U
#define EFI_VARIABLE_RUNTIME_ACCESS 4U
#define TPL_CALLBACK 8U
#define TPL_NOTIFY 16U
#define EVT_NOTIFY_SIGNAL 0x200U
#define VARIABLE_POLICY_TYPE_LOCK_ON_CREATE 1U
extern unsigned HostAssertions;
#define ASSERT(x) do { if (!(x)) ++HostAssertions; } while (0)
#define ASSERT_EFI_ERROR(x) ASSERT(!EFI_ERROR(x))
#define DEBUG(x) do {} while (0)
extern EFI_GUID gEfiEndOfDxeEventGroupGuid, gEfiEventReadyToBootGuid;
extern EFI_GUID gEfiEventExitBootServicesGuid, gEfiEventBeforeExitBootServicesGuid;
extern EFI_GUID gEdkiiVariablePolicyProtocolGuid;
typedef struct {
  EFI_STATUS (*LocateProtocol)(EFI_GUID *, void *, void **);
  EFI_STATUS (*CreateEventEx)(UINT32, UINTN, EFI_EVENT_NOTIFY, void *, EFI_GUID *, EFI_EVENT *);
  EFI_STATUS (*CloseEvent)(EFI_EVENT);
} EFI_BOOT_SERVICES;
typedef struct {
  EFI_STATUS (*SetVariable)(CHAR16 *, EFI_GUID *, UINT32, UINTN, void *);
} EFI_RUNTIME_SERVICES;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
EFI_STATUS RegisterBasicVariablePolicy(EDKII_VARIABLE_POLICY_PROTOCOL *, EFI_GUID *, CHAR16 *, UINT32, UINT32, UINT32, UINT32, UINT8);
#endif

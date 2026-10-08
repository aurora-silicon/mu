/** Portable C size and offset definitions for the shared MTP cores. */
#ifndef APPLE_MTP_EDK_STDDEF_H
#define APPLE_MTP_EDK_STDDEF_H

#include <Base.h>

typedef UINTN size_t;

#ifndef NULL
#define NULL  ((VOID *)0)
#endif

#define offsetof(Type, Field)  OFFSET_OF (Type, Field)

#endif

/** Portable C memory operations for the shared MTP cores in EDK II. */
#ifndef APPLE_MTP_EDK_STRING_H
#define APPLE_MTP_EDK_STRING_H

#include <Library/BaseMemoryLib.h>

#define memcpy(Destination, Source, Length) \
  CopyMem ((Destination), (Source), (Length))
#define memset(Buffer, Value, Length) \
  SetMem ((Buffer), (Length), (Value))

#define memcmp(First, Second, Length) CompareMem ((First), (Second), (Length))

#endif

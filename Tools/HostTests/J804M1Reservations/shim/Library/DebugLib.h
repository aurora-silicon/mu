#ifndef HOST_SHIM_DEBUGLIB_H_
#define HOST_SHIM_DEBUGLIB_H_
#include <Base.h>
extern int gHostVerbose;
void HostDebugPrint (UINTN Level, const char *Format, ...);
#define DEBUG_INFO 0x40
#define DEBUG_WARN 0x2
#define DEBUG_ERROR 0x80000000
#define DEBUG(x) HostDebugPrint x
#define ASSERT(x) do { if (!(x)) { fprintf (stderr, "ASSERT %s\n", #x); abort (); } } while (0)
#endif

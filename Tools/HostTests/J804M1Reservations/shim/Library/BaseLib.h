#ifndef HOST_SHIM_BASELIB_H_
#define HOST_SHIM_BASELIB_H_
#include <Base.h>
#include <setjmp.h>
#define AsciiStrCmp(a, b) strcmp ((a), (b))
#define AsciiStrnCmp(a, b, n) strncmp ((a), (b), (n))
#define AsciiStrStr(a, b) strstr ((a), (b))
#define AsciiStrLen(a) strlen (a)
extern jmp_buf gHostStop;
extern int gHostStopCount;
static inline void CpuDeadLoop (void) { gHostStopCount++; longjmp (gHostStop, 1); }
#endif

#pragma once
void HostDebugPrint(UINTN level, const char *format, ...);
#define DEBUG_ERROR 0x80000000u
#define DEBUG_INFO 0x40u
#define DEBUG(x) HostDebugPrint x

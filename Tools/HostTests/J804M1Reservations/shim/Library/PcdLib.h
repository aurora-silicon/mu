#include <Base.h>
extern UINT64 gHostAdt, gHostRamBase, gHostRamSize, gHostFdBase;
#define FixedPcdGet64(x) HOST_PCD_##x
#define HOST_PCD_PcdAdtPointer gHostAdt
#define HOST_PCD_PcdBootArgsPointer 0x10220000000ULL
#define PcdGet64(x) HOST_PCD_##x
#define HOST_PCD_PcdSystemMemoryBase gHostRamBase
#define HOST_PCD_PcdSystemMemorySize gHostRamSize
#define HOST_PCD_PcdFdBaseAddress gHostFdBase
#define PcdGet32(x) HOST_PCD32_##x
#define HOST_PCD32_PcdFdSize 0x1E00000u
#define HOST_PCD32_PcdCoreCount 12u

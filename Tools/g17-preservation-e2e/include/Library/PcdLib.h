/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef G17_HOST_PCD_LIB_H
#define G17_HOST_PCD_LIB_H
#include <Base.h>
extern UINT64 PcdFrameBufferAddress, PcdFrameBufferSize;
extern UINT64 PcdFdBaseAddress, PcdFdSize, PcdSystemMemoryUefiRegionSize;
extern UINT64 PcdBootArgsPointer, PcdAdtPointer, PcdSystemMemoryBase, PcdSystemMemorySize;
#define PcdGet64(x) (x)
#define PatchPcdSet64(x,v) ((x)=(v))
#define FixedPcdGet64(x) (x)
#define FixedPcdGet32(x) ((UINT32)(x))
#endif

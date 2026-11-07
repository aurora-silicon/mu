/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef AURA_G17_PRESERVATION_LIB_H
#define AURA_G17_PRESERVATION_LIB_H
#include <Uefi.h>
#include <IndustryStandard/AuraG17Preservation.h>
CONST VOID *AuraG17Record(UINTN *Length);
BOOLEAN AuraG17Observe(struct aura_g17_observation *Actual);
BOOLEAN AuraG17EarlyCheck(CONST VOID *BootArgs, CONST VOID *AdtSource,
                          UINT64 BootDestination, UINT64 AdtDestination);
#endif

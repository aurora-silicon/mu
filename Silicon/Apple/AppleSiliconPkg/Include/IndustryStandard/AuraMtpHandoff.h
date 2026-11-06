/** @file
  Physical memory owned by Aura's explicit J813-v1 MTP handoff.

  The first MiB backs guest staging IPA 0x1800000; the second remains the
  RTKit DMA grant pool. Both outlive ExitBootServices. This is not a generic
  Aura reservation ABI. Match public Aura windows.py and mtp_handoff.c.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef AURA_MTP_HANDOFF_H_
#define AURA_MTP_HANDOFF_H_

#define AURA_MTP_DISABLED_PROPERTY       "aura,windows-mtp-disabled"
#define AURA_J813_MTP_STAGING_BASE        0x10020000000ULL
#define AURA_J813_MTP_STAGING_SIZE        0x100000ULL
#define AURA_J813_MTP_RTKIT_POOL_SIZE     0x100000ULL
#define AURA_J813_MTP_RESERVED_SIZE       (AURA_J813_MTP_STAGING_SIZE + AURA_J813_MTP_RTKIT_POOL_SIZE)

#endif

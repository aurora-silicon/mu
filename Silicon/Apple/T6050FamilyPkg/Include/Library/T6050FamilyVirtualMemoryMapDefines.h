/**
 * @file T6050FamilyVirtualMemoryMapDefines.h
 *
 * Virtual memory map defines for T6050 family chips (Apple M5 Pro / J714s).
 *
 * These ranges are derived from the live J714s Apple Device Tree fixture
 * (emuasi/build/sptm-macho-j714/dtree-18, re-dumpable with scripts/adtraw.py).
 * The arm-io child `reg` values are offset by the arm-io bus base 0x200000000,
 * which was confirmed against the ADT `ranges` property:
 *
 *   node   ADT reg base   +0x200000000 -> CPU physical   size
 *   aic    0x080400000                   0x280400000     0x1cc000   ("aic,3")
 *   pmgr   0x080600000                   0x280600000     0x1fc000
 *   wdt    0x08836c000                   0x28836c000     0x004000
 *   smc    0x08c600000                   0x28c600000     0x088000
 *   uart0  0x305200000                   0x505200000     0x004000   ("uart-1,samsung")
 *
 * aic-timebase (CPU physical 0x280580000) sits inside the AIC 0x1cc000 span, so
 * a single AIC descriptor covers both.
 *
 * NOTE on the UART: T6050 runs as an EL1 guest of the x1n1 hypervisor, which
 * exposes the console UART to the guest at IPA 0x60000000 and maps that IPA to
 * the real PA 0x505200000 (see x1n1 sptm.c SPTM_GUEST_IO_IPA and the
 * WINDOWS_HANDOFF ABI). Mu's own stage-1 page tables map PcdAppleUartBase 1:1,
 * so the UART must be mapped at the guest IPA the hypervisor actually hands us,
 * NOT the real PA. Hence the default here is the IPA 0x60000000. Override
 * T6050_UART_BASE / T6050_UART_SIZE from the DSC if x1n1's console IPA changes.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Base.h>

#ifndef APPLE_T6050_VIRTUAL_MEMORY_MAP_DEFINES_H_
#define APPLE_T6050_VIRTUAL_MEMORY_MAP_DEFINES_H_

//
// "core" MMIO cluster - AIC (0x280400000), PMGR (0x280600000),
// WDT (0x28836c000) and SMC (0x28c600000) all live inside
// [0x280000000, 0x290000000). Mapped nGnRnE as device memory.
//
// A single 256 MiB descriptor covers every core peripheral Mu might touch
// during the Mu phase (the AppleAicDxe driver discovers the AIC base from the
// live ADT, so only the mapping - not a PCD - has to be right here).
//
#define APPLE_T6050_CORE_SYSTEM_MMIO_BASE 0x280000000ULL
#define APPLE_T6050_CORE_SYSTEM_MMIO_SIZE SIZE_256MB

// Individual apertures (for reference / future finer-grained maps).
#define APPLE_T6050_AIC_BASE          0x280400000ULL
#define APPLE_T6050_AIC_SIZE          0x1cc000
#define APPLE_T6050_AIC_TIMEBASE_BASE 0x280580000ULL
#define APPLE_T6050_PMGR_BASE         0x280600000ULL
#define APPLE_T6050_PMGR_SIZE         0x1fc000
#define APPLE_T6050_PMGR_CPU_START    0x280688000ULL   /* pmgr + 0x88000 */
#define APPLE_T6050_WDT_BASE          0x28836c000ULL
#define APPLE_T6050_WDT_SIZE          0x4000
#define APPLE_T6050_SMC_BASE          0x28c600000ULL
#define APPLE_T6050_SMC_SIZE          0x88000

// Real UART0 PA (documentation only; the guest never touches this directly).
#define APPLE_T6050_UART_PHYS_BASE    0x505200000ULL

//
// Console UART as seen by the x1n1 guest (IPA). This is what Mu maps and what
// PcdAppleUartBase must equal. Overridable from the DSC via T6050_UART_BASE.
//
#ifndef T6050_UART_BASE
#define T6050_UART_BASE 0x60000000ULL
#endif
#ifndef T6050_UART_SIZE
#define T6050_UART_SIZE 0x4000
#endif

//
// System DRAM base. Verified from the J714s ADT /chosen dram-base and the
// QEMU darwin machine (8 GiB at the 1 TiB mark). Actual base/size are patched
// at runtime by SEC from boot_args.
//
#define APPLE_T6050_DRAM_BASE 0x10000000000ULL

#endif // APPLE_T6050_VIRTUAL_MEMORY_MAP_DEFINES_H_

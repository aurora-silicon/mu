/** @file
  Separate hardware/QEMU Windows topologies and native-AIC resources for J714s.

  This isolated profile describes fixtures/windows/dtree-24mhz, whose CPU reg
  values produce QEMU MPIDRs 0x80000000+{0..5}, 0x80000100+{0..5}, and
  0x80000200+{0..5}. Physical J714s uses different affinity values; this table
  is selected by J714_HARDWARE=1 and matches the 2026-09-29 Linux boot traces.

  * The AIC v3 register geometry mirrors the captured T6050 fixture in
    drivers/AppleAic/aic3_platform.c (ntasi_aic3_t6050_fixture): AIC3 core base
    0x280400000, size 0x1cc000, EVENT base 0x280440000, 3104/4096 IRQs.  The
    same geometry is echoed by NOTES-windows-guest.md ("AIC3 PA 0x280400000
    (+0x1cc000)").

  The E/P efficiency-class split for T6050 is NOT independently grounded here, so
  every core is published with efficiency class 0 (homogeneous).  Differentiate
  it only once a captured T6050 cluster map is available; this affects Windows
  scheduling hints, not interrupt or processor-start correctness.

  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#ifndef T6050_J714S_TOPOLOGY_H_
#define T6050_J714S_TOPOLOGY_H_

#define T6050_J714S_CPU_COUNT              18
#define T6050_J714S_CLUSTER_COUNT           3

/*
 * Native Apple AIC v3 resources (captured T6050/J714s layout).  These describe
 * the *physical* Apple Interrupt Controller that Windows drives through the
 * APPL/NTAS AIC3 CSRT below -- there is no GICv3 distributor on this path.
 */
#define T6050_AIC3_CORE_BASE         0x0000000280400000ULL
#define T6050_AIC3_CORE_SIZE         0x00000000001CC000ULL
#define T6050_AIC3_EVENT_BASE        0x0000000280440000ULL
#define T6050_AIC3_EVENT_SIZE        0x0000000000004000ULL
#define T6050_AIC3_CONFIG_OFFSET     0x0000000000010000ULL
#define T6050_AIC3_IRQ_COUNT         3104
#define T6050_AIC3_MAX_IRQ           4096
#define T6050_AIC3_CHIP_ID           0x6050
#define T6050_AIC3_BOARD_ID          0x0008

/*
 * Architected ARM timer GSIVs.  On Apple silicon the timers are FIQ-wired; the
 * x1n1 hypervisor reflects them onto these logical INTIDs (non-secure PL1 = 17,
 * virtual = 18), matching the boot-agent J714 GTDT and the WINDOWS_HANDOFF ABI.
 * These are timer identities only and do NOT imply any GIC distributor.
 */
#define T6050_GTDT_NONSECURE_EL1_GSIV    17
#define T6050_GTDT_VIRTUAL_EL1_GSIV      18

/*
 * X(LogicalUid, RawMpidr, EfficiencyClass)
 *
 * ACPI UIDs are dense for Windows.  RawMpidr is the emulated MPIDR_EL1 value
 * (bit 31 RES1 set), placed into the GICC MPIDR field verbatim.
 *
 *   QEMU fixture cluster 0: 0x80000000..0x80000005
 *   QEMU fixture cluster 1: 0x80000100..0x80000105
 *   QEMU fixture cluster 2: 0x80000200..0x80000205
 * J714_HARDWARE selects the captured physical affinities (Aff2=4,4,1).
 * The default emulator profile retains its ADT reg-derived affinities.
 */
#ifndef J714_HARDWARE
#error J714_HARDWARE must be defined for both C and ASLCC topology consumers
#endif
#if J714_HARDWARE
#define T6050_J714S_CPU_LIST(X)                                                \
  X ( 0, 0x80040000, 0)                                                        \
  X ( 1, 0x80040001, 0)                                                        \
  X ( 2, 0x80040002, 0)                                                        \
  X ( 3, 0x80040003, 0)                                                        \
  X ( 4, 0x80040004, 0)                                                        \
  X ( 5, 0x80040005, 0)                                                        \
  X ( 6, 0x80040100, 0)                                                        \
  X ( 7, 0x80040101, 0)                                                        \
  X ( 8, 0x80040102, 0)                                                        \
  X ( 9, 0x80040103, 0)                                                        \
  X (10, 0x80040104, 0)                                                        \
  X (11, 0x80040105, 0)                                                        \
  X (12, 0x80010200, 0)                                                        \
  X (13, 0x80010201, 0)                                                        \
  X (14, 0x80010202, 0)                                                        \
  X (15, 0x80010203, 0)                                                        \
  X (16, 0x80010204, 0)                                                        \
  X (17, 0x80010205, 0)

/* Boot CPU in the captured J714s hardware topology. */
#define T6050_J714S_BOOT_CPU_MPIDR   0x80040000

#else
#define T6050_J714S_CPU_LIST(X)                                                \
  X ( 0, 0x80000000, 0)                                                        \
  X ( 1, 0x80000001, 0)                                                        \
  X ( 2, 0x80000002, 0)                                                        \
  X ( 3, 0x80000003, 0)                                                        \
  X ( 4, 0x80000004, 0)                                                        \
  X ( 5, 0x80000005, 0)                                                        \
  X ( 6, 0x80000100, 0)                                                        \
  X ( 7, 0x80000101, 0)                                                        \
  X ( 8, 0x80000102, 0)                                                        \
  X ( 9, 0x80000103, 0)                                                        \
  X (10, 0x80000104, 0)                                                        \
  X (11, 0x80000105, 0)                                                        \
  X (12, 0x80000200, 0)                                                        \
  X (13, 0x80000201, 0)                                                        \
  X (14, 0x80000202, 0)                                                        \
  X (15, 0x80000203, 0)                                                        \
  X (16, 0x80000204, 0)                                                        \
  X (17, 0x80000205, 0)

/* Boot CPU in the QEMU fixture. */
#define T6050_J714S_BOOT_CPU_MPIDR   0x80000000

#endif

/* Publish all QEMU cores; retain a single-CPU diagnostic build override. */
#ifndef T6050_J714S_MADT_CPU_COUNT
#define T6050_J714S_MADT_CPU_COUNT   18
#endif

#endif /* T6050_J714S_TOPOLOGY_H_ */

/** @file
  Exact processor topology and native-AIC (AIC v3) resources for the T6050
  J714s (Apple M5 Pro, MacBook Pro).

  This is the single source shared by the native-AIC MADT and the AIC v3 CSRT.
  Every value below is grounded in a captured description, not invented:

  * The 18 CPU MPIDRs are the live J714s values captured in
    NOTES-windows-guest.md ("T6050 MPIDRs: 0x80040000+{0..5}, 0x80040100+{0..5},
    0x80010200+{0..5}").  They are the real MPIDR_EL1 values (bit 31 RES1 set),
    so they are placed into the MADT GICC MPIDR field verbatim, exactly as the
    boot-agent single-CPU J714 MADT places cpu0's 0x80040000.
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
 * ACPI UIDs are dense for Windows.  RawMpidr is the captured MPIDR_EL1 value
 * (bit 31 RES1 set), placed into the GICC MPIDR field verbatim.
 *
 *   Cluster 0 (Aff2=4, Aff1=0): 0x80040000..0x80040005
 *   Cluster 1 (Aff2=4, Aff1=1): 0x80040100..0x80040105
 *   Cluster 2 (Aff2=1, Aff1=2): 0x80010200..0x80010205
 */
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

/*
 * The boot CPU MPIDR for the single-CPU milestone (matches the boot-agent
 * J714 MADT: T6050 cpu0, MPIDR 0x80040000).
 */
#define T6050_J714S_BOOT_CPU_MPIDR   0x80040000

/*
 * Number of CPUs the native-AIC MADT actually publishes.
 *
 * The current x1n1 Windows milestone brings up a SINGLE boot CPU (see the
 * boot-agent MADT and WINDOWS_HANDOFF: "Add the remaining 17 cores only after a
 * UEFI shell boots under x1n1").  Default to that so this native-AIC MADT is a
 * drop-in for the boot-agent's GICv3 MADT without changing CPU bring-up.  Set to
 * T6050_J714S_CPU_COUNT to publish all 18 captured cores.
 */
#ifndef T6050_J714S_MADT_CPU_COUNT
#define T6050_J714S_MADT_CPU_COUNT   1
#endif

#endif /* T6050_J714S_TOPOLOGY_H_ */

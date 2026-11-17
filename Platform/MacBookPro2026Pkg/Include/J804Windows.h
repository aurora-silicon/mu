/** @file
  J804 (MacBook Pro 14-inch, M6) windows profile: the values this firmware
  shares with the EL2 carrier (aura WINDOWS=1 personality ported to T8152).

  Each value here is half of a contract. The carrier side must use the same
  number; neither side can discover the other's choice at run time. See
  Docs/j804-windows-profile.md.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef J804_WINDOWS_H_
#define J804_WINDOWS_H_

//
// Boot CPU: ADT cpu6, the first P core. MADT lists it first.
//
#define J804_BOOT_CPU_MPIDR  0x10100

//
// Emulated GICv3 (carrier hv_vgic.c). The distributor and redistributor
// bases are the 36-bit pair the carrier uses for T8142 and that the J873
// windows profile used on T8152 (DIST_BASE_36_BIT / REDIST_BASE_36_BIT). The
// window 0xF00000000..0xF30000000 is outside every /arm-io range of the J804
// ADT; the carrier's hv_vgicv3_init T8152 case uses this pair.
// Redistributor frames are 0x20000 each, one per live CPU, dense in carrier
// CPU-index order; GICR_TYPER.Last ends the walk.
//
// The MADT is static: J804_CPU_COUNT GICCs, all ENABLED, and a GICR range of
// J804_CPU_COUNT frames. The carrier sizes the vGIC to hv_vgic_live_cpu_count()
// instead, so it must REFUSE a T8152 Windows launch unless exactly
// J804_CPU_COUNT CPUs are live (otherwise Windows sees enabled CPUs with no
// redistributor behind them). There is no carrier-reported live mask that
// J804AcpiDxe could patch the MADT from.
//
#define J804_GICD_BASE         0xF00000000
#define J804_GICR_BASE         0xF10000000
#define J804_GICR_FRAME_SIZE   0x20000
#define J804_CPU_COUNT         12

//
// Timer INTIDs the carrier injects (hv_exc.c HV_GIC_TIMER_P_INTID and
// HV_GIC_TIMER_V_INTID). Equal to T8152FamilyPkg's PcdArmArchTimerIntrNum and
// PcdArmArchTimerVirtIntrNum.
//
#define J804_TIMER_P_INTID     17
#define J804_TIMER_V_INTID     18

//
// Published GSIVs for AIC lines above the GIC SPI limit. The carrier's T8152
// hv_aic_alias table must hold exactly these {published, physical} pairs.
// All three published numbers are absent from every "interrupts" property of
// the J804 ADT (checked on the host against the saved ADT).
//
#define J804_GSIV_XHC1         995   // usb-drd1 physical AIC 1692
#define J804_GSIV_ANS          996   // ans NVMe physical AIC 1349
#define J804_GSIV_XHC2         997   // usb-drd3 physical AIC 1834

//
// ANS0 (only in J804_WINDOWS_ANS builds) names two SHARED pages that the
// carrier must NEVER map into stage 2 (PLAN 0.5: the whole spi
// controller/NOR stays out of the guest; other coprocessors' mailboxes are
// not the guest's):
//   - the PMGR page 0x300700000: ps_ans at +0x8a0, but also ps_spi_p at
//     +0xd40 and ps_qspi at +0x1140 (our t8152-j804.dts pmgr node);
//   - the mailbox page 0x316020000: the ANS post-office window at +0x300,
//     but also dcp_mbox at +0x500 (t8152-j804.dts), and it is reg[2] of
//     dcp, dcpext0/1, gfx-asc, gfx1-asc, isp, pmp and scaler0 in the ADT.
// The carrier traps both pages (hooked MMIO) and passes through only the
// 4-byte ps_ans register and the 0x80-byte ANS mailbox window; every other
// offset is refused. Alternative: the carrier powers ANS itself and the
// PMGR resource is dropped from ANS0.
//
#define J804_ANS_PMGR_PAGE        0x300700000
#define J804_ANS_PS_REG           0x3007008A0   // 4 bytes, the only PMGR pass-through
#define J804_PMGR_PS_SPI_P        0x300700D40   // never reachable by the guest
#define J804_PMGR_PS_QSPI         0x300701140   // never reachable by the guest
#define J804_ANS_MBOX_PAGE        0x316020000
#define J804_ANS_MBOX_WINDOW      0x316020300   // 0x80 bytes, the only mailbox pass-through
#define J804_ANS_MBOX_WINDOW_SIZE 0x80
#define J804_DCP_MBOX             0x316020500   // never reachable by the guest

//
// Boot hand-off copies: PrePi copies boot_args (16 KiB page) and the ADT
// (J804_M1_ADT_COPY_MAX = 2 MiB) to the fixed PcdBootArgsPointer /
// PcdAdtPointer of T8152FamilyPkg.dsc.inc before anything else. The carrier's
// guest RAM window must contain [J804_HANDOFF_BASE, J804_HANDOFF_TOP), and
// the launcher's ADT, TrustCache, FD+blobs and boot_args (load_raw: from
// guest_ram base + 16 MiB upwards) must lie outside it, as must the 128 MiB
// UEFI region at the window top. PrePi (J804_WINDOWS) stops before copying
// when a source or the running FD overlaps the copy range or the range is
// not inside boot_args RAM.
//
#define J804_HANDOFF_BASE      0x10220000000
#define J804_HANDOFF_TOP       0x10220204000

//
// WD1 (/arm-io/wdt reg[0], apple,t8152-wdt). Register layout as in aura's
// wdt.c and Linux apple_wdt: COUNT +0x10, ALARM +0x14, CTL +0x1c.
//
#define J804_WD1_BASE          0x3082b0000
#define J804_WD1_COUNT         0x10
#define J804_WD1_ALARM         0x14
#define J804_WD1_CTL           0x1c

#endif

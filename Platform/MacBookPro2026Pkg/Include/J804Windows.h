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
// ADT. Redistributor frames are 0x20000 each, one per live CPU, dense in
// carrier CPU-index order; GICR_TYPER.Last ends the walk.
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
// WD1 (/arm-io/wdt reg[0], apple,t8152-wdt). Register layout as in aura's
// wdt.c and Linux apple_wdt: COUNT +0x10, ALARM +0x14, CTL +0x1c.
//
#define J804_WD1_BASE          0x3082b0000
#define J804_WD1_COUNT         0x10
#define J804_WD1_ALARM         0x14
#define J804_WD1_CTL           0x1c

#endif

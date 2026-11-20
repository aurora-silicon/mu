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

//
// D1: the carrier-emulated AHCI 1.3.1 HBA (one SATA port, a DRAM-backed disk;
// windows-m6/D1-AHCI-DESIGN.md). Only a J804_WINDOWS_AHCI build uses these.
//
// ABAR: one 16 KiB stage-2 page the carrier hooks and never maps, in the gap
// [0xE80000000, 0x1000000000) that no /arm-io range of the J804 ADT covers,
// 0x10000000 above the vGIC ITS frame. The interrupt is a purely virtual SPI
// (no AIC line behind it): level-sensitive, active-high, routed by the
// carrier's GICD_IROUTER. 990 is in no "interrupts" property of the J804 ADT
// and is not one of the published AIC aliases above.
//
#define J804_AHCI_BASE         0xF40000000
#define J804_AHCI_SIZE         0x4000
#define J804_GSIV_AHCI         990

//
// Run-time handshake: the launcher writes this /chosen property into the
// guest ADT only when the carrier has attached a disk (ATTACH done, ABAR
// hooked). Without it J804AhciDxe registers nothing, so an AHCI FD under an
// older launcher never touches J804_AHCI_BASE. Little-endian, 40 bytes:
//   +0x00 u64 ABAR base (J804_AHCI_BASE)
//   +0x08 u64 ABAR size (J804_AHCI_SIZE)
//   +0x10 u32 GSIV (J804_GSIV_AHCI)
//   +0x14 u32 boot: 0 = AHCI disk first, 1 = appended RAM disk first
//   +0x18 u64 store base (first byte of the carrier's DRAM store)
//   +0x20 u64 store top (exclusive; the --windows-guest-ram window top)
// The 24-byte form without the store range is accepted with a warning.
//
#define J804_AHCI_ADT_PROPERTY        "aura,windows-ahci-v1"
#define J804_AHCI_ADT_PROPERTY_SIZE   40
#define J804_AHCI_ADT_PROPERTY_SHORT  24
#define J804_AHCI_BOOT_DISK           0
#define J804_AHCI_BOOT_RAMDISK        1

//
// Raw FD marker (MacBookPro2026.fdf region 0x110|0x10, AHCI builds only),
// read by the launcher before it attaches a disk: "J804AHC1", u32
// J804_AHCI_BASE >> 12, u32 J804_GSIV_AHCI. A non-AHCI FD has 0xFF there.
//
#define J804_AHCI_FD_MARKER_OFFSET    0x110

//
// D4: USB keyboard and mouse (windows-m6/D4-USB-DESIGN.md). Only a
// J804_WINDOWS_USB build uses these. The carrier passes usb-drd1's xHCI
// register window through to the guest (stage 2 maps exactly these two
// 16 KiB pages, IPA == PA, Device-nGnRnE; the dwc3 globals at +0xc100, the
// PHY, the DART and everything else of the port stay denied), aliases AIC
// 1692 to the level SPI J804_GSIV_XHC1, and maps exactly the guest RAM
// window in a coherent Gen3 DART identity map of its own (DVA == IPA == PA),
// invisible to the guest. Mu never touches the controller: it only publishes
// \_SB.XHC1 (PNP0D10) for Windows' usbxhci, in an SSDT that J804XhciAcpiDxe
// installs only when the launcher's handshake below is present and valid.
// usb-drd0 (0x402430000) is the proxy link to the host and is never
// described; usb-drd3 (J804_XHC2_BASE, GSIV 997) is not in this build.
//
#define J804_XHC1_BASE         0x40A430000   // usb-drd1 xHCI (dwc3 +0x0..+0x7FFF)
#define J804_XHC2_BASE         0x412430000   // usb-drd3, not described by any build yet
#define J804_XHC_SIZE          0x8000        // two 16 KiB stage-2 pages

//
// Run-time handshake: the launcher writes this /chosen property into the
// guest ADT only after the carrier's USB ATTACH succeeded (stage-2 window
// mapped, alias owned, DART identity map live). Without it, or with any value
// that differs from this contract, J804XhciAcpiDxe installs no XHC1 SSDT, so
// Windows never touches the xHCI window. Little-endian, 40 bytes:
//   +0x00 u32 port mask: exactly J804_USB_PORT_DRD1 (bit 1 = usb-drd1; the
//             ATTACH argument; bit 3 = usb-drd3 is refused by this build)
//   +0x04 u32 GSIV (J804_GSIV_XHC1)
//   +0x08 u64 xHCI base (J804_XHC1_BASE)
//   +0x10 u64 xHCI size (J804_XHC_SIZE)
//   +0x18 u64 DMA window base: first byte the carrier's DART maps (DVA == PA)
//   +0x20 u64 DMA window top (exclusive). The window must be non-empty,
//             16 KiB aligned at both ends and clear of the xHCI window; Mu
//             also requires every byte of RAM in its GCD map to lie inside
//             it and the window to be clear of the AHCI store named in
//             /chosen/aura,windows-ahci-v1 (when that property is present).
//
#define J804_USB_ADT_PROPERTY        "aura,windows-usb-v1"
#define J804_USB_ADT_PROPERTY_SIZE   40
#define J804_USB_PORT_DRD1           0x2
#define J804_USB_PORT_DRD3           0x8

//
// Raw FD marker (MacBookPro2026.fdf region 0x120|0x10, USB builds only),
// read by the launcher before it attaches USB: "J804USB1", u32
// J804_XHC1_BASE >> 12, u32 J804_GSIV_XHC1. A non-USB FD has 0xFF there.
// 0x130 is kept for a later usb-drd3 marker ("J804USB3").
//
#define J804_USB_FD_MARKER_OFFSET    0x120

//
// G0: the carrier-admitted G18 GPU (windows-m6/WINDOWS-GPU-PLAN.md H10,
// windows-m6/G0-CARRIER-CONTRACT.md). Only a J804_WINDOWS_GPU build uses
// these. Unlike AHCI and USB, no address is part of the compiled contract:
// the carrier lifts every GPU address, line and size from the live ADT, puts
// them in a 16 KiB read-only handoff page outside guest RAM, and the launcher
// writes /chosen/aura,windows-gpu-v1 (16 bytes: page IPA, page size) only
// after the carrier's ADMIT returned 0. Layout, rules and parser:
// AppleSiliconPkg/Include/J804GpuHandoff.h (PrePi reads it too).
//
//   - PrePi: with a valid page, lowers Mu's RAM top to the GPU pool base (the
//     pool is the top of the guest RAM window, 32 MiB aligned, at most half
//     of it) and describes the pool as reserved memory, so the UEFI memory
//     map Windows gets reports it as EfiReservedMemoryType.
//   - J804GpuAcpiDxe: re-validates the page and installs \_SB.GPU0 (its own
//     SSDT, OEM table ID J804GPU0, patched from the page) with _HID
//     J804_GPU0_HID, the sgx window, the GFX post-office page, the pool and
//     the handoff page in _CRS, the handoff IPA in _DSD, no interrupt and no
//     power methods.
// Without the handshake, or with any check failing, the RAM is unchanged and
// there is no GPU0. Mu never touches the GPU, the mailbox or the pool.
//
// J804_GPU0_HID is the project's own ID (owner decision D5): not an
// AuroraSilicon NTAS ID, so none of their unaudited drivers can bind.
//
#define J804_GPU0_HID  "AGHN0001"

//
// I0: keyboard and pointer (windows-m6/INPUT-DESIGN.md, aura-win
// docs/windows-t8152.md "The Mu side"). Only a J804_WINDOWS_INPUT build uses
// these. The carrier emulates one 16 KiB MMIO mailbox page (hooked, never
// mapped; every access trapped) in the same /arm-io gap as the AHCI ABAR,
// 0x10000000 above it, and raises a purely virtual level SPI (no AIC line
// behind it) while reports are queued. 991 is in no "interrupts" property of
// the J804 ADT and is neither the AHCI line nor a published AIC alias. The
// carrier translates every source (Mac-fed frames on the vUART RX first, the
// internal keyboard and trackpad later) into fixed HID reports; Windows'
// m6input.sys binds _HID J804_INPUT_HID. Mu never touches the page and has no
// input driver: J804InputAcpiDxe only installs \_SB.INP0 (its own SSDT, OEM
// table ID J804INP0) when the launcher's handshake below is present and valid.
//
#define J804_INPUT_BASE        0xF50000000
#define J804_INPUT_SIZE        0x4000
#define J804_GSIV_INPUT        991
#define J804_INPUT_HID         "NTAS2040"   // project-local; not AuroraSilicon's NTAS0050

//
// Run-time handshake: the launcher writes this /chosen property into the
// guest ADT only after the carrier's input ATTACH returned 0 (page hooked,
// SPI 991 owned). Without it, or with any value that differs from this
// contract, J804InputAcpiDxe installs no INP0 SSDT, so Windows never touches
// the page. Little-endian, 24 bytes:
//   +0x00 u64 page base (J804_INPUT_BASE)
//   +0x08 u32 page size (J804_INPUT_SIZE)
//   +0x0C u32 GSIV (J804_GSIV_INPUT)
//   +0x10 u32 feature mask: bits 0-3 the report collections (keyboard,
//             absolute pointer, relative pointer, consumer; all four set),
//             bits 4-15 clear (PTP is not in this contract), bits 16-23 the
//             attached sources (at least one: 16 host feed, 17 internal
//             keyboard, 18 trackpad), bits 24-31 clear
//   +0x14 u32 version (J804_INPUT_VERSION)
//
#define J804_INPUT_ADT_PROPERTY        "aura,windows-input-v1"
#define J804_INPUT_ADT_PROPERTY_SIZE   24
#define J804_INPUT_VERSION             1
#define J804_INPUT_FEATURES_REPORTS    0x0000000F
#define J804_INPUT_FEATURES_SOURCES    0x00FF0000

//
// Raw FD marker (MacBookPro2026.fdf region 0x140|0x10, input builds only),
// read by the launcher before it attaches input: "J804INP1", u32
// J804_INPUT_BASE >> 12, u32 J804_GSIV_INPUT. A non-input FD has 0xFF there.
//
#define J804_INPUT_FD_MARKER_OFFSET    0x140

//
// G6-S scanout service (aura-win j804-windows-g6-scanout, hv_scanout.h;
// docs/windows-t8152.md "G6-S"). Only a J804_WINDOWS_SCANOUT build uses these.
// The carrier emulates one 16 KiB register page (hooked, never mapped) in the
// same /arm-io gap, 0x10000000 above the input page, and raises a purely
// virtual level SPI (no AIC line) for vblank, flip done and flip error. It
// gives the WDDM driver a virtual vblank from the system counter and flips
// copied into the iBoot scanout; the DCP is never touched. 992 is in no
// "interrupts" property of the J804 ADT and is neither 990/991 nor a published
// AIC alias. Mu never touches the page: with a valid handshake below (and the
// GPU handshake) J804GpuAcpiDxe installs the GPU0 variant (J804Gpu0ScnSsdt)
// whose _CRS adds resource 5 (the page) and resource 6 (Interrupt 992), so the
// one WDDM adapter owns both.
//
#define J804_SCANOUT_BASE          0xF60000000
#define J804_SCANOUT_SIZE          0x4000
#define J804_GSIV_SCANOUT          992

//
// Run-time handshake: the launcher writes this /chosen property into the
// guest ADT only after the carrier's scanout ATTACH returned 0 (page hooked,
// SPI 992 owned). Without it, or with any value that differs from this
// contract, GPU0 carries no scanout resources. Little-endian, 32 bytes:
//   +0x00 u64 page base (J804_SCANOUT_BASE)
//   +0x08 u32 page size (J804_SCANOUT_SIZE)
//   +0x0C u32 GSIV (J804_GSIV_SCANOUT)
//   +0x10 u32 FEATURES (the page's: at least J804_SCANOUT_FEATURES_REQUIRED,
//             nothing outside J804_SCANOUT_FEATURES_KNOWN)
//   +0x14 u32 refresh in mHz (J804_SCANOUT_MIN_MHZ..J804_SCANOUT_MAX_MHZ)
//   +0x18 u32 version (J804_SCANOUT_VERSION)
//   +0x1C u32 zero
//
#define J804_SCANOUT_ADT_PROPERTY        "aura,windows-scanout-v1"
#define J804_SCANOUT_ADT_PROPERTY_SIZE   32
#define J804_SCANOUT_VERSION             1
#define J804_SCANOUT_FEATURES_REQUIRED   0x00000003   // timer vblank, copy flip
#define J804_SCANOUT_FEATURES_KNOWN      0x0000007F
#define J804_SCANOUT_MIN_MHZ             1000
#define J804_SCANOUT_MAX_MHZ             240000

//
// Raw FD marker (MacBookPro2026.fdf region 0x150|0x10, scanout builds only),
// read by the launcher before it attaches the service: "J804SCN1", u32
// J804_SCANOUT_BASE >> 12, u32 J804_GSIV_SCANOUT. A non-scanout FD has 0xFF.
//
#define J804_SCANOUT_FD_MARKER_OFFSET    0x150

#endif

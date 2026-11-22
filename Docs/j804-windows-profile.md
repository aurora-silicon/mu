# J804 windows profile (Platform/MacBookPro2026Pkg)

Mu for the MacBook Pro 14-inch, M6 (J804, T8152), running at EL1 as the guest
of the EL2 carrier: aura's `WINDOWS=1` personality ported to T8152.
Build: `sh Tools/build-j804-windows.sh` on branch `j804-windows`;
output `../out/j804-windows/artifacts/J804WIN_EFI.fd` with `MANIFEST.txt`.
The serial variant is `WM6_CLEAN=1 WM6_J804_VUART=TRUE
WM6_OUTPUT_ROOT=../out/j804-windows-vuart sh Tools/build-j804-windows.sh`.
Both variants share `Build/MacBookPro2026-AARCH64`; the script cleans it by
itself when the flags differ from the last build there, and `WM6_CLEAN=1`
gives the clean tree the recorded sha256 comes from. `MANIFEST.txt` records
the build flags and each submodule's HEAD and `git diff` sha256 (the j873
patches are uncommitted submodule changes). The manifest check fails unless
the FV carries exactly one DSDT, the DSDT declares `Device (COM0)` directly
in `Scope (\_SB)` exactly when `J804_WINDOWS_VUART=TRUE`, and DBG2 (naming
`\_SB.COM0`) is in the FV exactly then. In a vUART build `DxeCore.efi` also
carries the `J804 reset: EL2, restarting through WD1` string: DxeCore links
DebuggerFeaturePkg's DebugAgentDxe, whose GDB stub reboot path
(`DebugReboot` -> `ResetCold`) is reachable only with a real serial
transport; with the Null serial library the linker drops it. The debugger
stays off (`PcdForceEnableDebugger` FALSE, no debugger-control HOB).

The D1 AHCI variant (the carrier's emulated SATA disk, see below) is
`WM6_CLEAN=1 WM6_J804_VUART=TRUE WM6_J804_AHCI=TRUE
WM6_OUTPUT_ROOT=../out/j804-windows-ahci sh Tools/build-j804-windows.sh`.
With `J804_WINDOWS_AHCI` FALSE (the default) the FDs are byte-identical to
those built before the flag existed.

The D4 USB variant (usb-drd1's xHCI passed through, see below) is
`WM6_CLEAN=1 WM6_J804_VUART=TRUE WM6_J804_AHCI=TRUE WM6_J804_USB=TRUE
WM6_OUTPUT_ROOT=../out/j804-windows-usb sh Tools/build-j804-windows.sh`
(`WM6_J804_USB` needs `WM6_J804_AHCI`). With `J804_WINDOWS_USB` FALSE the
default, vUART and AHCI FDs are byte-identical to those built before the flag
existed.

The G0 GPU variant (the carrier-admitted G18 GPU as `\_SB.GPU0`, see below)
is `WM6_CLEAN=1 WM6_J804_VUART=TRUE WM6_J804_AHCI=TRUE WM6_J804_USB=TRUE
WM6_J804_GPU=TRUE WM6_OUTPUT_ROOT=../out/j804-windows-gpu sh
Tools/build-j804-windows.sh` (`WM6_J804_GPU` needs `WM6_J804_AHCI`). With
`J804_WINDOWS_GPU` FALSE the default, vUART, AHCI and USB FDs are
byte-identical to those built before the flag existed. The build script also
accepts a `j804-windows-*` topic branch.

The I0 input variant (the carrier's keyboard and pointer mailbox as
`\_SB.INP0`, see below) is `WM6_CLEAN=1 WM6_J804_VUART=TRUE
WM6_J804_AHCI=TRUE WM6_J804_USB=TRUE WM6_J804_GPU=TRUE WM6_J804_INPUT=TRUE
WM6_OUTPUT_ROOT=../out/j804-windows-next sh Tools/build-j804-windows.sh`
(`WM6_J804_INPUT` needs `WM6_J804_AHCI`; branch `j804-windows-next` carries
AHCI, vUART, USB, GPU and input). With `J804_WINDOWS_INPUT` FALSE the
default, vUART, AHCI, USB and GPU FDs are byte-identical to those built
before the flag existed: the flag adds no C or ASL define, only the marker
block, one SSDT and one driver.

Status: built and host-tested only. Nothing here has run on hardware.

## What the image does

- PrePi, MemoryInitPeiLib and the panel checkpoints are the J804 M1 ones
  (`J804_M1` in C): the RAM top must be at or below 0x1059d600000
  (region-id-218, MTE tag storage, stays out), every ADT carve-out and
  memory-map entry in RAM is reserved, and the boot_args/ADT copies are
  checked (`J804BootHandoffIsSane`) before they are reserved. Before
  anything is copied, `J804BootHandoffSourcesClear` (J804_WINDOWS only)
  stops PrePi (gMuCheckpoint stage 0xE6, no panel block) when the copy
  range is not inside boot_args RAM or overlaps the launcher's boot_args
  or ADT source or the running FD.
- Idle never executes WFI (`J804CpuLib`) until WFI is measured on T8152.
  The carrier traps WFI anyway (HCR_EL2.TWI).
- AIC timer reflection (AppleAicV2Dxe) runs only at EL1, which is where this
  profile runs; at EL2 the native FIQ timer path is used.
- Serial is the Null library unless the build sets `J804_WINDOWS_VUART`.
- Variables are RAM-only (`PcdEmuVariableNvModeEnable`), Secure Boot is off,
  there is no driver for any real storage device, no USB driver and no NVRAM
  access (a `J804_WINDOWS_USB` build only describes usb-drd1 to Windows; Mu
  never touches it). Only a `J804_WINDOWS_AHCI` build carries an ATA stack, and it only
  ever talks to the carrier's emulated AHCI HBA (next section).
- `J804AcpiDxe` installs the ACPI tables only at EL1, on cpu6 (MPIDR
  0x10100), on a J804 ADT.
- EL1 under the carrier only. The default FD links `AuraArmExceptionLib`,
  which must never run in a native-EL2 image (its HVC forward has no
  CurrentEL check, and at EL2 the HVC traps to EL2 itself). For native EL2
  use the M1 FD (`Tools/build-j804-m1.sh`). An `NTASI_AURA_WINDOWS=FALSE`
  build drops the forwarding but is not tested at EL2 either.
- Reset (`J804Wd1ResetSystemLib`): at EL1, PSCI SYSTEM_RESET to the carrier
  (which shuts NVMe down and then restarts through WD1); the guest never
  writes WD1 and parks if the call returns. At EL2 (only in an
  `NTASI_AURA_WINDOWS=FALSE` build), WD1 directly (boot time only).
  Shutdown parks the CPU: there is no power-off.
- SMBIOS: Type 1/2/3 identity (model, product name, board name, serial
  numbers) is read from the ADT at run time (`PcdSmbiosLiftAdtIdentity`);
  only fallbacks are compiled in ("Mac18,1", "MacBook Pro (14-inch, M6)",
  "J804", "MacBook Pro"). Serial numbers are never logged.
- The Aura exception forwarding (`AuraArmExceptionLib`, NTSRG over HVC) and
  the phase-indicator wrapper (`AuraVarPolicyFoundationDxe`) are selected by
  `NTASI_AURA_WINDOWS` (default TRUE in this profile).
- `startup.nsh` prints `J804_MU_FIRMWARE_READY`, runs `J804BootProbe.efi`,
  `ver` and `map -r`, waits 30 s and resets. With an appended ramdisk,
  BootRamdiskHelperDxe boots `\EFI\BOOT\BOOTAA64.EFI` from it first.

## D1: the carrier's AHCI disk (`J804_WINDOWS_AHCI`)

Contract: `J804Windows.h`
(`J804_AHCI_*`, `J804_GSIV_AHCI`). The carrier emulates one AHCI 1.3.1 HBA
with one SATA port at ABAR 0xF40000000 (one hooked 16 KiB page), interrupt
SPI 990 (level, active-high, no AIC line), backed by a DRAM store at the top
of its guest RAM window that the guest never maps.

A `J804_WINDOWS_AHCI=TRUE` build adds:
- `\_SB.AHC0` in the DSDT: `_HID` NTAS2010 (project-local), `_CLS`
  01/06/01 (Windows' compatible ID `ACPI\CLS_0001&SUBCLS_0006&PI_01`,
  bound to storahci by mshdc.inf), `_CCA` 1, `_STA` 0xF, the ABAR page and
  GSIV 990. `_STA` is constant, so the launcher must not start this FD
  without an attached disk.
- A raw marker at FD offset 0x110: `"J804AHC1"`, u32 0xF40000 (ABAR >> 12),
  u32 990, for the launcher to check before it attaches a disk. Non-AHCI FDs
  have 0xFF there.
- `NonDiscoverablePciDeviceDxe`, `SataControllerDxe` (AtaAtapiPassThru
  needs its IdeControllerInit protocol), `AtaAtapiPassThru`, `AtaBusDxe` and
  `J804AhciDxe` in the FV; `PcdAtaSmartEnable` FALSE (the IDENTIFY data
  reports no SMART).
- `J804AhciDxe` (EL1 on a J804 only) acts only when the launcher wrote
  `/chosen/aura,windows-ahci-v1` (40 bytes, little-endian: u64 ABAR, u64
  ABAR size, u32 GSIV, u32 boot (0 disk, 1 ramdisk), u64 store base, u64
  store top; a 24-byte form without the store range is accepted with a
  warning) and every value matches `J804Windows.h`. It refuses the disk if
  the store overlaps anything in the GCD map (the launcher passes
  boot_args `mem_size` ending below the store, so Mu's memory map and the
  map Windows receives never contain it). Then it adds the ABAR to the GCD
  as uncached MMIO, registers it as a non-discoverable AHCI device (coherent
  DMA, class 01/06/01, BAR 5), connects it and looks for
  `\EFI\Microsoft\Boot\bootmgfw.efi`, then `\EFI\BOOT\BOOTAA64.EFI`, on
  the disk's file systems. Boot = disk: the "Windows Boot Manager (AHCI)"
  option goes first in BootOrder and becomes BootNext (overriding the
  appended RAM disk). Boot = ramdisk: it goes last and becomes BootNext only
  if BootRamdiskHelperDxe set none. No property, no disk, no loader: nothing
  changes (appended RAM disk, then the shell).
- It runs during DXE dispatch, not at EndOfDxe: BdsDxe reads BootNext before
  `PlatformBootManagerBeforeConsole` signals EndOfDxe. Its depex
  (`gEfiRamDiskProtocolGuid AND` the variable arch protocols) is a superset
  of BootRamdiskHelperDxe's and the FDF lists it after the helper, so the
  helper always runs first; the UEFI drivers it connects have no depex and
  run in the first dispatch pass.
- Host tests: `Tools/HostTests/J804AhciHandshake` (the property parser,
  ASan/UBSan) and the AHC0/marker/DSC checks in
  `Tests/test_j804_windows_acpi_contract.py`. The manifest checks AHC0, the
  marker and the five drivers against the `J804_WINDOWS_AHCI` build flag.

## D4: usb-drd1's xHCI for Windows (`J804_WINDOWS_USB`)

Contract: `J804Windows.h` (`J804_XHC*`,
`J804_GSIV_XHC1`, `J804_USB_*`). The carrier passes usb-drd1's xHCI register
window [0x40A430000, 0x40A438000) through (two 16 KiB stage-2 pages, IPA ==
PA; the dwc3 globals at +0xc100, the PHY, the DART and the rest of the port
stay denied), aliases AIC 1692 to the level SPI 995, keeps the dwc3 in the
USB2 host state aura's `j804_usb_prep` leaves, and maps exactly the guest RAM
window in a coherent Gen3 DART identity map that the guest cannot see.
usb-drd0 (the proxy link) is never described; usb-drd3 is not in this build
(0x130 is kept for its marker).

A `J804_WINDOWS_USB=TRUE` build (needs `J804_WINDOWS_AHCI=TRUE`) adds:
- `\_SB.XHC1` in its own SSDT (`AcpiTables/SsdtXhc1.asl`, OEM table ID
  `J804XHC1`, FFS `J804Xhc1Ssdt` F6D32646-0922-4EF3-8337-F5780E98B345), not in
  the DSDT: `_HID` PNP0D10 (usbxhci), `_UID` 1, `_CCA` 1, `_STA` 0xF,
  QWordMemory 0x40A430000-0x40A437FFF (0x8000) and Interrupt {995} level,
  active-high, exclusive. No `_PSx`, `_PRx`, `_PRW`, `_DSW` or `_S0W`:
  Windows has no ACPI way to power the controller off, and without `_S0W` it
  is not wake-capable in S0. No `_DSD`, `_DMA` or MSI.
- `J804XhciAcpiDxe`, which installs that SSDT only at EL1 on cpu6 of a J804
  and only when the launcher wrote `/chosen/aura,windows-usb-v1` (40 bytes,
  little-endian: u32 port mask = 0x2 (bit 1 = usb-drd1), u32 GSIV 995, u64
  xHCI base 0x40A430000, u64 xHCI size 0x8000, u64 DMA window base, u64 DMA
  window top; the window non-empty, 16 KiB aligned and clear of the xHCI
  window) after the carrier's USB ATTACH. It also refuses when any RAM in
  the GCD map lies outside the DMA window (a DMA buffer there would fault in
  the DART), when the xHCI window is RAM in the GCD map, and when the DMA
  window overlaps the AHCI store of `/chosen/aura,windows-ahci-v1` (or that
  property is malformed). Without the property, or with any refusal, Windows
  sees no XHC1 and never touches the xHCI window, so a USB FD is safe under
  a launcher with USB off.
- A raw marker at FD offset 0x120: `"J804USB1"`, u32 0x0040A430 (xHCI base
  >> 12), u32 995, for the launcher to check before it attaches USB.
  Non-USB FDs have 0xFF there.
- Host tests: `Tools/HostTests/J804UsbHandshake` (the property parser and the
  range helpers, ASan/UBSan) and the XHC1/marker/DSC/SSDT checks in
  `Tests/test_j804_windows_acpi_contract.py` (the SSDT is also compiled on
  its own with `cc -E` and `iasl`). The manifest checks the SSDT (exactly one
  platform SSDT, `J804XHC1`, declaring `\_SB.XHC1`), no XHC1 in the DSDT,
  the marker and the driver against the `J804_WINDOWS_USB` build flag.

## G0: the carrier-admitted GPU (`J804_WINDOWS_GPU`)

Contract: the carrier's GPU hand-off and
`AppleSiliconPkg/Include/J804GpuHandoff.h` (layout, rules, parser, SSDT
patcher; header-only so PrePi, the driver and the host test share it). Unlike
AHCI and USB, no GPU address, line or size is compiled in: the carrier admits
the GPU firmware against the live ADT, lifts every value from it into a
16 KiB read-only handoff page ("AGH1", version 1, CRC-32) outside guest RAM,
and the launcher (`run_guest.py --windows-gpu admit|log`) writes
`/chosen/aura,windows-gpu-v1` (16 bytes, little-endian: u64 page IPA, u64
page size 0x4000) only after ADMIT returned 0. In G0 nothing is powered,
started, passed through or published as an interrupt; the carrier only
hooks (logs) the sgx window and the post-office page.

A `J804_WINDOWS_GPU=TRUE` build (needs `J804_WINDOWS_AHCI=TRUE`) adds:
- PrePi (`AdtParser.c`, `PrePi.c`): the carrier carves the GPU pool from the
  top of its guest RAM window, which is the boot_args window, exactly where
  PrePi puts its stack and UEFI region. With the property present, PrePi
  cleans and invalidates the page to the point of coherency (the MMU is off,
  the reads are uncached), validates it (`J804GpuParseHandoff`) and, only if
  it validates and the FD, the boot hand-off copies and the new UEFI region
  all lie clear below the pool, lowers its RAM top to the pool base and adds
  the pool as a reserved resource HOB (cache capabilities, neither PRESENT
  nor INITIALIZED, so the DXE core never promotes it): the UEFI memory map
  reports it as `EfiReservedMemoryType` and Mu never maps it. Without the
  property, or with any failure, the RAM is unchanged.
- `\_SB.GPU0` in its own SSDT template (`AcpiTables/SsdtGpu0.asl`, OEM table
  ID `J804GPU0`, FFS `J804Gpu0Ssdt` 3C230D00-9C66-4CA9-BCAA-614E034EAAC2),
  never in the DSDT: `_HID` `J804_GPU0_HID` (the project's own ID,
  not an AuroraSilicon NTAS ID), `_UID` 0, `_CCA` 1, `_STA` 0xF,
  `_CRS` = four QWordMemory ranges whose values are unique 64-bit sentinels:
  the sgx window (handoff classes 1..5 tiled into one range; non-cacheable
  RW), the post-office page with both GFX windows (that 16 KiB page only;
  non-cacheable RW), the pool (cacheable RW) and the handoff page (cacheable
  RO); `_DSD` (device-properties UUID) `aura,handoff-ipa` (sentinel),
  `aura,handoff-size` 0x4000, `aura,handoff-version` 1. No interrupt (state
  bit 3 is clear in G0 and the lines' trigger type is unmeasured; completion
  is polled), no power or wake methods (the carrier owns PSD). The UAT,
  DATA and TEXT ranges are never listed.
- `J804GpuAcpiDxe`: at EL1 on cpu6 of a J804 and only with the property, it
  maps the handoff page write-back (coherent with the carrier's cached
  writes; it is read-only at stage 2), re-validates it, refuses unless the
  whole pool is reserved memory in the GCD map (proof that PrePi carved it)
  and the sgx window, mailbox page and handoff page are not RAM, and unless
  none of them overlaps the AHCI store; then it reads the template, replaces
  each sentinel (each must occur exactly once) and installs it. Otherwise
  Windows sees no GPU0. Mu never touches the GPU, the mailbox or the pool.
- Page rules beyond the carrier's layout: the pool is the one class-10 range,
  equal to the header's, 32 MiB aligned, ending exactly at the top of guest
  RAM and at most half of it; every other range lies outside guest RAM; the
  firmware classes inside DRAM, the sgx/ASC/mailbox classes outside it;
  classes 1..5 tile one window; exactly one mailbox page and one handoff
  page (equal to the handshake); state has the admitted bit and no unknown
  bit; four distinct lines in [32, 1020) outside the carrier block
  [980, 1000); reserved bytes zero; a printable receipt.
- No FD marker: the launcher reads none for the GPU.
- Host tests: `Tools/HostTests/J804GpuHandoff` (the property and page parser
  against pages built field by field from the documented layout with zlib's
  CRC, 66 refusal cases, and the SSDT patcher; ASan/UBSan; synthetic
  addresses only) and the GPU checks in
  `Tests/test_j804_windows_acpi_contract.py` (header constants against the
  carrier contract, the template's sentinels, DSC defaults, the SSDT compiled
  on its own with `cc -E` and `iasl`, patched by the host test and
  disassembled and recompiled). The manifest checks the J804GPU0 SSDT (one,
  declaring `\_SB.GPU0`, the HID, each sentinel once), no GPU0 in the DSDT,
  `J804GpuAcpiDxe` and the PrePi strings against the `J804_WINDOWS_GPU` flag.

## I0: keyboard and pointer for Windows (`J804_WINDOWS_INPUT`)

Contract: aura `docs/windows-t8152.md` ("The Mu side") and `J804Windows.h` (`J804_INPUT_*`,
`J804_GSIV_INPUT`). The carrier emulates one 16 KiB MMIO mailbox page at IPA
0xF50000000 (hooked and never mapped, every access trapped; 256 MiB above
the AHCI ABAR in the same `/arm-io` gap) and raises the purely virtual level
SPI 991 while reports are queued. It translates every source (Mac-fed frames
on the vUART RX first; the internal keyboard and trackpad later) into fixed
HID reports for our own `m6input.sys` HID minidriver. The device does no
DMA.

A `J804_WINDOWS_INPUT=TRUE` build (needs `J804_WINDOWS_AHCI=TRUE`) adds:
- `\_SB.INP0` in its own SSDT (`AcpiTables/SsdtInp0.asl`, OEM table ID
  `J804INP0`, FFS `J804Inp0Ssdt` 021B562E-3BD3-4324-BB92-25DB0ED09223), never
  in the DSDT: `_HID` NTAS2040 (`J804_INPUT_HID`, project-local; not
  AuroraSilicon's NTAS0050, which their AppleMtpHid binds, nor the Neo
  AURA0700), `_UID` 0, `_CCA` 1, `_STA` 0xF, QWordMemory
  0xF50000000-0xF50003FFF (0x4000) and Interrupt {991} level, active-high,
  exclusive. No `_PSx`, `_PRx`, `_PRW`, `_DSW`, `_S0W`, `_DSD` or `_DMA`.
- `J804InputAcpiDxe`, which installs that SSDT only at EL1 on cpu6 of a J804
  and only when the launcher wrote `/chosen/aura,windows-input-v1` (24 bytes,
  little-endian `<QIIII>`: u64 page base 0xF50000000, u32 size 0x4000, u32
  GSIV 991, u32 feature mask, u32 version 1) after the carrier's input
  ATTACH returned 0. The feature mask must have all four report bits 0-3
  (keyboard, absolute pointer, relative pointer, consumer), bits 4-15 and
  24-31 clear, and at least one source bit in 16-23 (`--windows-input host`
  writes 0x1000F). It also refuses unless the whole page is NonExistent in
  the GCD map. Without the property, or with any refusal (logged), Windows
  sees no INP0 and never touches the page, so an input FD is safe under a
  launcher with input off. Mu itself never touches the page and has no input
  driver: host frames sent while Mu runs only update the carrier's state.
- A raw marker at FD offset 0x140: `"J804INP1"`, u32 0x00F50000 (page >>
  12), u32 991, for the launcher to check, in both directions, before it
  attaches input. Non-input FDs have 0xFF there.
- Host tests: `Tools/HostTests/J804InputHandshake` (the property parser,
  ASan/UBSan, against bytes built field by field from the documented layout
  and the launcher's literal `struct.pack` output) and the INP0/marker/DSC/
  SSDT checks in `Tests/test_j804_windows_acpi_contract.py` (the SSDT is
  also compiled on its own with `cc -E` and `iasl`). The manifest checks the
  SSDT (one `J804INP0`, declaring `\_SB.INP0`, `_HID` NTAS2040), no INP0 in
  the DSDT, the marker and the driver against the `J804_WINDOWS_INPUT` flag.

## ACPI tables

| Table | Content |
|---|---|
| DSDT | `\_SB.CLU0` (E, cpu0-5), `CLU1` (P, cpu6-7), `CLU2` (M, cpu8-11), all ACPI0010 with ACPI0007 CPUs, `_UID` = ADT CPU index; `SMC0` NTAS2032 (ASC 0x30C600000/0x74000, SRAM 0x30DE00000/0x120000, mailbox page 0x30C710000/0x4000, polled), `_STA` 0 unless `J804_WINDOWS_SMC` (the T8152 carrier denies the SMC pages today); `ANS0` NTAS2004 only with `J804_WINDOWS_ANS` (ASC 0x321600000/0x74000, NVMe+NVMMU 0x325CC0000/0x60000, SART 0x325C50000/0xC000, mailbox page 0x316020000/0x4000, PMGR 0x300700000/0x4000, GSIV 996; the last two are shared pages, see below); `COM0` only with `J804_WINDOWS_VUART` |
| MADT | 12 GICCs, cpu6 (0x10100) first, then 0x10101, 0x40102-0x40105, 0x0-0x5; efficiency class (ACPI: lower = more power efficient) E 0, M 1, P 2 (order by cluster role, not measured); GICD 0xF00000000 (v3); GICR range 0xF10000000 + 12 x 0x20000; no ITS, no MSI frame, no PMU or maintenance interrupt |
| GTDT | EL1 physical timer INTID 17, virtual 18, level/high; no secure, EL2 or platform timers |
| FADT | HW_REDUCED_ACPI, PSCI compliant (SMC), PreferredPmProfile MOBILE, no low-power S0 idle |
| DBG2 | only with `J804_WINDOWS_VUART`: \_SB.COM0 at 0x331200000 |
| DSDT AHC0 | only with `J804_WINDOWS_AHCI`: NTAS2010, `_CLS` 01/06/01, `_CCA` 1, ABAR 0xF40000000/0x4000, GSIV 990 level/high |
| SSDT J804XHC1 | only with `J804_WINDOWS_USB`, installed only with the `/chosen/aura,windows-usb-v1` handshake: `\_SB.XHC1` PNP0D10, `_UID` 1, `_CCA` 1, 0x40A430000/0x8000, GSIV 995 level/high, no power methods |
| SSDT J804INP0 | only with `J804_WINDOWS_INPUT`, installed only with the `/chosen/aura,windows-input-v1` handshake: `\_SB.INP0` NTAS2040, `_UID` 0, `_CCA` 1, 0xF50000000/0x4000, GSIV 991 level/high, no power methods, no `_DSD` |
| SSDT J804GPU0 | only with `J804_WINDOWS_GPU`, patched from the carrier's handoff page and installed only with the `/chosen/aura,windows-gpu-v1` handshake: `\_SB.GPU0` `J804_GPU0_HID`, `_UID` 0, `_CCA` 1, sgx window, GFX post-office page, pool, handoff page; `_DSD` handoff IPA/size; no interrupt, no power methods |

Not described, by design: speaker amps, i2c1, i2c3, AOP, ADMAC, the hpm
USB-PD controllers, GPIO 0xca (193), the xHCI controllers (except XHC1 in
the gated SSDT of a `J804_WINDOWS_USB` build), the QSPI NVRAM (spi
controller and NOR).

ANS mailbox: 0x316020300 is the Linux-proven post-office window 3 (our
`t8152-j804.dts` ans_mbox, IRQs 302/262). It is not taken from the live ADT,
whose `/arm-io/ans` reg[2] (0x316000000 + 0x1c000) does not cover it, and it
is not J873's 0x316010000 page. The ADT post-office rule would give
0x316010300; which window Windows needs is unverified.

ANS0's PMGR and mailbox resources are shared pages, not a mapping request.
The PMGR page 0x300700000 holds ps_ans (+0x8a0) but also ps_spi_p (+0xd40)
and ps_qspi (+0x1140); the mailbox page 0x316020000 holds the DCP mailbox
(+0x500) and is reg[2] of dcp, dcpext0/1, gfx-asc, gfx1-asc, isp, pmp and
scaler0 in the ADT. The carrier must never map either page: it traps them
and passes through only the 4-byte ps_ans register and the 0x80-byte
mailbox window 0x316020300 (`J804_ANS_*` in J804Windows.h), or it powers
ANS itself and the PMGR resource is dropped. The default build emits no
ANS0, and the contract test forbids ps_spi_p, ps_qspi and the DCP mailbox
in any resource.

## Contract with the carrier (T8152 port of src/hv/windows)

These values live in `Platform/MacBookPro2026Pkg/Include/J804Windows.h` and
must match the carrier; neither side can discover the other's choice.

| Item | Mu side | Carrier side to provide |
|---|---|---|
| vGIC bases | GICD 0xF00000000, GICR 0xF10000000 (frames 0x20000) | `hv_vgicv3_init` T8152 case with `DIST_BASE_36_BIT`/`REDIST_BASE_36_BIT` (present in aura-win `j804-windows`, hv_vgic.c T8152 case; not run) |
| Redistributors | static: 12 GICCs all ENABLED, 12 x 0x20000 discovery range | one dense frame per live CPU, `GICR_TYPER.Last` on the last; affinity from `smp_get_mpidr`. The carrier sizes the vGIC to `hv_vgic_live_cpu_count()`, so it must **refuse** a T8152 Windows launch unless all 12 CPUs are live (there is no live mask Mu could patch the MADT from). Not implemented in aura-win yet |
| Timers | INTID 17 (phys), 18 (virt) | `HV_GIC_TIMER_P_INTID`/`_V_INTID` 17/18 |
| AIC aliases | GSIV 995/996/997 | `hv_aic_alias` T8152 table {995, 1692}, {996, 1349}, {997, 1834}; 995 only while the USB ATTACH holds, 997 not used by any build |
| PSCI | SMC conduit; CPU_ON by MPIDR | CPU_ON = spin-table release, CPU_OFF = park (never a power-off), SYSTEM_RESET = NVMe shutdown then WD1, SYSTEM_OFF = no power-off |
| Timer reflection | `smc #0` with x0 = "NTAIC" | the existing NTAIC handler, AIC3 slots at NumIrqs (0x960 on J804) minus 2 x slots |
| Sysreg forwarding | `hvc #0` with x0 = "NTSRG" | the existing NTSRG handler |
| Boot hand-off copies | PrePi copies boot_args to 0x10220000000 (16 KiB) and the ADT to 0x10220004000 (2 MiB) before anything else; it stops if [0x10220000000, 0x10220204000) is not inside boot_args RAM or overlaps the boot_args or ADT source or the FD | T8152 profiles take no `reserved_memory`: `guest_ram {base, top}` is the whole guest, and load_raw packs ADT, TrustCache, the FD (64 KiB aligned), SEPFW/preoslog and boot_args upwards from base + 16 MiB (total L). So: base <= 0x10220000000 and top >= 0x10220204000 + 128 MiB (UEFI region at the top, clear of the copies), and the load clear of the copies: either base in [0x1021F204000, 0x10220000000] (copies in the first 16 MiB, load above them) or base + 16 MiB + L <= 0x10220000000; top <= 0x1059d600000. On the saved J804 ADT the gap 0x1013b2e4000..0x1059d600000 is free apart from region-id-24, which `hv_t8152_set_guest_ram` refuses to overlap. Nothing in the launcher checks the copy range; PrePi fails closed |
| RAM top | PrePi stops above 0x1059d600000 | guest_ram top (the boot_args RAM top the guest sees) <= 0x1059d600000 |
| FD | 0x1E00000 bytes, entry at offset 0, 64 KiB aligned | `run_guest.py -r -E 0`, placed outside CTXR_D |
| AHCI disk (D1, AHCI builds) | ABAR 0xF40000000/0x4000, GSIV 990 level/high, `_CCA` 1; FD marker at 0x110; acts only on `/chosen/aura,windows-ahci-v1` | hook the ABAR page (never map it), deliver SPI 990 as a level through GICD_IROUTER, keep the store outside guest RAM and stage 2; write the 40-byte property only after ATTACH; refuse an AHCI FD (marker present) without an attached disk |
| USB (D4, USB builds) | XHC1 0x40A430000/0x8000, GSIV 995 level/high, `_CCA` 1, no power methods; FD marker at 0x120; XHC1 SSDT installed only on `/chosen/aura,windows-usb-v1` | map exactly [0x40A430000, 0x40A438000) Device-nGnRnE IPA == PA, nothing else of usb-drd1; alias AIC 1692 to level SPI 995; coherent Gen3 DART identity map of exactly the DMA window it writes into the property (the guest RAM window, never the AHCI store or carrier memory); dwc3 left as `j804_usb_prep` sets it; write the 40-byte property only after ATTACH succeeded, with port mask 0x2; refuse a USB FD (marker present) without ATTACH and `--windows-usb drd1` with an FD lacking the marker |
| GPU (G0, GPU builds) | GPU0 SSDT installed only on `/chosen/aura,windows-gpu-v1` and a handoff page that validates (J804GpuHandoff.h); RAM top lowered to the pool base and the pool reserved by PrePi; no interrupt, no power methods | build the page from the live ADT outside guest RAM, map it read-only at IPA == PA, write the 16-byte property only after ADMIT returned 0; carve the pool from the very top of the guest RAM window (= boot_args top), 32 MiB aligned, at most half; with the pool the RAM top Mu uses is the pool base, so `pool base >= 0x10220204000 + 128 MiB` (UEFI region clear of the copies); clean the page to the point of coherency after building it (Mu also cleans it before its uncached PrePi read) |
| Input (I0, input builds) | INP0 0xF50000000/0x4000, GSIV 991 level/high, `_HID` NTAS2040; FD marker at 0x140; INP0 SSDT installed only on `/chosen/aura,windows-input-v1` | configure BIT(10), CAPS2 bit 3, `P_HV_WINDOWS_INPUT` 0xc89; hook the page (never map it), raise SPI 991 as a purely virtual level (`ENABLE && IRQ_EN && queue non-empty`); write the 24-byte property only after ATTACH returned 0; refuse an input FD (marker present) with `--windows-input off` and `--windows-input host` with an FD lacking the marker or naming another page or GSIV |
| Stage 2 | the default build enables no MMIO device: only guest RAM, the framebuffer shadow, AIC3 and the hooked vGIC window are used (SMC0 `_STA` 0, no ANS0) | aura-win's real policy: an allowlist of the guest RAM window, carrier-registered pages (BGRA shadow, read-only ADT alias) and the AIC (Device-nGnRnE), plus hooked MMIO; an ADT denylist (qspi, sep, aop, admac, i2c, audio, gpio, wdt, pmgr, spmi, dart, sart, ans, smc, mtp, spi, dcp, sio, usb-drd0, 0x331118000, region-id-218/-4) checked at map time and before entry; HCR_EL2.TWI set. Enabling SMC0 or ANS0 needs an explicit carrier allowlist (ANS: trapped windows only, see above) |

## Not verified

- Nothing has run on hardware: EL1 entry under the carrier, the vGIC, PSCI,
  timer delivery, ACPI install, SMBIOS lifting and the reset paths are all
  unexercised.
- The carrier port (aura-win `j804-windows`) is built and host-tested only;
  every row of the contract table above is a requirement on it, checked
  against its source but not on hardware. The 12-live-CPU refusal is not in
  it yet.
- MemoryInitPeiLib's chainload-block reservation follows `chainload.py
  --raw` packing; whether the carrier's guest loader packs SEPFW,
  TrustCache and preoslog the same way after the FD is unverified.
- Windows behaviour with three efficiency classes, without a PPTT, and with
  ANS0 enabled (no J804 NVMe driver exists; AuroraSilicon AppleNvme is
  reference-only and hard-codes J873).
- Whether a Windows debugger transport exists for the DBG2 port subtype.
- D4: usbxhci on this dwc3 through the carrier's passthrough and DART map,
  Windows' ACPI handling of an SSDT-only XHC1, and whether the GCD RAM check
  of `J804XhciAcpiDxe` matches the RAM PrePi describes under the AHCI launch
  (it fails closed: no XHC1, with a log line).
- G0: PrePi's uncached read of the carrier's page under the carrier (the
  clean-and-invalidate before it, a read-only stage-2 page), the DXE
  write-back mapping of a GCD-added page outside RAM, Windows' handling of an
  SSDT-only device with a reserved-memory resource and a `_DSD`, and whether
  a driver can map the pool cached. No driver binds `J804_GPU0_HID` yet.
  "Admit once" is per carrier session on the carrier side.
- I0: Windows' handling of an SSDT-only INP0 and of a level SPI with no AIC
  line behind it; `m6input.sys` has never been loaded; SPI 991 free on the
  live ADT (980..999 were free except 984 in the last walk); whether the
  page is NonExistent in the GCD map under the real launch (it fails closed:
  no INP0, with a log line).
- D1: AtaAtapiPassThru, AtaBusDxe and storahci against the carrier's model;
  whether CpuDxe maps the GCD-added ABAR as expected on this TIANO
  (ArmSetMemoryAttributes on an unmapped range); `NTAS2010` against the IDs
  of other drivers that might be installed.

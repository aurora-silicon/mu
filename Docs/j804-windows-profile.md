# J804 windows profile (Platform/MacBookPro2026Pkg)

Mu for the MacBook Pro 14-inch, M6 (J804, T8152), running at EL1 as the guest
of the EL2 carrier: aura's `WINDOWS=1` personality ported to T8152 (PLAN 0.5,
option H1). Build: `sh Tools/build-j804-windows.sh` on branch `j804-windows`;
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
- Idle never executes WFI (`J804CpuLib`), until PLAN Phase 3(a) measures WFI
  on T8152. The carrier traps WFI anyway (HCR_EL2.TWI).
- AIC timer reflection (AppleAicV2Dxe) runs only at EL1, which is where this
  profile runs; at EL2 the native FIQ timer path is used.
- Serial is the Null library unless the build sets `J804_WINDOWS_VUART`.
- Variables are RAM-only (`PcdEmuVariableNvModeEnable`), Secure Boot is off,
  there is no Mu storage driver, no USB driver and no NVRAM access.
- `J804AcpiDxe` installs the ACPI tables only at EL1, on cpu6 (MPIDR
  0x10100), on a J804 ADT.
- EL1 under the carrier only. The default FD links `AuraArmExceptionLib`,
  which AUDIT-0.8 forbids in a native-EL2 image (its HVC forward has no
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

## ACPI tables

| Table | Content |
|---|---|
| DSDT | `\_SB.CLU0` (E, cpu0-5), `CLU1` (P, cpu6-7), `CLU2` (M, cpu8-11), all ACPI0010 with ACPI0007 CPUs, `_UID` = ADT CPU index; `SMC0` NTAS2032 (ASC 0x30C600000/0x74000, SRAM 0x30DE00000/0x120000, mailbox page 0x30C710000/0x4000, polled), `_STA` 0 unless `J804_WINDOWS_SMC` (the T8152 carrier denies the SMC pages today); `ANS0` NTAS2004 only with `J804_WINDOWS_ANS` (ASC 0x321600000/0x74000, NVMe+NVMMU 0x325CC0000/0x60000, SART 0x325C50000/0xC000, mailbox page 0x316020000/0x4000, PMGR 0x300700000/0x4000, GSIV 996; the last two are shared pages, see below); `COM0` only with `J804_WINDOWS_VUART` |
| MADT | 12 GICCs, cpu6 (0x10100) first, then 0x10101, 0x40102-0x40105, 0x0-0x5; efficiency class (ACPI: lower = more power efficient) E 0, M 1, P 2 (order by cluster role, not measured); GICD 0xF00000000 (v3); GICR range 0xF10000000 + 12 x 0x20000; no ITS, no MSI frame, no PMU or maintenance interrupt |
| GTDT | EL1 physical timer INTID 17, virtual 18, level/high; no secure, EL2 or platform timers |
| FADT | HW_REDUCED_ACPI, PSCI compliant (SMC), PreferredPmProfile MOBILE, no low-power S0 idle |
| DBG2 | only with `J804_WINDOWS_VUART`: \_SB.COM0 at 0x331200000 |

Not described, by design: speaker amps, i2c1, i2c3, AOP, ADMAC, the hpm
USB-PD controllers, GPIO 0xca (193), the xHCI controllers, the QSPI NVRAM
(spi controller and NOR). `J804_WINDOWS_USB=TRUE` is refused by the DSC
until the USB prerequisite of PLAN 0.6 holds.

ANS mailbox: 0x316020300 is the Linux-proven post-office window 3 (our
`t8152-j804.dts` ans_mbox, IRQs 302/262). It is not taken from the live ADT,
whose `/arm-io/ans` reg[2] (0x316000000 + 0x1c000) does not cover it, and it
is not J873's 0x316010000 page. The ADT post-office rule would give
0x316010300 (AUDIT-0.8 7.2); which window Windows needs is unverified.

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
| AIC aliases | GSIV 995/996/997 | `hv_aic_alias` T8152 table {995, 1692}, {996, 1349}, {997, 1834}; 995/997 only once USB exists |
| PSCI | SMC conduit; CPU_ON by MPIDR | CPU_ON = spin-table release, CPU_OFF = park (never a power-off), SYSTEM_RESET = NVMe shutdown then WD1, SYSTEM_OFF = no power-off |
| Timer reflection | `smc #0` with x0 = "NTAIC" | the existing NTAIC handler, AIC3 slots at NumIrqs (0x960 on J804) minus 2 x slots |
| Sysreg forwarding | `hvc #0` with x0 = "NTSRG" | the existing NTSRG handler |
| Boot hand-off copies | PrePi copies boot_args to 0x10220000000 (16 KiB) and the ADT to 0x10220004000 (2 MiB) before anything else; it stops if [0x10220000000, 0x10220204000) is not inside boot_args RAM or overlaps the boot_args or ADT source or the FD | T8152 profiles take no `reserved_memory`: `guest_ram {base, top}` is the whole guest, and load_raw packs ADT, TrustCache, the FD (64 KiB aligned), SEPFW/preoslog and boot_args upwards from base + 16 MiB (total L). So: base <= 0x10220000000 and top >= 0x10220204000 + 128 MiB (UEFI region at the top, clear of the copies), and the load clear of the copies: either base in [0x1021F204000, 0x10220000000] (copies in the first 16 MiB, load above them) or base + 16 MiB + L <= 0x10220000000; top <= 0x1059d600000. On the saved J804 ADT the gap 0x1013b2e4000..0x1059d600000 is free apart from region-id-24, which `hv_t8152_set_guest_ram` refuses to overlap. Nothing in the launcher checks the copy range; PrePi fails closed |
| RAM top | PrePi stops above 0x1059d600000 | guest_ram top (the boot_args RAM top the guest sees) <= 0x1059d600000 |
| FD | 0x1E00000 bytes, entry at offset 0, 64 KiB aligned | `run_guest.py -r -E 0`, placed outside CTXR_D |
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

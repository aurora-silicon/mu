# J804 windows profile (Platform/MacBookPro2026Pkg)

Mu for the MacBook Pro 14-inch, M6 (J804, T8152), running at EL1 as the guest
of the EL2 carrier: aura's `WINDOWS=1` personality ported to T8152 (PLAN 0.5,
option H1). Build: `sh Tools/build-j804-windows.sh` on branch `j804-windows`;
output `../out/j804-windows/artifacts/J804WIN_EFI.fd` with `MANIFEST.txt`.

Status: built and host-tested only. Nothing here has run on hardware.

## What the image does

- PrePi, MemoryInitPeiLib and the panel checkpoints are the J804 M1 ones
  (`J804_M1` in C): the RAM top must be at or below 0x1059d600000
  (region-id-218, MTE tag storage, stays out), every ADT carve-out and
  memory-map entry in RAM is reserved, and the boot_args/ADT copies are
  checked (`J804BootHandoffIsSane`) before they are reserved.
- Idle never executes WFI (`J804CpuLib`), until PLAN Phase 3(a) measures WFI
  on T8152. The carrier traps WFI anyway (HCR_EL2.TWI).
- AIC timer reflection (AppleAicV2Dxe) runs only at EL1, which is where this
  profile runs; at EL2 the native FIQ timer path is used.
- Serial is the Null library unless the build sets `J804_WINDOWS_VUART`.
- Variables are RAM-only (`PcdEmuVariableNvModeEnable`), Secure Boot is off,
  there is no Mu storage driver, no USB driver and no NVRAM access.
- `J804AcpiDxe` installs the ACPI tables only at EL1, on cpu6 (MPIDR
  0x10100), on a J804 ADT. Started natively at EL2 the same FD behaves like
  M1: shell, no ACPI.
- Reset (`J804Wd1ResetSystemLib`): at EL1, PSCI SYSTEM_RESET to the carrier
  (which shuts NVMe down and then restarts through WD1); the guest never
  writes WD1 and parks if the call returns. At EL2, WD1 directly (boot time
  only). Shutdown parks the CPU: there is no power-off.
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
| DSDT | `\_SB.CLU0` (E, cpu0-5), `CLU1` (P, cpu6-7), `CLU2` (M, cpu8-11), all ACPI0010 with ACPI0007 CPUs, `_UID` = ADT CPU index; `SMC0` NTAS2032 (ASC 0x30C600000/0x74000, SRAM 0x30DE00000/0x120000, mailbox page 0x30C710000/0x4000, polled); `ANS0` NTAS2004 (ASC 0x321600000/0x74000, NVMe+NVMMU 0x325CC0000/0x60000, SART 0x325C50000/0xC000, mailbox page 0x316020000/0x4000, PMGR 0x300700000/0x4000, GSIV 996), `_STA` 0 unless `J804_WINDOWS_ANS`; `COM0` only with `J804_WINDOWS_VUART` |
| MADT | 12 GICCs, cpu6 (0x10100) first, then 0x10101, 0x40102-0x40105, 0x0-0x5; efficiency P 0, M 1, E 2; GICD 0xF00000000 (v3); GICR range 0xF10000000 + 12 x 0x20000; no ITS, no MSI frame, no PMU or maintenance interrupt |
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

## Contract with the carrier (T8152 port of src/hv/windows)

These values live in `Platform/MacBookPro2026Pkg/Include/J804Windows.h` and
must match the carrier; neither side can discover the other's choice.

| Item | Mu side | Carrier side to provide |
|---|---|---|
| vGIC bases | GICD 0xF00000000, GICR 0xF10000000 (frames 0x20000) | `hv_vgicv3_init` T8152 case with `DIST_BASE_36_BIT`/`REDIST_BASE_36_BIT` (today there is no T8152 case: it prints "unsupported chip_id") |
| Redistributors | 12 x 0x20000 discovery range | one dense frame per live CPU, `GICR_TYPER.Last` on the last; affinity from `smp_get_mpidr` |
| Timers | INTID 17 (phys), 18 (virt) | `HV_GIC_TIMER_P_INTID`/`_V_INTID` 17/18 |
| AIC aliases | GSIV 995/996/997 | `hv_aic_alias` T8152 table {995, 1692}, {996, 1349}, {997, 1834}; 995/997 only once USB exists |
| PSCI | SMC conduit; CPU_ON by MPIDR | CPU_ON = spin-table release, CPU_OFF = park (never a power-off), SYSTEM_RESET = NVMe shutdown then WD1, SYSTEM_OFF = no power-off |
| Timer reflection | `smc #0` with x0 = "NTAIC" | the existing NTAIC handler, AIC3 slots at NumIrqs (0x960 on J804) minus 2 x slots |
| Sysreg forwarding | `hvc #0` with x0 = "NTSRG" | the existing NTSRG handler |
| Boot hand-off copies | boot_args at 0x10220000000 (16 KiB), ADT at 0x10220004000 (2 MiB) | `reserved_memory` in the launch profile: `{"base": "0x10220000000", "size": "0x204000"}` plus the 128 MiB UEFI workspace from the RAM top |
| RAM top | PrePi stops above 0x1059d600000 | boot_args `mem_size` trimmed so phys_base + mem_size <= 0x1059d600000 |
| FD | 0x1E00000 bytes, entry at offset 0, 64 KiB aligned | `run_guest.py -r -E 0`, placed outside CTXR_D |
| Stage 2 | uses only RAM, the framebuffer, AIC3, the described MMIO, the vGIC window | keep CTXR_D, aura, SEP and firmware carve-outs, the QSPI NVRAM and the whole spi controller/NOR, speaker amps, i2c audio, AOP and ADMAC out; device memory nGnRnE; HCR_EL2.TWI set |

## Not verified

- Nothing has run on hardware: EL1 entry under the carrier, the vGIC, PSCI,
  timer delivery, ACPI install, SMBIOS lifting and the reset paths are all
  unexercised.
- The carrier has no T8152 port yet; every row of the contract table above
  is a requirement on it, not a fact about it.
- MemoryInitPeiLib's chainload-block reservation follows `chainload.py
  --raw` packing; whether the carrier's guest loader packs SEPFW,
  TrustCache and preoslog the same way after the FD is unverified.
- Windows behaviour with three efficiency classes, without a PPTT, and with
  ANS0 enabled (no J804 NVMe driver exists; AuroraSilicon AppleNvme is
  reference-only and hard-codes J873).
- Whether a Windows debugger transport exists for the DBG2 port subtype.

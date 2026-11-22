# Provenance: J804 Windows profile

This branch adds Mu support for the J804 (MacBook Pro 14-inch, M6, T8152):

- the J804 `windows` profile (`Platform/MacBookPro2026Pkg`), Mu at EL1 as the
  guest of the aura EL2 carrier, which boots Windows on ARM;
- the J804 M1 profile (in `Platform/MacMini2026Pkg`), Mu natively at EL2.

It is based on aurora-silicon/mu `j813-windows`, and also carries Acelogic's
`j873` commit (the T8152 platform for the J873 Mac mini), which J804 builds
on.

## Sources and rules

- No Apple code, decompiled Apple logic or copied Apple data, firmware or
  blobs. Hardware addresses and interrupt numbers come from our J804 device
  tree, our Linux bring-up notes and a read of the J804 device tree (ADT) on
  our own machine (values only; no ADT content is committed).
- No machine identifiers are committed. SMBIOS serial numbers, the board
  serial and the model are read from the ADT at run time
  (`PcdSmbiosLiftAdtIdentity`, off for every other platform).
- No Windows WDK/SDK or other Microsoft files beyond the Mu submodules.
- Upstream code is used unmodified from the pinned submodules. Changes to it
  live in `Silicon/Apple/AppleSiliconPkg/Override`, which the build proves
  from the build report; the submodules stay clean.
- Every reused file keeps its original header and licence notice.

## What came from where

| Path | Origin | Licence |
|---|---|---|
| `Platform/MacMini2026Pkg`, `Silicon/Apple/T8152FamilyPkg` | Acelogic's `j873` (T8152 platform), extended for J804 M1 | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg` (DSC, FDF, apriori list, `.dec`) | derived from MacMini2026Pkg | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg/PlatformBuild.py` | the shared `Platform/PlatformBuildCommon.py` wrapper | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg/AcpiTables/*` | written for J804; structure follows MacMini2026Pkg's J873 tables; values from the J804 device tree and the aura carrier's interface (vGIC, PSCI, exception and device emulation) | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg/Applications/J804BootProbe` | derived from MacMini2026Pkg's J873BootProbe | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg/Drivers/*` (ACPI install, AHCI, GPU, input, xHCI handshakes) | written for J804 | BSD-2-Clause-Patent |
| `Platform/MacBookPro2026Pkg/Library/J804Wd1ResetSystemLib` | API of TIANO `ArmPsciResetSystemLib`; the WD1 register sequence follows the public Linux `apple_wdt` driver (register facts) | BSD-2-Clause-Patent |
| `J804BootHandoffIsSane` (`Silicon/Apple/AppleSiliconPkg/Include/J804M1.h`) | a port of the ReserveBootHandoff check in the T8142 `MemoryInitPeiLib` (Acelogic), attributed in the function comment | BSD-2-Clause-Patent |
| `J804_WINDOWS` / `J804_M1` switches in AppleSiliconPkg (PrePi, AIC, UART, SMBIOS, boot RAM disk) | written for J804, next to the matching `J873_WINDOWS` switches | BSD-2-Clause-Patent |
| `Tests/test_j804_*`, `Tests/j804/*`, `Tools/HostTests/J804*` | written for J804 | BSD-2-Clause-Patent; the appended RAM disk tests are MIT |
| `Tools/build-j804-*.sh`, `Tools/build-j873-native.sh`, `Tools/check-j804-windows-inputs.py`, `Tools/wm6-*`, `Tools/j804-winpe-ramdisk.py` | written for J804 and J873 | MIT |

The Aura Windows firmware pieces the J804 windows FD links
(`AuraArmExceptionLib`, `AuraVarPolicyFoundationDxe`) come from the base
branch unchanged. The fix to the EL0 stack alignment mask in the
ArmExceptionLib override is ours.

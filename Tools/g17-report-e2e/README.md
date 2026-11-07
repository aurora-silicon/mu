# G17 diagnostic report end-to-end validation

This runner consumes serialized ADT and BootArgs artifacts produced by Aura's
real Windows `HardwareSession` path. It executes Mu's actual EarlySetup, ADT
parser, PEI reservations, GUID HOB validation, DXE exclusion checks, report
constructor/publisher and ReadyToBoot verification. Host services substitute
owned RAM, EFI HOBs, memory-map, ACPI protocols and the two fixed metadata-copy
destinations. ASan and UBSan are enabled. No device is accessed.

The report remains disabled unless Aura explicitly admits
`j813-resource-report-v1`. Preservation-only boots publish no report. Mu requires
the exact matching preservation record, single GUID HOB and memory exclusions,
rejects duplicate admission properties or existing AG17 tables, and installs one
724-byte table. At ReadyToBoot it rechecks the preservation contract and requires
exactly one byte-identical report in the final ACPI inventory.

Read [PROTOCOL.md](PROTOCOL.md) and [FAILURE-MODEL.md](FAILURE-MODEL.md) first.
The failure scenarios were written before production code. `--red` records the
initial missing-publication case against the previous implementation; it is not
an alternate firmware mode.

```sh
python3 Tools/g17-report-e2e/run.py \
  --fixtures /absolute/path/to/aura/build/g17-report-host-01 \
  --output Build/G17ReportE2E
```

The fixture root contains `report`, `disabled`, and `preserve-only` directories,
each with `guest.adt`, `guest-bootargs.bin`, and (when enabled) `record.bin`.
The runner records source and input hashes, exact commands, outcomes, the
installed `AG17.bin`, and resource-HOB/EFI-map JSON. The compiled consumer receipt
also hashes all recursive compiler inputs. Semantic mutations repair checksums;
serialized ADT size changes update the actual BootArgs extent.

The reusable consumer accepts its existing positional inputs and adds
`--acpi-output /absolute/path/AG17.bin`. Absent reports produce an empty file.
Report admission failures exit 23; rejected repeat publication exits 24;
ReadyToBoot failures exit 22. Diagnostic fault flags are host harness options,
never firmware configuration.

The portable MIT header and implementation are
`Include/IndustryStandard/AuraG17Report.h` and
`Library/AuraG17PreservationLib/AuraG17Report.c` under `AppleSiliconPkg`.
They construct and validate the exact wire format without UEFI types. Their
dependency on the existing preservation validator remains BSD-2-Clause-Patent.
Standalone table validation proves syntax and fixed profile consistency only;
Mu additionally binds the exact live ADT, BootArgs, preserved HOB and memory map.

For the native J813 firmware build, initialize this checkout's submodules and
external dependencies, then run:

```sh
env AURORADBG_MU_OUTPUT_ROOT="$PWD/Build/G17ReportNative" \
  AURORADBG_LLD_LINK=/opt/homebrew/bin/lld-link AURORADBG_MU_JOBS=8 \
  Tools/build-j813-windows-native.sh internal-storage-windows
```

These artifacts establish offline producer/firmware/reader interoperability and
native compilation. They do not establish a Windows execution, live MMIO,
interrupt routing, GPU startup or WDDM qualification. All report authority
fields remain zero; existing aliases, ACPI device namespace and retained-memory
lifetime are unchanged.

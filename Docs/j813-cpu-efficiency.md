# J813 processor efficiency classes

## Failure model (recorded before implementation)

The J813 MADT at baseline `81fc2821d99aabd02e05f8f40b440135848fd220`
labels E-core UIDs 0–5 as class 1 and P-core UIDs 6–9 as class 0.
ACPI 6.5 Table 5.36 defines lower numbers as more power efficient. Windows'
`SYSTEM_CPU_SET_INFORMATION.EfficiencyClass` uses the same polarity.

The correction must avoid these failures:

- Reversing the polarity again, or changing only some of the ten GICC entries.
- Moving the bootstrap P-core (UID 6 / MPIDR 0x10100) from the first entry.
- Changing any UID, MPIDR, interrupt, enable flag, address, table revision or
  other ACPI structure while changing the class byte.
- Checking source literals without verifying the ARM64-compiled ACPI table.
- Validating a stale table, or a table that is absent from the built firmware.
- Comparing against an unrelated or modified baseline table.
- Confusing corrected firmware metadata with measured Windows scheduling,
  energy savings, clock control, or hardware qualification.

Validation must decode the real compiled MADT, check all ten class bytes, and
compare it byte-for-byte with the baseline except for those bytes and the ACPI
checksum. The full native ARM64 firmware build must succeed, and its packaged
table must match the validated compiler output. Preserve hashes, decoded
entries, exact changed offsets, build commands and artifacts in a repeatable
receipt. A future hardware check must read the installed MADT and Windows CPU
sets again; do not infer a logical-processor-to-MPIDR mapping from list order.

This change does not modify PPTT/cache descriptions, CPU ordering, DVFS or
the CPU-frequency handoff. No deployment or hardware action is part of the
offline validation.

## Public references

- [ACPI 6.5, GICC structure, Table 5.36](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#generic-interrupt-controller-cpu-interface-gicc-structure)
- [Microsoft SYSTEM_CPU_SET_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_cpu_set_information)
- [Exact public MADT baseline](https://github.com/aurora-silicon/mu/blob/81fc2821d99aabd02e05f8f40b440135848fd220/Silicon/Apple/T8142FamilyPkg/AcpiTables/MADT_Static.aslc)

The baseline file was retrieved anonymously from the public repository and
matched local SHA-256
`d551911ceb3bcd5c6e5f6039e59eb79c85434b3a176d4b175ad8e5ba2c088001`.

## Repeatable artifact verification

Build the baseline commit in a separate checkout with
`Tools/build-j813-windows-native.sh internal-storage-windows`. Preserve its
`Build/MacBookAir2026-AARCH64/DEBUG_CLANGPDB/AARCH64/T8142FamilyPkg/AcpiTables/CPUAcpiTables/OUTPUT/MADT_Static.acpi`
as `baseline-MADT_Static.acpi`. The verifier pins its SHA-256 to
`c25ce320cf5fc91999d04507c2c7c6aade87bc483e8154dcb4f5272b3751bf52`.
The zero checksum byte is the build-time placeholder; AcpiTableDxe fills it
when installing the table.

Build the candidate with the same command, then run from its checkout:

```sh
python3 Tests/e2e/cpu-efficiency.py \
  --baseline-table /absolute/path/baseline-MADT_Static.acpi \
  --table Build/MacBookAir2026-AARCH64/DEBUG_CLANGPDB/AARCH64/T8142FamilyPkg/AcpiTables/CPUAcpiTables/OUTPUT/MADT_Static.acpi \
  --fd Build/MacBookAir2026-AARCH64/DEBUG_CLANGPDB/FV/J813MACBOOKAIR2026_EFI.fd \
  --out build/cpu-efficiency-verified
```

The output directory must be new. It contains the exact compiler table, a
separately labelled calculation of the installed checksum, and a JSON receipt
binding both source files, the verifier, firmware, decoded GICC entries and
the ten changed offsets. The verifier decompresses the actual firmware's
bounded LZMA section and requires exactly one byte-identical MADT. Running
against the unchanged baseline without `--expect-unfixed` must fail.
`--expect-unfixed` explicitly records the reproduced defect for the baseline;
it never reports the correction as passed.

For each new firmware candidate, repeat the installed-MADT and Windows CPU-set
readback below. A successful build and byte comparison alone do not prove
hardware behavior or a scheduling or power improvement.

## Physical Windows readback: 2026-11-06

The corrected firmware booted J813 through Aura → Mu → NVMe Windows on the
existing CDC port. The installed 884-byte MADT exactly matches the independently
calculated expected table (SHA-256
`ca68bd6f5242d387dc3d0408a67885493fdea2aa57e6797f60a3c4fe0fdcdf34`).
Compared with the prior physical table, only the ten GICC class bytes and the
installed ACPI checksum differ.

Windows `GetSystemCpuSetInformation` now reports class 0 for the six E-cores
(group 0, processors 1–6) and class 1 for the four P-cores (0, 7–9). Each processor
was joined by its affinity-pinned architectural MPIDR to a unique MADT UID;
the association was not inferred from class values or list position. The
installed CPU driver remained Running with Code 0, and frequency records
bracketing the identity read were identical (E2/P10). This boot issued no
frequency SET calls.

The [physical receipt](validation/j813-cpu-efficiency-windows-20261106.json)
contains both raw MADTs, all CPU-set and identity records, exact changed offsets,
firmware/source/input hashes and the unchanged frequency brackets. The initial
read-only query outlasted the host's initial response timeout; its complete
CRC-valid response was recovered from the original capture without resending the
request. Subsequent requests passed with an ACK-aware bounded response wait.

To verify the published artifact independently from this checkout:

```sh
python3 - <<'PYVERIFY'
import hashlib, json, struct
from pathlib import Path
p = Path('Docs/validation/j813-cpu-efficiency-windows-20261106.json')
r = json.loads(p.read_text())
a = bytes.fromhex(r['previous_installed_madt_hex'])
b = bytes.fromhex(r['installed_madt_hex'])
assert len(a) == len(b) == 884 and sum(a) % 256 == sum(b) % 256 == 0
assert hashlib.sha256(b).hexdigest() == r['installed_madt_sha256']
assert [i for i, (x, y) in enumerate(zip(a, b)) if x != y] == r['changed_installed_offsets']
entries = {}
offset = 44
while offset < len(b):
    kind, size = b[offset:offset + 2]
    assert size >= 2 and offset + size <= len(b)
    if kind == 11:
        uid = struct.unpack_from('<I', b, offset + 8)[0]
        assert size == 80 and uid not in entries
        entries[uid] = (struct.unpack_from('<Q', b, offset + 68)[0], b[offset + 76])
    offset += size
assert len(entries) == len(r['processors']) == 10
for cpu in r['processors']:
    expected = int(cpu['uid'] >= 6)
    assert entries[cpu['uid']] == (cpu['mpidr'], expected)
    assert cpu['windows_cpu_set']['EfficiencyClass'] == expected
assert r['frequency_brackets']['before'] == r['frequency_brackets']['after']
print('Captured firmware-to-Windows efficiency readback verified')
PYVERIFY
```

This proves corrected firmware and Windows CPU-set classes on this physical
boot. It does not measure scheduling improvement, energy savings or absolute
clock frequency, and it does not implement automatic Windows frequency policy.

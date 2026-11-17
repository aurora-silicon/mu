#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Execute AcpiPlatform's device-table loop with and without the CPUF marker.

The loop text and the CPUF/MTP admission functions are cut verbatim from
AcpiPlatform.c; firmware services (FV2 ReadSection, gBS->FreePool, the ACPI
table protocol, the ADT) are the modeled boundary in acpi-table-loop-host.c.
The same harness is also built from a baseline commit, which must fail.

Ways the loop can fail, written down before this check (each case below names
the failure modes it can expose):
  F1  a withheld table is freed twice (boot 145: DxeCore Pool.c assert);
  F2  ReadSection is handed a stale, already-freed CurrentTable and writes the
      next table into freed pool (use after free);
  F3  a withheld instance is re-read forever because Instance is not advanced;
  F4  tables after a withheld table are not installed (loop stops or skips);
  F5  a withheld table is installed anyway (marker absent/invalid, CPUF out);
  F6  a valid marker withholds the CPUF table (over-rejection);
  F7  a table read but neither installed nor freed (leak at loop exit);
  F8  an installed table differs from its storage bytes beyond the checksum,
      or carries a bad checksum;
  F9  a table installed twice or out of storage order;
  F10 a malformed marker, wrong value, wrong chip-id or board-id admits CPUF;
  F11 the neighbouring withhold branch (N1/MTP/G17/display) regresses;
  F12 the DSDT USB-policy replacement leaks or double-frees, or its failure
      path frees the table twice before aborting.
Harness-side failure modes, checked here too: the loop text cannot be found
(markers drifted), the extracted loop no longer calls the admission function,
the ReadSection model is not PI-conformant (it follows FwVolReadSection: a
non-NULL *Buffer is filled in place, truncation returns a warning).
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = pathlib.Path('Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AcpiPlatform.c')
HOST = pathlib.Path('Tests/e2e/acpi-table-loop-host.c')


def table(sig, oem_table_id, body):
    length = 36 + len(body)
    hdr = struct.pack('<4sIBB6s8sI4sI', sig.encode(), length, 2, 0, b'APPLE ',
                      oem_table_id, 1, b'APPL', 1)
    raw = bytearray(hdr + body)
    raw[9] = (-sum(raw)) & 0xff
    return bytes(raw)


# Signatures, OEM table IDs and storage order of the J813 device-table file in
# the 45087b59 image (CPUF third, eight tables around it); bodies are filler.
TABLES = [
    table('DBG2', b'APPLEEFI', b'\x00' * 8),
    table('FACP', b'APPLEEFI', b'\x01' * 8),
    table('SSDT', b'J813CPUF', b'\x5b\x82\x05CPUF'),
    table('SSDT', b'AURA818D', b'\x5b\x82\x05DISP'),
    table('DSDT', b'J813\0\0\0\0', b'\x10\x05\\_SB_'),
    table('SSDT', b'AURA817G', b'\x5b\x82\x05GPU0'),
    table('SSDT', b'J813MTP\0', b'\x5b\x82\x05MTP0'),
    table('SSDT', b'AURA813N', b'\x5b\x82\x05N1B0'),
    table('SSDT', b'SMCGPIO ', b'\x5b\x82\x05SMCG'),
]
CPUF_LAST = TABLES[:2] + TABLES[3:] + TABLES[2:3]
ALL = list(range(len(TABLES)))
NO_CPUF = [i for i in ALL if i != 2]
CHIP, BOARD = 0x8142, 0x24

# name -> (fixture, harness flags, indices expected installed, failure modes)
CASES = {
    'marker-present': (TABLES, ['marker=1'], ALL, 'F6 F7 F8 F9'),
    'marker-absent': (TABLES, ['marker=absent'], NO_CPUF, 'F1 F2 F3 F4 F5 F7 F9'),
    'marker-zero': (TABLES, ['marker=0'], NO_CPUF, 'F1 F2 F10'),
    'marker-short': (TABLES, ['marker=short'], NO_CPUF, 'F1 F2 F10'),
    'wrong-chip': (TABLES, ['marker=1', f'chip={CHIP + 1:#x}'], NO_CPUF, 'F1 F10'),
    'wrong-board': (TABLES, ['marker=1', f'board={BOARD + 1:#x}'], NO_CPUF, 'F1 F10'),
    'cpuf-last-absent': (CPUF_LAST, ['marker=absent'], ALL[:-1], 'F1 F3'),
    'neighbour-withhold': (TABLES, ['marker=1', 'reject=AURA813N'], [i for i in ALL if i != 7], 'F11'),
    'mtp-withheld': (TABLES, ['marker=absent', 'mtp=1'], [i for i in NO_CPUF if i != 6], 'F1 F11'),
    'usb-replace': (TABLES, ['marker=absent', 'usb=replace'], NO_CPUF, 'F12'),
    'usb-fail': (TABLES, ['marker=absent', 'usb=fail'], [0, 1, 3], 'F12'),
}


def extract(source):
    """Cut the device-table loop and the helpers it calls, verbatim."""
    anchor = source.index('Status = LocateFvInstanceWithDeviceTables (&FwVol);')
    start = source.index('  while (Status == EFI_SUCCESS) {', anchor)
    end = source.index('\n  Instance     = 0;\n', start)
    loop = source[start:end] + '\n'
    assert 'PcdDeviceAcpiTableStorageFile' in loop and 'AppleCpuFreqTableAdmitted' in loop
    helpers = []
    for name in ('AppleCpuFreqTableAdmitted', 'AppleMtpTableAdmitted', 'AppleAcpiPlatformChecksum'):
        head = source.index(f'\n{name} (\n') + 1
        # Back up over the return-type/storage lines (STATIC, BOOLEAN, VOID...).
        while True:
            prev = source.rindex('\n', 0, head - 1) + 1
            if not re.fullmatch(r'[A-Z_]+', source[prev:head - 1]):
                break
            head = prev
        tail = source.index('\n}\n', head) + 3
        helpers.append(source[head:tail])
    return loop, '\n'.join(helpers)


def build(out, label, source):
    inc = out / label / 'include'
    inc.mkdir(parents=True)
    loop, helpers = extract(source)
    (inc / 'loop.inc').write_text(loop)
    (inc / 'helpers.inc').write_text(helpers)
    binary = out / label / 'loop'
    command = ['clang', '-std=c17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(inc), str(ROOT / HOST), '-o', str(binary)]
    result = subprocess.run(command, text=True, capture_output=True)
    (out / label / 'compile.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, f'compile failed: {out / label / "compile.log"}'
    return binary, command, hashlib.sha256(loop.encode()).hexdigest()


def run_case(binary, out, label, name):
    fixture, flags, expected, modes = CASES[name]
    blob = out / label / f'{name}.tables'
    blob.write_bytes(struct.pack('<I', len(fixture)) +
                     b''.join(struct.pack('<I', len(t)) + t for t in fixture))
    run = subprocess.run([str(binary), str(blob)] + flags, text=True, capture_output=True, timeout=20)
    (out / label / f'{name}.log').write_text(run.stdout + run.stderr)
    record = {'case': name, 'flags': flags, 'modes': modes, 'rc': run.returncode}
    try:
        got = json.loads(run.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        record.update(verdict='FAIL', reason='no harness record (crash)')
        return record
    record.update(got)
    problems = []
    if run.returncode != 0:
        problems.append(f'harness rc {run.returncode}')
    if got['violations']:
        problems.append('violations: ' + '; '.join(got['violations']))
    installed = [bytes.fromhex(h) for h in got['installed']]
    want = [fixture[i] for i in expected]
    if name == 'usb-replace':
        # The modeled policy rewrite flips the last byte of the J813 DSDT.
        want = [t[:-1] + bytes([t[-1] ^ 0xff]) if t[:4] == b'DSDT' else t for t in want]
    for t in installed:
        if sum(t) & 0xff:
            problems.append('installed table has a bad checksum')
    strip = lambda t: t[:9] + t[10:]
    if [strip(t) for t in installed] != [strip(t) for t in want]:
        problems.append(f'installed {[t[:4] + t[16:24] for t in installed]} '
                        f'!= expected {[t[:4] + t[16:24] for t in want]}')
    if got['live_allocations']:
        problems.append(f'{got["live_allocations"]} allocation(s) live at loop exit')
    if name == 'usb-fail':
        if got['loop_status'] != 'EFI_ABORTED':
            problems.append(f'loop status {got["loop_status"]} != EFI_ABORTED')
    elif got['loop_status'] != 'EFI_NOT_FOUND':
        problems.append(f'loop ended with {got["loop_status"]}, not storage exhaustion')
    reads = 5 if name == 'usb-fail' else len(fixture) + 1  # usb-fail aborts at the DSDT
    if got['read_calls'] != reads:
        problems.append(f'{got["read_calls"]} ReadSection calls, expected {reads}')
    record['verdict'] = 'FAIL' if problems else 'PASS'
    if problems:
        record['problems'] = problems
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--baseline', default='45087b59e756eac1ed3341bf95d5fac4fe70220d',
                        help='commit whose loop must fail (the unfixed boot-145 code)')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    current = (ROOT / SOURCE).read_text()
    baseline = subprocess.run(['git', '-C', str(ROOT), 'show', f'{args.baseline}:{SOURCE}'],
                              check=True, capture_output=True, text=True).stdout
    summary = {'schema': 'mu.acpi-table-loop.replay.v1', 'hardware_qualified': False,
               'modeled_boundary': 'FV2 ReadSection (FwVolReadSection semantics), gBS->FreePool, '
                                   'InstallAcpiTable, ADT /chosen, N1/G17/display admission, '
                                   'USB policy rewrite',
               'verbatim_from_source': ['device-table loop of AcpiPlatformEntryPoint',
                                        'AppleCpuFreqTableAdmitted', 'AppleMtpTableAdmitted',
                                        'AppleAcpiPlatformChecksum'],
               'sources': {}, 'runs': {}}
    for name in (SOURCE, HOST, pathlib.Path('Tests/e2e/acpi-table-loop.py')):
        dest = out / 'sources' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, dest)
        summary['sources'][str(name)] = hashlib.sha256(dest.read_bytes()).hexdigest()
    for label, text in (('current', current), ('baseline', baseline)):
        binary, command, loop_sha = build(out, label, text)
        results = [run_case(binary, out, label, name) for name in CASES]
        summary['runs'][label] = {'loop_sha256': loop_sha, 'command': command, 'cases': results}
    cur = summary['runs']['current']['cases']
    base = summary['runs']['baseline']['cases']
    base_failed = sorted(r['case'] for r in base if r['verdict'] == 'FAIL')
    summary['current_pass'] = all(r['verdict'] == 'PASS' for r in cur)
    summary['baseline_failed_cases'] = base_failed
    # The unfixed loop must be caught exactly where the marker withholds CPUF.
    must_fail = {'marker-absent', 'marker-zero', 'marker-short', 'wrong-chip', 'wrong-board',
                 'cpuf-last-absent', 'mtp-withheld', 'usb-replace', 'usb-fail'}
    summary['baseline_caught'] = must_fail <= set(base_failed)
    summary['result'] = 'PASS' if summary['current_pass'] and summary['baseline_caught'] else 'FAIL'
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'result': summary['result'],
                      'current': {r['case']: r['verdict'] for r in cur},
                      'baseline_failed': base_failed}, indent=1))
    raise SystemExit(0 if summary['result'] == 'PASS' else 1)


if __name__ == '__main__':
    main()

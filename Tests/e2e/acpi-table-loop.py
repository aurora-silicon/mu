#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Execute AcpiPlatform's device-table loop with its real admission helpers.

The loop text and the CPUF/MTP admission functions are cut verbatim from
AcpiPlatform.c, and AuraG17GpuDevice.c, AuraJ813DisplayDevice.c and (where the
revision has it) AuraJ813UnifiedDisplay.c are compiled verbatim beside them.
Firmware services (FV2 ReadSection, FreePool, the ACPI table protocol, the
ADT, the GCD map) are the modeled boundary in acpi-table-loop-host.c.  The
device SSDTs are compiled with iasl from Platform/J813BoardPkg/AcpiTables
and stored in the J813 order; DBG2, FACP and the DSDT are fillers.

Runs: the working tree (every case must pass); the quiet image's source
(7ad68eb), whose key-off installs must equal the working tree's byte for byte;
45087b59, which must fail with the boot-145 use-after-free and double free;
and three working-tree variants that withhold the unified table through a
separate branch (free only; free and clear; free and advance), which must all
fail on a default boot.

Ways the loop can fail, written down before this check (each case names the
failure modes it can expose; U1-U10 are in Docs/j813-unified-display-marker.md):
  F1  a withheld table is freed twice (boot 145: DxeCore Pool.c assert);
  F2  ReadSection is handed a stale, already-freed CurrentTable and writes the
      next table into freed pool (use after free);
  F3  a withheld instance is re-read forever because Instance is not advanced;
  F4  tables after a withheld table are not installed (loop stops or skips);
  F5  a withheld table is installed anyway (marker absent/invalid, CPUF out);
  F6  a valid marker withholds the CPUF table (over-rejection);
  F7  a table read but neither installed nor freed (leak at loop exit);
  F8  an installed table differs from its storage bytes beyond the checksum
      and the G17 range patch, or carries a bad checksum;
  F9  a table installed twice or out of storage order;
  F10 a malformed marker, wrong value, wrong chip-id or board-id admits CPUF;
  F11 the neighbouring withhold branch (N1/MTP/G17/display) regresses;
  F12 the DSDT USB-policy replacement leaks or double-frees, or its failure
      path frees the table twice before aborting.
Harness-side failure modes, checked here too: the loop text cannot be found
(markers drifted), the extracted loop no longer calls the admission function,
the ReadSection model is not PI-conformant (it follows FwVolReadSection: a
non-NULL *Buffer is filled in place; a truncating fill, which the loop would
overrun, is reported and stops the run), an ASL fixture fails to compile, the
G17 placeholders are not found exactly once.
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
DXE = SOURCE.parent
UNITS = {'g17': 'AuraG17GpuDevice.c', 'disp': 'AuraJ813DisplayDevice.c',
         'unified': 'AuraJ813UnifiedDisplay.c'}
ASL = pathlib.Path('Platform/J813BoardPkg/AcpiTables')
HOST = pathlib.Path('Tests/e2e/acpi-table-loop-host.c')
HOST_H = pathlib.Path('Tests/e2e/acpi-table-loop-host.h')
QUIET = '7ad68ebf88463054ebc8faca9b16a51f9ecd4e1f'
BASELINE = '45087b59e756eac1ed3341bf95d5fac4fe70220d'

# Boot 147's AURA8170 ranges 3-8 (Windows' allocated resources, GPU.asl order).
REGIONS = [(0x103fff30000, 0x4000), (0x103fffb8000, 0x4000), (0x103fff38000, 0x80000),
           (0x103fff34000, 0x4000), (0x1000249c000, 0x134000), (0x100025d0000, 0x130000)]
OVER_16MIB = (0x10400000000, 0x2000000)      # outside RAM and the other regions
IN_RAM = (0x10100000000, 0x80000)


def table(sig, oem_table_id, body):
    length = 36 + len(body)
    hdr = struct.pack('<4sIBB6s8sI4sI', sig.encode(), length, 2, 0, b'APPLE ',
                      oem_table_id, 1, b'APPL', 1)
    raw = bytearray(hdr + body)
    raw[9] = (-sum(raw)) & 0xff
    return bytes(raw)


def compile_asl(out):
    aml = {}
    # GPUP (the unified table with the GPU pool, AURA817P) joins the storage
    # file when the tree has it; every case here builds with its gate off.
    names = ['CPUF', 'DISP', 'GPU', 'GPUF', 'MTP', 'N1', 'SMCG']
    if (ROOT / ASL / 'GPUP.asl').exists():
        names.insert(names.index('GPUF') + 1, 'GPUP')
    for name in names:
        src = ROOT / ASL / f'{name}.asl'
        result = subprocess.run(['iasl', '-p', str(out / name), str(src)], text=True, capture_output=True)
        (out / f'{name}.iasl.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, f'iasl failed for {src}'
        aml[name] = (out / f'{name}.aml').read_bytes()
    return aml


def fixture(aml):
    """Signatures, OEM table IDs and order of the J813 device-table file."""
    rows = [('DBG2', table('DBG2', b'APPLEEFI', b'\x00' * 8)),
            ('FACP', table('FACP', b'APPLEEFI', b'\x01' * 8)),
            ('CPUF', aml['CPUF']), ('DISP', aml['DISP']),
            ('DSDT', table('DSDT', b'J813\0\0\0\0', b'\x10\x05\\_SB_')),
            ('GPU', aml['GPU']), ('GPUF', aml['GPUF'])] + \
           ([('GPUP', aml['GPUP'])] if 'GPUP' in aml else []) + \
           [('MTP', aml['MTP']), ('N1', aml['N1']), ('SMCG', aml['SMCG'])]
    for label, blob in rows:
        assert len(blob) == struct.unpack_from('<I', blob, 4)[0], label
    return rows


def patch(blob, regions):
    """Independent model of AuraG17GpuDevice.c PatchDescriptor for all six."""
    raw = bytearray(blob)
    for n, (base, size) in enumerate(regions):
        needle = struct.pack('<Q', 0xA170000000000000 | (n << 36))
        hits = [m.start() for m in re.finditer(re.escape(needle), bytes(raw)) if m.start() >= 36 + 14]
        assert len(hits) == 1, f'placeholder {n} found {len(hits)} times'
        d = hits[0] - 14
        assert raw[d:d + 4] == b'\x8a\x2b\x00\x00', f'descriptor {n} malformed'
        struct.pack_into('<Q', raw, d + 14, base)
        struct.pack_into('<Q', raw, d + 22, base + size - 1)
        struct.pack_into('<Q', raw, d + 38, size)
    return bytes(raw)


LEGACY = ['DBG2', 'FACP', 'CPUF', 'DISP', 'DSDT', 'GPU', 'MTP', 'N1', 'SMCG']
UNIFIED = ['DBG2', 'FACP', 'CPUF', 'DSDT', 'GPUF', 'MTP', 'N1', 'SMCG']
NO_GPU = ['DBG2', 'FACP', 'CPUF', 'DSDT', 'MTP', 'N1', 'SMCG']


def minus(labels, *drop):
    return [x for x in labels if x not in drop]


# name -> (cpuf_last, harness flags, expected labels, log expectation, failure modes)
#   log: 'off' default-boot logging, 'on' unified selected, 'warn' malformed/ignored marker
CASES = {
    'marker-present': (False, ['marker=1'], LEGACY, 'off', 'F6 F7 F8 F9 U1'),
    'marker-absent': (False, ['marker=absent'], minus(LEGACY, 'CPUF'), 'off', 'F1 F2 F3 F4 F5 F7 F9 U1 U2'),
    'marker-zero': (False, ['marker=0'], minus(LEGACY, 'CPUF'), 'off', 'F1 F2 F10'),
    'marker-short': (False, ['marker=short'], minus(LEGACY, 'CPUF'), 'off', 'F1 F2 F10'),
    'wrong-chip': (False, ['marker=1', 'chip=0x8143'], minus(LEGACY, 'CPUF', 'DISP', 'GPU'), 'off', 'F1 F10 U5'),
    'wrong-board': (False, ['marker=1', 'board=0x25'], minus(LEGACY, 'CPUF', 'DISP', 'GPU'), 'off', 'F1 F10 U5'),
    'cpuf-last-absent': (True, ['marker=absent'], minus(LEGACY, 'CPUF'), 'off', 'F1 F3'),
    'neighbour-withhold': (False, ['marker=1', 'reject=AURA813N'], minus(LEGACY, 'N1'), 'off', 'F11'),
    'mtp-withheld': (False, ['marker=absent', 'mtp=1'], minus(LEGACY, 'CPUF', 'MTP'), 'off', 'F1 F11'),
    'usb-replace': (False, ['marker=absent', 'usb=replace'], minus(LEGACY, 'CPUF'), 'off', 'F12'),
    'usb-fail': (False, ['marker=absent', 'usb=fail'], ['DBG2', 'FACP', 'DISP'], 'off', 'F12'),
    'key-on/cpufreq-on': (False, ['marker=1', 'unified=1'], UNIFIED, 'on', 'U2 U8 U9'),
    'key-on/cpufreq-off': (False, ['marker=absent', 'unified=1'], minus(UNIFIED, 'CPUF'), 'on', 'F1 U2 U8'),
    'malformed-short/cpufreq-on': (False, ['marker=1', 'unified=short'], LEGACY, 'warn', 'U3'),
    'malformed-long/cpufreq-on': (False, ['marker=1', 'unified=long'], LEGACY, 'warn', 'U3'),
    'malformed-zero/cpufreq-on': (False, ['marker=1', 'unified=0'], LEGACY, 'warn', 'U3'),
    'malformed-two/cpufreq-off': (False, ['marker=absent', 'unified=2'], minus(LEGACY, 'CPUF'), 'warn', 'U2 U3'),
    'malformed-short/cpufreq-off': (False, ['marker=absent', 'unified=short'], minus(LEGACY, 'CPUF'), 'warn', 'U2 U3'),
    'gate-off/key-on': (False, ['marker=1', 'unified=1', 'gate=0'], LEGACY, 'warn', 'U5'),
    'key-on/not-j813': (False, ['marker=1', 'unified=1', 'chip=0x8143'], minus(NO_GPU, 'CPUF'), 'on', 'U5 U8'),
    'key-on/no-dcp': (False, ['marker=1', 'unified=1', 'dcp=0'], NO_GPU, 'on', 'U5 U8'),
    'key-on/over-16MiB': (False, ['marker=1', 'unified=1', 'region=0:%#x:%#x' % OVER_16MIB], NO_GPU, 'on', 'U7 U8'),
    'key-off/over-16MiB': (False, ['marker=1', 'region=0:%#x:%#x' % OVER_16MIB], LEGACY, 'off', 'U1 U7'),
    'key-on/ram-overlap': (False, ['marker=1', 'unified=1', 'region=2:%#x:%#x' % IN_RAM], NO_GPU, 'on', 'U6 U8'),
}
KEY_OFF = ['marker-present', 'marker-absent']   # boot 147 shape, cpufreq on and off


def regions_for(flags):
    regions = list(REGIONS)
    for flag in flags:
        if flag.startswith('region='):
            index, base, size = flag[7:].split(':')
            regions[int(index)] = (int(base, 0), int(size, 0))
    return regions


# Package headers the helpers include with angle brackets that hold the helpers'
# own contract rather than firmware services: inlined like quoted headers.
PACKAGE_HEADERS = {'IndustryStandard/AuraJ813GpuPool.h':
                   pathlib.Path('Silicon/Apple/AppleSiliconPkg/Include/IndustryStandard/AuraJ813GpuPool.h')}


def revision_sources(rev):
    """{file name: text} for AcpiPlatform.c and the DXE helper sources."""
    names = ['AcpiPlatform.c'] + list(UNITS.values()) + \
            [n.replace('.c', '.h') for n in UNITS.values()] + list(PACKAGE_HEADERS)
    out = {}
    for name in names:
        path = PACKAGE_HEADERS.get(name, DXE / name)
        if rev is None:
            if (ROOT / path).exists():
                out[name] = (ROOT / path).read_text()
        else:
            got = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:{path}'],
                                 capture_output=True, text=True)
            if got.returncode == 0:
                out[name] = got.stdout
    return out


def flatten(text, sources):
    """Drop angle-bracket includes except PACKAGE_HEADERS; inline those and the
    module's own quoted headers."""
    lines = []
    for line in text.splitlines():
        m = re.match(r'\s*#include\s+([<"])([^>"]+)[>"]', line)
        if m and m.group(1) == '<' and m.group(2) not in sources:
            continue
        if m:
            lines.append(flatten(sources[m.group(2)], sources))
            continue
        lines.append(line)
    return '\n'.join(lines) + '\n'


def extract(source):
    """Cut the device-table loop and the helpers it calls, verbatim."""
    anchor = source.index('Status = LocateFvInstanceWithDeviceTables (&FwVol);')
    start = source.index('  while (Status == EFI_SUCCESS) {', anchor)
    end = source.index('\n  Instance     = 0;\n', start)
    loop = source[start:end] + '\n'
    assert 'PcdDeviceAcpiTableStorageFile' in loop and 'AppleCpuFreqTableAdmitted' in loop
    assert 'AppleG17GpuTableAdmitted' in loop and 'AppleJ813DisplayTableAdmitted' in loop
    helpers = []
    for name in ('AppleCpuFreqTableAdmitted', 'AppleMtpTableAdmitted', 'AppleAcpiPlatformChecksum'):
        head = source.index(f'\n{name} (\n') + 1
        while True:
            prev = source.rindex('\n', 0, head - 1) + 1
            if not re.fullmatch(r'[A-Z_]+', source[prev:head - 1]):
                break
            head = prev
        tail = source.index('\n}\n', head) + 3
        helpers.append(source[head:tail])
    return loop, '\n'.join(helpers)


SEPARATE = ('      if ((((EFI_ACPI_DESCRIPTION_HEADER *)CurrentTable)->Signature == SIGNATURE_32 (\'S\', \'S\', \'D\', \'T\')) &&\n'
            '          (CompareMem (&((EFI_ACPI_DESCRIPTION_HEADER *)CurrentTable)->OemTableId, "AURA817F", 8) == 0) &&\n'
            '          !AppleJ813UnifiedDisplaySelected ())\n'
            '      {\n'
            '        gBS->FreePool (CurrentTable);\n'
            '%s'
            '        continue;\n'
            '      }\n')
MUTATIONS = {'separate-free-only': '', 'separate-free-clear': '        CurrentTable = NULL;\n',
             'separate-free-advance': '        Instance++;\n'}


def mutate(loop, extra):
    anchor = '      if (!AppleN1TableAdmitted ('
    assert loop.count(anchor) == 1, 'mutation anchor'
    return loop.replace(anchor, SEPARATE % extra + anchor)


def build(out, label, sources, loop_override=None):
    inc = out / label / 'include'
    inc.mkdir(parents=True)
    loop, helpers = extract(sources['AcpiPlatform.c'])
    if loop_override:
        loop = loop_override(loop)
    (inc / 'loop.inc').write_text(loop)
    (inc / 'helpers.inc').write_text(helpers)
    shutil.copy2(ROOT / HOST_H, inc / 'acpi-table-loop-host.h')
    units = []
    for unit, name in UNITS.items():
        if name in sources:
            path = inc / f'{unit}.c'
            path.write_text('#include "acpi-table-loop-host.h"\n' + flatten(sources[name], sources))
            units.append(str(path))
    binary = out / label / 'loop'
    command = ['clang', '-std=c17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(inc), str(ROOT / HOST)] + units + ['-o', str(binary)]
    result = subprocess.run(command, text=True, capture_output=True)
    (out / label / 'compile.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, f'compile failed: {out / label / "compile.log"}'
    return binary, command, hashlib.sha256(loop.encode()).hexdigest(), sorted(units)


def run_case(binary, out, label, name, rows, unified_aware):
    cpuf_last, flags, expected, log_kind, modes = CASES[name]
    if cpuf_last:
        rows = [r for r in rows if r[0] != 'CPUF'] + [r for r in rows if r[0] == 'CPUF']
    blob = out / label / f'{name.replace("/", "_")}.tables'
    blob.write_bytes(struct.pack('<I', len(rows)) +
                     b''.join(struct.pack('<I', len(t)) + t for _, t in rows))
    run = subprocess.run([str(binary), str(blob)] + flags, text=True, capture_output=True, timeout=20)
    (out / label / f'{name.replace("/", "_")}.log').write_text(run.stdout + run.stderr)
    record = {'case': name, 'flags': flags, 'modes': modes, 'rc': run.returncode}
    try:
        got = json.loads(run.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        record.update(verdict='FAIL', problems=['no harness record (crash)'])
        return record, None
    problems = []
    if run.returncode != 0:
        problems.append(f'harness rc {run.returncode}')
    if got['violations']:
        problems.append('violations: ' + '; '.join(got['violations']))
    installed = [bytes.fromhex(h) for h in got['installed']]
    by_label = dict(rows)
    regions = regions_for(flags)
    want = []
    for lab in expected:
        t = by_label[lab]
        if lab in ('GPU', 'GPUF'):
            t = patch(t, regions)
        if lab == 'DSDT' and 'usb=replace' in flags:
            t = t[:-1] + bytes([t[-1] ^ 0xff])
        want.append(t)
    for t in installed:
        if sum(t) & 0xff:
            problems.append('installed table has a bad checksum')
    strip = lambda t: t[:9] + t[10:]
    if [strip(t) for t in installed] != [strip(t) for t in want]:
        name_of = lambda t: (t[:4] + t[16:24]).decode('latin1')
        problems.append(f'installed {[name_of(t) for t in installed]} != expected {[name_of(t) for t in want]}'
                        if [name_of(t) for t in installed] != [name_of(t) for t in want]
                        else 'installed bytes differ from expected (patch or content)')
    if got['live_allocations']:
        problems.append(f'{got["live_allocations"]} allocation(s) live at loop exit')
    if name == 'usb-fail':
        if got['loop_status'] != 'EFI_ABORTED':
            problems.append(f'loop status {got["loop_status"]} != EFI_ABORTED')
    elif got['loop_status'] != 'EFI_NOT_FOUND':
        problems.append(f'loop ended with {got["loop_status"]}, not storage exhaustion')
    reads = 5 if name == 'usb-fail' else len(rows) + 1
    if got['read_calls'] != reads:
        problems.append(f'{got["read_calls"]} ReadSection calls, expected {reads}')
    if unified_aware:
        log = got['log']
        unified_warn = [t for lvl, t in log if lvl & 0x2 and 'unified' in t]
        if got['marker_reads'] != 1:
            problems.append(f'unified marker read {got["marker_reads"]} times, not once (U4)')
        if any('Missing DeviceTree prop: aura,windows-unified-display-v1' in t for _, t in log):
            problems.append('absent unified marker printed a Missing DeviceTree warning (U10)')
        if log_kind == 'off' and (unified_warn or not any('no unified marker' in t for lvl, t in log if lvl & 0x1)):
            problems.append('default boot did not log exactly the no-marker milestone (U10)')
        if log_kind == 'warn' and len(unified_warn) != 1:
            problems.append(f'malformed/ignored marker logged {len(unified_warn)} warnings, not 1 (U3)')
        if log_kind == 'on' and not any('unified marker set' in t for lvl, t in log if lvl & 0x1):
            problems.append('unified selection not logged as a milestone')
    record['verdict'] = 'FAIL' if problems else 'PASS'
    record.update({k: got[k] for k in ('outcome', 'loop_status', 'read_calls', 'live_allocations',
                                       'marker_reads', 'violations')})
    record['installed'] = [(t[:4] + t[16:24]).decode('latin1') for t in installed]
    record['log'] = got['log']
    if problems:
        record['problems'] = problems
    return record, got['installed']


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    aml_dir = out / 'aml'
    aml_dir.mkdir()
    rows = fixture(compile_asl(aml_dir))
    quiet_rows = [r for r in rows if r[0] not in ('GPUF', 'GPUP')]
    summary = {'schema': 'mu.acpi-table-loop.replay.v2', 'hardware_qualified': False,
               'modeled_boundary': 'FV2 ReadSection (FwVolReadSection semantics), gBS->FreePool and '
                                   'FreePool, InstallAcpiTable, ADT (/chosen, /arm-io/sgx|dcp|disp0), '
                                   'GCD map, N1 admission, USB policy rewrite',
               'verbatim_from_source': ['device-table loop of AcpiPlatformEntryPoint',
                                        'AppleCpuFreqTableAdmitted', 'AppleMtpTableAdmitted',
                                        'AppleAcpiPlatformChecksum'] + list(UNITS.values()),
               'fixture': {lab: hashlib.sha256(t).hexdigest() for lab, t in rows},
               'sources': {}, 'runs': {}}
    for name in [SOURCE] + [DXE / n for n in UNITS.values()] + [HOST, HOST_H,
                 pathlib.Path('Tests/e2e/acpi-table-loop.py')]:
        if (ROOT / name).exists():
            dest = out / 'sources' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, dest)
            summary['sources'][str(name)] = hashlib.sha256(dest.read_bytes()).hexdigest()

    current = revision_sources(None)
    plans = [('current', current, None, rows, list(CASES), True),
             ('quiet-7ad68eb', revision_sources(QUIET), None, quiet_rows, KEY_OFF, False),
             ('baseline-45087b59', revision_sources(BASELINE), None, quiet_rows, list(CASES)[:11], False)]
    for mname, extra in MUTATIONS.items():
        plans.append((f'mutation-{mname}', current, (lambda loop, e=extra: mutate(loop, e)), rows,
                      list(CASES), True))
    installs = {}
    for label, sources, override, table_rows, case_names, aware in plans:
        binary, command, loop_sha, units = build(out, label, sources, override)
        results = []
        for name in case_names:
            record, hexes = run_case(binary, out, label, name, table_rows, aware)
            results.append(record)
            installs[(label, name)] = hexes
        summary['runs'][label] = {'loop_sha256': loop_sha, 'units': [pathlib.Path(u).name for u in units],
                                  'command': command, 'cases': results}

    cur = summary['runs']['current']['cases']
    summary['current_pass'] = all(r['verdict'] == 'PASS' for r in cur)
    # Default boot: the merged loop over the merged storage installs exactly
    # what the quiet image's loop installs over the quiet storage.
    summary['key_off_equals_quiet'] = {
        name: installs[('current', name)] is not None and
        installs[('current', name)] == installs[('quiet-7ad68eb', name)] for name in KEY_OFF}
    summary['quiet_runs_pass'] = all(r['verdict'] == 'PASS' for r in summary['runs']['quiet-7ad68eb']['cases'])
    base = summary['runs']['baseline-45087b59']['cases']
    boot145 = {'marker-absent', 'marker-zero', 'marker-short', 'wrong-chip', 'wrong-board',
               'cpuf-last-absent', 'mtp-withheld', 'usb-replace', 'usb-fail'}
    summary['baseline_caught'] = all(
        any('(F1)' in v for v in r.get('violations', [])) and any('(F2)' in v for v in r.get('violations', []))
        for r in base if r['case'] in boot145)
    summary['mutations_caught'] = {}
    for mname in MUTATIONS:
        rs = {r['case']: r for r in summary['runs'][f'mutation-{mname}']['cases']}
        summary['mutations_caught'][mname] = {
            'default_boot_failed': all(rs[n]['verdict'] == 'FAIL' for n in KEY_OFF),
            'violations': sorted({f for n in KEY_OFF for v in rs[n].get('violations', [])
                                  for f in re.findall(r'\((F\d+)\)', v)}),
            'failed_cases': sum(r['verdict'] == 'FAIL' for r in rs.values())}
    summary['result'] = 'PASS' if (summary['current_pass'] and all(summary['key_off_equals_quiet'].values())
                                   and summary['quiet_runs_pass'] and summary['baseline_caught']
                                   and all(m['default_boot_failed'] for m in summary['mutations_caught'].values())
                                   ) else 'FAIL'
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'result': summary['result'],
                      'current': {r['case']: r['verdict'] for r in cur},
                      'key_off_equals_quiet': summary['key_off_equals_quiet'],
                      'baseline_caught': summary['baseline_caught'],
                      'mutations_caught': summary['mutations_caught']}, indent=1))
    raise SystemExit(0 if summary['result'] == 'PASS' else 1)


if __name__ == '__main__':
    main()

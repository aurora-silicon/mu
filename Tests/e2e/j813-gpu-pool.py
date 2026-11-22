#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""J813 G17 local-memory pool, end to end through Mu's own code; no hardware.

One run of a case goes through the firmware the way a boot does:
  1. PEI: the production T8142 MemoryInitPeiLib.c (verbatim, any revision)
     runs over J813 guest RAM at its real physical addresses and leaves its
     HOB list (j813-gpu-pool-peim-host.c).
  2. DXE core: the GCD is built from those resource HOBs by the rule of
     MU_BASECORE 11a8cedb Gcd.c:2687-2688 (EFI_RESOURCE_MEMORY_RESERVED ->
     EfiGcdMemoryTypeReserved), and CoreGetMemoryMap reports a Reserved GCD
     range as EfiReservedMemoryType (Page.c:2049-2070).  This is the modeled
     boundary; it is not Mu code.
  3. DXE: AcpiPlatform's device-table loop with AuraG17GpuDevice.c and the
     other admission helpers (verbatim, built by acpi-table-loop.py) installs
     the J813 device tables against that GCD and the GUID HOBs from step 1.

The checks are the host test of SHARED-SURFACES.md ruling 3 and the failure
list Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/
FAILURE-MODES-J813-GPU-POOL.md (G1-G10).  When a pool is expected:
  * it is EfiReservedMemoryType: exactly one EFI_RESOURCE_MEMORY_RESERVED HOB
    equals it, so every byte is GCD Reserved;
  * it is absent from the memory Windows is given: no system-memory HOB, GCD
    SystemMemory range or allocation HOB overlaps it, and the RAM accounting
    balances against 094f1e5 (system memory shrinks by exactly the pool);
  * it is inside Aura's stage-2 identity map, computed from the Aura source's
    formula over the fixture's boot arguments and TZ carveouts;
  * it is published as exactly one more QWordMemory resource of AURA8171
    (GPUF's 16 descriptors, byte for byte, then the pool) and of no other
    device: its base and last byte appear once across every installed table.
For the key-off cases, and for key-on with the pool gate off, every resource,
allocation and GUID HOB and every installed table equals 094f1e5's, byte for
byte.  The failure cases (no room, not J813, malformed carveouts, a device
that is withheld, forged or misplaced HOBs) must publish no pool.

Output: --output DIR receives every input, log and per-case record, and
summary.json with the source hashes and the verdict.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import shutil
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
E2E = pathlib.Path('Tests/e2e')
PEI_SOURCE = pathlib.Path('Silicon/Apple/T8142FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c')
PEI_HOST = E2E / 'j813-gpu-pool-peim-host.c'
BASELINE = '094f1e5'

_spec = importlib.util.spec_from_file_location('acpi_table_loop', ROOT / E2E / 'acpi-table-loop.py')
LOOP = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(LOOP)

# The pool contract (FAILURE-MODES-J813-GPU-POOL.md, IndustryStandard/AuraJ813GpuPool.h).
POOL_SIZE = 1 << 30
POOL_ALIGN = 32 << 20
POOL_GUID = struct.pack('<IHH8B', 0x7e10d7ec, 0x9434, 0x42ac, 0x91, 0x3f, 0xe8, 0x79, 0xc9, 0x2f, 0x51, 0xfa)
POOL_SIGNATURE = b'AGLP'
POOL_RECORD = struct.Struct('<4sHHQQQQQ')       # signature, version, size, base, size, guest base, top, 0
PLACEHOLDER = 0xA170006000000000                # G17_PLACEHOLDER (6)
PHYS_LIMIT = 1 << 42

# Aura's guest-RAM and stage-2 inputs for the fixture (boot 147's guest RAM).
# Guest RAM is [heap_top, ba.phys_base + ba.mem_size) (hv/__init__.py:2082,
# 2094, 2171-2172); the identity map is RAM-LOW and RAM-HIGH (2143-2144) less
# the TZ carveouts (2040-2050).  iBoot's own phys_base and the carveouts are
# synthetic: their J813 values are runtime facts, not source.
AURA = {'ram_base': 0x10000000000, 'mem_size_actual': 0x400000000,
        'iboot_phys_base': 0x10000C00000, 'iboot_top': 0x103DC600000, 'heap_top': 0x1001AB80000}
CARVEOUTS_ABOVE = [(0x103E0000000, 0x4000000), (0x103F4000000, 0x2000000)]
CARVEOUTS_TOP = [(0x103C0000000, 0x1000000), (0x103F4000000, 0x2000000)]
SMALL_RAM = {'heap_top': 0x10200000000, 'iboot_top': 0x10260000000}

GCD_NONEXISTENT, GCD_RESERVED, GCD_SYSTEM = 0, 1, 2
HOB_SYSTEM, HOB_RESERVED = 1, 2

LEGACY = ['DBG2', 'FACP', 'CPUF', 'DISP', 'DSDT', 'GPU', 'MTP', 'N1', 'SMCG']
UNIFIED = ['DBG2', 'FACP', 'CPUF', 'DSDT', 'GPUF', 'MTP', 'N1', 'SMCG']
POOLED = ['DBG2', 'FACP', 'CPUF', 'DSDT', 'GPUP', 'MTP', 'N1', 'SMCG']
NO_GPU = ['DBG2', 'FACP', 'CPUF', 'DSDT', 'MTP', 'N1', 'SMCG']


def minus(labels, *drop):
    return [x for x in labels if x not in drop]


def case(name, marker, expect, modes, *, pool=True, unified_pcd=True, cpufreq=True, chip=None,
         carveouts='above', ram=None, dcp=True, region=None, hob=None, baseline=False):
    """expect: 'published' | 'reserved' (held, not published) | 'none'."""
    return {'name': name, 'marker': marker, 'pool': pool, 'unified_pcd': unified_pcd,
            'cpufreq': cpufreq, 'chip': chip, 'carveouts': carveouts, 'ram': ram, 'dcp': dcp,
            'region': region, 'hob': hob, 'baseline': baseline, 'expect': expect, 'modes': modes}


CASES = [
    # name                         marker    expect        modes
    case('pool',                   '1', 'published', 'G1 G2 G3 G5 G6 G8'),
    case('pool/cpufreq-off',       '1', 'published', 'G6 G8', cpufreq=False),
    case('pool/carveout-in-top',   '1', 'published', 'G2 G3', carveouts='top'),
    case('pool/carveouts-absent',  '1', 'published', 'G3', carveouts='absent'),
    case('key-off/marker-absent',  'absent', 'none', 'G7 G9', baseline=True),
    case('key-off/cpufreq-off',    'absent', 'none', 'G7', cpufreq=False, baseline=True),
    case('key-off/marker-zero',    '0', 'none', 'G7 G9', baseline=True),
    case('key-off/marker-two',     '2', 'none', 'G7 G9', baseline=True),
    case('key-off/marker-short',   'short', 'none', 'G7 G9', baseline=True),
    case('key-off/marker-long',    'long', 'none', 'G7 G9', baseline=True),
    case('key-off/unified-pcd-off', '1', 'none', 'G7 G9', unified_pcd=False, baseline=True),
    case('key-on/pool-gate-off',   '1', 'none', 'G7', pool=False, baseline=True),
    case('pool/no-room',           '1', 'none', 'G4', ram=SMALL_RAM),
    case('pool/not-j813',          '1', 'none', 'G8 G9', chip=0x8143),
    case('pool/carveout-malformed', '1', 'none', 'G3', carveouts='malformed'),
    case('pool/no-dcp',            '1', 'reserved', 'G8 G10', dcp=False),
    case('pool/region-in-pool',    '1', 'reserved', 'G2 G8', region=2),
    case('hob/absent',             '1', 'reserved', 'G4 G8', hob='absent'),
    case('hob/misaligned',         '1', 'reserved', 'G5', hob='misaligned'),
    case('hob/short',              '1', 'reserved', 'G5', hob='short'),
    case('hob/bad-signature',      '1', 'reserved', 'G9', hob='bad-signature'),
    case('hob/duplicate',          '1', 'reserved', 'G8', hob='duplicate'),
    case('hob/over-system-ram',    '1', 'reserved', 'G1 G9', hob='over-system-ram'),
    case('hob/not-wholly-reserved', '1', 'reserved', 'G1', hob='not-wholly-reserved'),
]


def expected_labels(c):
    """The device tables a case must install (acpi-table-loop.py's rules)."""
    unified = c['marker'] == '1' and c['unified_pcd']
    labels = POOLED if unified and c['pool'] else (UNIFIED if unified else LEGACY)
    if not c['cpufreq']:
        labels = minus(labels, 'CPUF')
    if c['chip'] is not None:
        labels = minus(labels, 'CPUF', 'DISP', 'GPU', 'GPUF', 'GPUP')
    if unified and c['pool'] and c['expect'] != 'published':
        labels = minus(labels, 'GPUP')
    return labels


# ---- AML -------------------------------------------------------------------

def pkg_length(raw, i):
    lead = raw[i]
    count = lead >> 6
    if count == 0:
        return lead & 0x3F, i + 1
    value = lead & 0x0F
    for k in range(count):
        value |= raw[i + 1 + k] << (4 + 8 * k)
    return value, i + 1 + count


def crs_buffers(table):
    """The bytes of every Name (_CRS, Buffer) in a table."""
    out, i = [], 36
    while (i := table.find(b'\x08_CRS\x11', i)) >= 0:
        _, k = pkg_length(table, i + 6)
        op = table[k]
        if op == 0x0A:
            size, k = table[k + 1], k + 2
        elif op == 0x0B:
            size, k = struct.unpack_from('<H', table, k + 1)[0], k + 3
        elif op == 0x0C:
            size, k = struct.unpack_from('<I', table, k + 1)[0], k + 5
        else:
            raise ValueError('unexpected _CRS buffer size encoding')
        out.append(table[k:k + size])
        i = k + size
    return out


def qword_memory(buf):
    """[(raw descriptor, general flags, type flags, min, max, length)]."""
    out, i = [], 0
    while i < len(buf):
        tag = buf[i]
        if tag & 0x80:
            length = struct.unpack_from('<H', buf, i + 1)[0]
            raw = buf[i:i + 3 + length]
            if tag == 0x8A and raw[3] == 0:
                out.append((raw, raw[4], raw[5]) + struct.unpack_from('<QQ', raw, 14) +
                           struct.unpack_from('<Q', raw, 38))
            i += 3 + length
        else:
            if (tag >> 3) == 0x0F:
                break
            i += 1 + (tag & 7)
    return out


def hid(table):
    i = table.find(b'_HID\x0d')
    return None if i < 0 else table[i + 5:table.index(b'\x00', i + 5)].decode('latin1')


def label_of(table):
    return (table[:4] + table[16:24]).decode('latin1')


# ---- stages ----------------------------------------------------------------

def build_pei(out, label, source_text):
    work = out / 'pei' / label
    include = work / 'include'
    for name in ('PiPei.h', 'Base.h', 'Library/ArmMmuLib.h', 'Library/ArmPlatformLib.h',
                 'Library/DebugLib.h', 'Library/HobLib.h', 'Library/MemoryAllocationLib.h',
                 'Library/PcdLib.h', 'Library/IoLib.h', 'Library/PrintLib.h'):
        dest = include / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('#include "memory-peim-host.h"\n')
    source = work / 'MemoryInitPeiLib.c'
    source.write_text(source_text)
    binary = work / 'peim'
    command = ['clang', '-std=c17', '-D_GNU_SOURCE', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
               '-Wno-unused-function', '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
               '-I', str(include), '-I', str(ROOT / E2E),
               '-I', str(ROOT / 'Silicon/Apple/AppleSiliconPkg/Include'),
               '-I', str(ROOT / 'Silicon/Apple/T8142FamilyPkg/Include'),
               '-include', str(ROOT / E2E / 'memory-peim-host.h'), str(source), str(ROOT / PEI_HOST),
               '-o', str(binary)]
    result = subprocess.run(command, text=True, capture_output=True)
    (work / 'compile.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'PEI compile failed: {work / "compile.log"}')
    return binary, command


def aura_layout(c):
    layout = dict(AURA)
    if c['ram']:
        layout.update(c['ram'])
    return layout


def carveouts_of(c):
    return {'above': CARVEOUTS_ABOVE, 'top': CARVEOUTS_TOP}.get(c['carveouts'], [])


def run_pei(binary, work, c):
    layout = aura_layout(c)
    flags = [f'marker={c["marker"]}', f'pool={int(c["pool"])}', f'unified-pcd={int(c["unified_pcd"])}',
             f'ram-base={layout["heap_top"]:#x}', f'ram-top={layout["iboot_top"]:#x}',
             f'carveouts={c["carveouts"]}']
    if c['chip'] is not None:
        flags.append(f'chip={c["chip"]:#x}')
    regions = carveouts_of(c) or CARVEOUTS_ABOVE
    flags += [f'carveout2={regions[0][0]:#x}:{regions[0][1]:#x}',
              f'carveout4={regions[1][0]:#x}:{regions[1][1]:#x}']
    run = subprocess.run([str(binary)] + flags, text=True, capture_output=True, timeout=60)
    (work / 'pei.log').write_text(' '.join(flags) + '\n' + run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError(f'PEI host failed ({run.returncode}): {work / "pei.log"}')
    got = json.loads(run.stdout.strip().splitlines()[-1])
    got['flags'] = flags
    return got


def gcd_from(pei):
    """The DXE core's GCD over [0, 2^42) from the resource HOBs (Gcd.c:2687-2688)."""
    spans = sorted((start, length, GCD_SYSTEM if kind == HOB_SYSTEM else GCD_RESERVED)
                   for kind, _, start, length in pei['resources'])
    out, cursor = [], 0
    for start, length, kind in spans:
        if start < cursor:
            raise ValueError(f'resource HOBs overlap at {start:#x}')
        if start > cursor:
            out.append((cursor, start - cursor, GCD_NONEXISTENT))
        out.append((start, length, kind))
        cursor = start + length
    if cursor < PHYS_LIMIT:
        out.append((cursor, PHYS_LIMIT - cursor, GCD_NONEXISTENT))
    return out


def pool_hobs(pei):
    return [bytes.fromhex(data) for guid, data in pei['guid_hobs'] if bytes.fromhex(guid) == POOL_GUID]


def forge(c, pei, gcd, baseline_gcd):
    """The DXE inputs for a hob/* case, from the published case's PEI output."""
    hobs = [(bytes.fromhex(g), bytes.fromhex(d)) for g, d in pei['guid_hobs']]
    mine = [d for g, d in hobs if g == POOL_GUID]
    others = [(g, d) for g, d in hobs if g != POOL_GUID]
    if not mine:
        return hobs, gcd
    record = list(POOL_RECORD.unpack(mine[0]))
    kind = c['hob']
    if kind == 'absent':
        return others, gcd
    if kind == 'misaligned':
        record[3] += 0x10000
    elif kind == 'short':
        record[4] = POOL_SIZE // 2
    elif kind == 'bad-signature':
        record[0] = b'AGBP'
    elif kind == 'duplicate':
        return hobs + [(POOL_GUID, mine[0])], gcd
    elif kind == 'over-system-ram':
        return hobs, baseline_gcd
    elif kind == 'not-wholly-reserved':
        base = record[3]
        split = []
        for start, length, t in gcd:
            if t == GCD_RESERVED and start == base and length == POOL_SIZE:
                split += [(start, POOL_SIZE // 2, GCD_RESERVED), (start + POOL_SIZE // 2, POOL_SIZE // 2, GCD_NONEXISTENT)]
            else:
                split.append((start, length, t))
        return hobs, split
    return others + [(POOL_GUID, POOL_RECORD.pack(*record))], gcd


def run_dxe(binary, work, rows, c, gcd, hobs, region=None):
    flags = ['marker=1' if c['cpufreq'] else 'marker=absent', f'unified={c["marker"]}',
             f'gate={int(c["unified_pcd"])}', f'pool={int(c["pool"])}']
    if c['chip'] is not None:
        flags.append(f'chip={c["chip"]:#x}')
    if not c['dcp']:
        flags.append('dcp=0')
    if region:
        flags.append(region)
    (work / 'gcd.bin').write_bytes(b''.join(struct.pack('<QQI', *g) for g in gcd))
    (work / 'hobs.bin').write_bytes(b''.join(g + struct.pack('<I', len(d)) + d for g, d in hobs))
    flags += [f'gcd={work / "gcd.bin"}', f'hobs={work / "hobs.bin"}']
    tables = work / 'tables.bin'
    tables.write_bytes(struct.pack('<I', len(rows)) + b''.join(struct.pack('<I', len(t)) + t for _, t in rows))
    run = subprocess.run([str(binary), str(tables)] + flags, text=True, capture_output=True, timeout=60)
    (work / 'dxe.log').write_text(' '.join(flags) + '\n' + run.stdout + run.stderr)
    try:
        got = json.loads(run.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return None, flags
    got['rc'] = run.returncode
    return got, flags


# ---- checks ----------------------------------------------------------------

def overlaps(a, b, c, d):
    return a < d and c < b


def identity_spans(layout, carveouts):
    """Aura's stage-2 identity map over DRAM (see AURA above)."""
    spans = [(layout['ram_base'], layout['iboot_phys_base']),
             (layout['heap_top'], layout['ram_base'] + layout['mem_size_actual'])]
    for base, size in carveouts:
        cut = []
        for s, e in spans:
            if overlaps(s, e, base, base + size):
                cut += [x for x in ((s, base), (base + size, e)) if x[0] < x[1]]
            else:
                cut.append((s, e))
        spans = cut
    return spans


def check_memory(c, pei, base_pei, gcd, problems):
    """G1-G5 on the HOB list; returns the pool (base, size) or None."""
    layout = aura_layout(c)
    ram = (layout['heap_top'], layout['iboot_top'])
    records = pool_hobs(pei)
    reserved_gib = [r for r in pei['resources'] if r[0] == HOB_RESERVED and r[3] == POOL_SIZE]
    if pei['status'] != 0 or not pei['mmu']:
        problems.append(f'MemoryPeim returned {pei["status"]} (mmu {pei["mmu"]})')
    if sum(r[3] for r in pei['resources']) != ram[1] - ram[0]:
        problems.append('resource HOBs do not account for guest RAM exactly')
    if c['expect'] == 'none':
        if records or reserved_gib:
            problems.append('a pool was reserved or handed off where none may exist')
        if base_pei is not None and any(pei[k] != base_pei[k] for k in ('resources', 'allocations', 'guid_hobs')):
            problems.append('HOB list differs from 094f1e5 (G7)')
        return None
    if len(records) != 1:
        problems.append(f'{len(records)} pool HOBs, not 1')
        return None
    sig, version, length, base, size, guest_base, guest_top, zero = POOL_RECORD.unpack(records[0])
    if (sig, version, length, zero) != (POOL_SIGNATURE, 1, POOL_RECORD.size, 0):
        problems.append('pool HOB header is not AGLP v1')
    if size != POOL_SIZE or base % POOL_ALIGN or (base + size) % POOL_ALIGN:
        problems.append(f'pool {base:#x}+{size:#x} is not 1 GiB on {POOL_ALIGN:#x} boundaries (G5)')
    if (guest_base, guest_top) != ram:
        problems.append('pool HOB records a guest-RAM window MemoryPeim was not given')
    top = base + size
    if [r for r in pei['resources'] if r[0] == HOB_RESERVED and (r[2], r[3]) == (base, size)] != \
            [r for r in pei['resources'] if overlaps(r[2], r[2] + r[3], base, top)]:
        problems.append('the pool is not exactly one EFI_RESOURCE_MEMORY_RESERVED HOB (G1)')
    if any(t != GCD_RESERVED for s, l, t in gcd if overlaps(s, s + l, base, top)):
        problems.append('a GCD range over the pool is not Reserved: not EfiReservedMemoryType (G1)')
    if any(overlaps(a, a + s, base, top) for a, s, _ in pei['allocations']):
        problems.append('an allocation HOB overlaps the pool (G1)')
    uefi, fd = pei['uefi'], pei['fd']
    if overlaps(uefi[0], uefi[0] + uefi[1], base, top) or overlaps(fd[0], fd[0] + fd[1], base, top):
        problems.append('the pool overlaps the initial UEFI region or the FD (G1/G2)')
    for cb, cs in carveouts_of(c):
        if overlaps(cb, cb + cs, base, top):
            problems.append(f'the pool overlaps TZ carveout {cb:#x} (G2/G3)')
    if not (ram[0] <= base and top <= ram[1]):
        problems.append('the pool is outside the guest RAM Aura hands Mu (G3)')
    if not any(s <= base and top <= e for s, e in identity_spans(layout, carveouts_of(c))):
        problems.append("the pool is outside Aura's stage-2 identity map (G3)")
    if base_pei is not None:
        system = lambda p: sum(r[3] for r in p['resources'] if r[0] == HOB_SYSTEM)
        if system(base_pei) - system(pei) != POOL_SIZE:
            problems.append('system memory did not shrink by exactly the pool against 094f1e5 (G1)')
        if [r for r in base_pei['resources'] if r[0] == HOB_RESERVED] != \
                [r for r in pei['resources'] if r[0] == HOB_RESERVED and (r[2], r[3]) != (base, size)]:
            problems.append("094f1e5's reserved HOBs changed (G2/G7)")
        if base_pei['allocations'] != pei['allocations'] or \
                base_pei['guid_hobs'] != [h for h in pei['guid_hobs'] if bytes.fromhex(h[0]) != POOL_GUID]:
            problems.append("094f1e5's allocation or other GUID HOBs changed (G7)")
    return base, size


def check_tables(c, got, rows, pool, base_got, problems):
    if got is None:
        problems.append('no DXE harness record (crash)')
        return []
    if got['rc'] or got['violations']:
        problems.append(f'DXE harness rc {got["rc"]}, violations {got["violations"]}')
    if got['live_allocations'] or got['loop_status'] != 'EFI_NOT_FOUND':
        problems.append(f'loop ended {got["loop_status"]} with {got["live_allocations"]} live allocations')
    installed = [bytes.fromhex(h) for h in got['installed']]
    labels = [label_of(t) for t in installed]
    want = expected_labels(c)
    by_label = {label_of(t): lab for lab, t in rows}
    names = [by_label.get(x, x) for x in labels]
    if names != want:
        problems.append(f'installed {names} != expected {want}')
    for t in installed:
        if sum(t) & 0xFF:
            problems.append(f'{label_of(t)} has a bad checksum')
        for buf in crs_buffers(t):
            for d in qword_memory(buf):
                if d[3] >> 52 == PLACEHOLDER >> 52:
                    problems.append(f'{label_of(t)} publishes an unpatched placeholder (G8)')
    if [hid(t) for t in installed].count('AURA8171') > 1:
        problems.append('AURA8171 is published twice (G8)')
    if base_got is not None and got['installed'] != base_got['installed']:
        problems.append('installed tables differ from 094f1e5 byte for byte (G7)')
    if pool is None:
        return names
    base, size = pool
    hits = {lab: sum(t.count(struct.pack('<Q', v)) for v in (base, base + size - 1))
            for lab, t in zip(names, installed)}
    if c['expect'] != 'published':
        if any(hits.values()):
            problems.append(f'the pool is published although its device is withheld (G8): {hits}')
        return names
    if sum(hits.values()) != 2 or hits.get('GPUP') != 2:
        problems.append(f'the pool range appears outside one AURA8171 descriptor (G6/G8): {hits}')
    for t, lab in zip(installed, names):
        for buf in crs_buffers(t):
            for d in qword_memory(buf):
                if overlaps(d[3], d[4] + 1, base, base + size) and lab != 'GPUP':
                    problems.append(f'{lab} has a descriptor over the pool (G6)')
    gpup = [t for t, lab in zip(installed, names) if lab == 'GPUP']
    if len(gpup) != 1 or hid(gpup[0]) != 'AURA8171':
        problems.append('the pool table is not one AURA8171 device')
        return names
    mine = [d for buf in crs_buffers(gpup[0]) for d in qword_memory(buf)]
    gpuf = LOOP.patch(dict(rows)['GPUF'], LOOP.REGIONS)
    theirs = [d for buf in crs_buffers(gpuf) for d in qword_memory(buf)]
    if len(theirs) != 16 or len(mine) != 17 or [d[0] for d in mine[:16]] != [d[0] for d in theirs]:
        problems.append(f'AURA8171 is not GPUF\'s 16 descriptors plus one ({len(mine)} found) (G6)')
        return names
    raw, general, flags, lo, hi, length = mine[16]
    if (lo, hi, length) != (base, base + size - 1, size):
        problems.append('the pool descriptor disagrees with the reserved range (G5)')
    if general != theirs[0][1] or flags != 0x05:
        problems.append(f'pool descriptor flags {general:#x}/{flags:#x}: want consumer fixed, RW write-combining')
    return names


def git_text(rev, path):
    got = subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:{path}'], capture_output=True, text=True)
    if got.returncode:
        raise RuntimeError(f'cannot read {path} at {rev}')
    return got.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', BASELINE], capture_output=True,
                              text=True, check=True).stdout.strip()

    aml_dir = out / 'aml'
    aml_dir.mkdir()
    rows = LOOP.fixture(LOOP.compile_asl(aml_dir))
    base_rows = [r for r in rows if r[0] != 'GPUP']

    pei_cur, pei_cur_cmd = build_pei(out, 'current', (ROOT / PEI_SOURCE).read_text())
    pei_base, _ = build_pei(out, 'baseline', git_text(baseline, PEI_SOURCE))
    dxe_cur, dxe_cmd, loop_sha, _ = LOOP.build(out / 'dxe', 'current', LOOP.revision_sources(None))
    dxe_base, _, _, _ = LOOP.build(out / 'dxe', 'baseline', LOOP.revision_sources(baseline))

    summary = {'schema': 'mu.j813-gpu-pool.e2e.v1', 'hardware_qualified': False, 'baseline': baseline,
               'modeled_boundary': 'PEI HOB store, ADT, PCDs, MMU install and page allocator; the DXE '
                                   "core's HOB-to-GCD and GCD-to-memory-map rules (Gcd.c:2687-2688, "
                                   'Page.c:2049-2070); FV2 ReadSection, pool, InstallAcpiTable '
                                   '(acpi-table-loop-host.c); Aura stage 2 (formula from source)',
               'verbatim_from_source': [str(PEI_SOURCE), 'AcpiPlatform.c device-table loop',
                                        'AuraG17GpuDevice.c', 'AuraJ813DisplayDevice.c',
                                        'AuraJ813UnifiedDisplay.c'],
               'aura_fixture': {k: hex(v) for k, v in AURA.items()},
               'pei_command': pei_cur_cmd, 'dxe_command': dxe_cmd, 'loop_sha256': loop_sha,
               'fixture': {lab: hashlib.sha256(t).hexdigest() for lab, t in rows},
               'sources': {}, 'cases': []}
    for name in [PEI_SOURCE, PEI_HOST, E2E / 'memory-peim-host.h', E2E / 'acpi-table-loop-host.c',
                 E2E / 'acpi-table-loop-host.h', E2E / 'acpi-table-loop.py', E2E / 'j813-gpu-pool.py',
                 pathlib.Path('Silicon/Apple/AppleSiliconPkg/Include/IndustryStandard/AuraJ813GpuPool.h'),
                 pathlib.Path('Platform/J813BoardPkg/AcpiTables/GPUP.asl')] + \
            [LOOP.DXE / n for n in LOOP.UNITS.values()]:
        if (ROOT / name).exists():
            dest = out / 'sources' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, dest)
            summary['sources'][str(name)] = hashlib.sha256(dest.read_bytes()).hexdigest()

    published = None
    for c in CASES:
        work = out / 'cases' / c['name'].replace('/', '_')
        work.mkdir(parents=True)
        problems = []
        pei = run_pei(pei_cur, work, c)
        (work / 'baseline-pei').mkdir()
        base_pei = run_pei(pei_base, work / 'baseline-pei', c)
        try:
            gcd = gcd_from(pei)
        except ValueError as error:
            problems.append(f'{error} (G1)')
            gcd = []
        # 'reserved' cases reserve exactly as 'published' ones do; only DXE differs.
        pool = check_memory(dict(c, expect='published' if c['expect'] == 'reserved' else c['expect']),
                            pei, base_pei, gcd, problems)
        hobs = [(bytes.fromhex(g), bytes.fromhex(d)) for g, d in pei['guid_hobs']]
        dxe_gcd, region = gcd, None
        if c['hob']:
            hobs, dxe_gcd = forge(c, pei, gcd, gcd_from(base_pei))
        if c['region'] is not None:
            if published is None:
                problems.append('no published pool to move a region into')
            else:
                region = f'region={c["region"]}:{published[0] + 0x100000:#x}:0x80000'
        got, flags = run_dxe(dxe_cur, work, rows, c, dxe_gcd, hobs, region)
        base_got = None
        if c['baseline']:
            bwork = work / 'baseline'
            bwork.mkdir()
            base_got, _ = run_dxe(dxe_base, bwork, base_rows, c, gcd_from(base_pei),
                                  [(bytes.fromhex(g), bytes.fromhex(d)) for g, d in base_pei['guid_hobs']])
        names = check_tables(c, got, rows, pool, base_got, problems)
        if c['name'] == 'pool' and pool and not problems:
            published = pool
        record = {'case': c['name'], 'modes': c['modes'], 'expect': c['expect'],
                  'verdict': 'FAIL' if problems else 'PASS', 'installed': names,
                  'pei_flags': pei['flags'], 'dxe_flags': flags,
                  'pool': None if pool is None else {'base': hex(pool[0]), 'size': hex(pool[1])},
                  'pei_marker_reads': pei['marker_reads']}
        if got is not None:
            record['log'] = [t for lvl, t in got['log'] if 'G17' in t or 'pool' in t.lower()]
        if problems:
            record['problems'] = problems
        summary['cases'].append(record)

    summary['result'] = 'PASS' if all(r['verdict'] == 'PASS' for r in summary['cases']) else 'FAIL'
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'result': summary['result'],
                      'cases': {r['case']: r['verdict'] for r in summary['cases']},
                      'pool': next((r['pool'] for r in summary['cases'] if r['case'] == 'pool'), None)},
                     indent=1))
    raise SystemExit(0 if summary['result'] == 'PASS' else 1)


if __name__ == '__main__':
    main()

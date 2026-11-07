#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Verify the real ARM64-compiled J813 MADT and its packaged FD copy.

Run after Tools/build-j813-windows-native.sh; this never boots firmware.
See Docs/j813-cpu-efficiency.md for the failure model and baseline procedure.
"""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('Silicon/Apple/T8142FamilyPkg/AcpiTables/MADT_Static.aslc')
TOPOLOGY = Path('Silicon/Apple/T8142FamilyPkg/AcpiTables/T8142J813Topology.h')
BASELINE = '81fc2821d99aabd02e05f8f40b440135848fd220'
BASELINE_SOURCE_SHA = 'd551911ceb3bcd5c6e5f6039e59eb79c85434b3a176d4b175ad8e5ba2c088001'
BASELINE_TABLE_SHA = 'c25ce320cf5fc91999d04507c2c7c6aade87bc483e8154dcb4f5272b3751bf52'
LZMA_GUID = uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf').bytes_le
ORDER = [6, 0, 1, 2, 3, 4, 5, 7, 8, 9]
MAX_FD = 64 * 1024 * 1024
MAX_EXPANDED = 64 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(path, limit):
    if path.stat().st_size > limit:
        raise ValueError(f'oversized input: {path}')
    return path.read_bytes()


def decode(data):
    if len(data) != 884 or data[:4] != b'APIC' or struct.unpack_from('<I', data, 4)[0] != len(data):
        raise ValueError('expected the complete 884-byte J813 MADT')
    entries = []
    offset = 44
    while offset < len(data):
        kind, length = data[offset:offset+2]
        if length < 2 or offset+length > len(data):
            raise ValueError('invalid MADT subtable extent')
        if kind == 11:
            if length != 80:
                raise ValueError('unexpected GICC version/size')
            gic_id, uid, flags = struct.unpack_from('<III', data, offset+4)
            mpidr = struct.unpack_from('<Q', data, offset+68)[0]
            entries.append({'offset': offset, 'gic_id': gic_id, 'uid': uid,
                            'flags': flags, 'mpidr': mpidr,
                            'efficiency': data[offset+76]})
        offset += length
    if [e['uid'] for e in entries] != ORDER:
        raise ValueError('CPU count, UIDs or bootstrap ordering changed')
    for entry in entries:
        uid = entry['uid']
        expected_mpidr = uid if uid < 6 else 0x10100+uid-6
        if entry['gic_id'] != uid or entry['mpidr'] != expected_mpidr or entry['flags'] != 1:
            raise ValueError('CPU identity or enable flags changed')
    return entries


def packaged_tables(fd, table):
    """Find the exact compiled table in raw or bounded LZMA GUID sections.

    These are build artifacts, not an EFI memory map. AcpiTableDxe computes
    the installed ACPI checksum; the packaged source has a zero placeholder.
    """
    regions = [('raw-fd', fd)]
    cursor = 0
    while True:
        guid_offset = fd.find(LZMA_GUID, cursor)
        if guid_offset < 0:
            break
        cursor = guid_offset+16
        start = guid_offset-4
        if start < 0 or fd[start+3] != 2:
            continue
        size = int.from_bytes(fd[start:start+3], 'little')
        if size == 0xffffff:
            raise ValueError('extended GUID section unsupported by this bounded verifier')
        if size < 24 or start+size > len(fd):
            raise ValueError('invalid LZMA section extent')
        data_offset, _ = struct.unpack_from('<HH', fd, guid_offset+16)
        if not 24 <= data_offset < size:
            raise ValueError('invalid GUID data offset')
        decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
        expanded = decoder.decompress(fd[start+data_offset:start+size], max_length=MAX_EXPANDED+1)
        if len(expanded) > MAX_EXPANDED or not decoder.eof or decoder.unused_data:
            raise ValueError('incomplete, oversized or trailing LZMA data')
        regions.append((f'lzma-section-{start:#x}', expanded))
    hits = []
    for name, data in regions:
        cursor = 0
        while True:
            offset = data.find(b'APIC', cursor)
            if offset < 0:
                break
            cursor = offset+4
            if offset+8 > len(data):
                continue
            size = struct.unpack_from('<I', data, offset+4)[0]
            if size != 884 or offset+size > len(data):
                continue
            candidate = data[offset:offset+size]
            decode(candidate)
            if candidate != table:
                raise ValueError('packaged J813 MADT differs from compiler output')
            hits.append({'container': name, 'offset': offset, 'sha256': digest(candidate)})
    if len(hits) != 1:
        raise ValueError(f'expected exactly one packaged J813 MADT, found {len(hits)}')
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-table', type=Path, required=True)
    parser.add_argument('--table', type=Path, required=True)
    parser.add_argument('--fd', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--expect-unfixed', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    before = load(args.baseline_table, 4096)
    if digest(before) != BASELINE_TABLE_SHA:
        raise ValueError('baseline compiled table identity mismatch')
    after = load(args.table, 4096)
    fd = load(args.fd, MAX_FD)
    baseline_source = subprocess.check_output(['git', 'show', f'{BASELINE}:{SOURCE}'], cwd=ROOT)
    if digest(baseline_source) != BASELINE_SOURCE_SHA:
        raise ValueError('baseline source identity mismatch')
    old = decode(before)
    new = decode(after)
    if any(e['efficiency'] != (1 if e['uid'] < 6 else 0) for e in old):
        raise ValueError('baseline does not reproduce the inverted classes')
    expected = bytearray(before)
    offsets = []
    for entry in old:
        offset = entry['offset']+76
        offsets.append(offset)
        expected[offset] = 0 if entry['uid'] < 6 else 1
    if before[9] != 0 or after[9] != 0:
        raise ValueError('expected build-time checksum placeholders')
    hits = packaged_tables(fd, after)
    changed = [i for i, pair in enumerate(zip(before, after)) if pair[0] != pair[1]]
    correct = after == bytes(expected)
    if args.expect_unfixed:
        if correct or after != before:
            raise ValueError('negative control did not reproduce exact unchanged baseline')
    elif not correct or changed != offsets:
        raise ValueError('compiled delta is not exactly the ten efficiency bytes')
    # Preserve the actual packaged bytes and a separately labelled installed
    # checksum calculation. This does not claim firmware was booted.
    (args.out/'compiled-madt.bin').write_bytes(after)
    installed = bytearray(after)
    installed[9] = (-sum(installed)) & 0xff
    (args.out/'expected-installed-madt.bin').write_bytes(installed)
    receipt = {'schema': 'mu.j813.cpu-efficiency.v1',
               'result': 'unfixed-reproduced' if args.expect_unfixed else 'passed',
               'hardware_accessed': False, 'windows_scheduler_verified': False,
               'baseline_commit': BASELINE, 'baseline_source_sha256': BASELINE_SOURCE_SHA,
                'baseline_table_sha256': BASELINE_TABLE_SHA,
               'source_sha256': digest((ROOT/SOURCE).read_bytes()),
                'topology_sha256': digest((ROOT/TOPOLOGY).read_bytes()),
               'verifier_sha256': digest(Path(__file__).read_bytes()),
               'inputs': {str(path.resolve()): digest(data) for path, data in
                          [(args.baseline_table, before), (args.table, after), (args.fd, fd)]},
               'entries': new, 'changed_byte_offsets': changed,
               'expected_efficiency_offsets': offsets, 'packaged_matches': hits,
               'installed_checksum_is_offline_calculation': True,
               'expected_installed_sha256': digest(installed)}
    (args.out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'result': receipt['result'], 'changed_offsets': changed,
                      'packaged_matches': hits}))


if __name__ == '__main__':
    main()

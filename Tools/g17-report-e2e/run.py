#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run real Mu ADT/EarlySetup/PEI/DXE/ACPI publication with host EFI services.

The producer artifacts must come from Aura's real HardwareSession E2E. This is
offline evidence; no hardware or Windows execution is implied.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
REPORT = b'aura,windows-g17-report-v1'
PRESERVE = b'aura,windows-g17-preserve-v1'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def property_span(blob, name):
    key = name.ljust(32, b'\0')
    start = blob.index(key)
    size = struct.unpack_from('<I', blob, start + 32)[0] & 0xffffff
    return start + 36, size

def crc(payload):
    struct.pack_into('<I', payload, 28, 0)
    struct.pack_into('<I', payload, 28, zlib.crc32(payload))

def rewrite_property(blob, name, transform):
    """Edit actual ADT structure, retaining unrelated byte-exact properties."""
    def node(offset):
        count, children = struct.unpack_from('<II', blob, offset); offset += 8
        properties = []
        for _ in range(count):
            key = bytes(blob[offset:offset+32]); raw_size = struct.unpack_from('<I',blob,offset+32)[0]
            size = raw_size & 0xffffff; value = bytes(blob[offset+36:offset+36+size])
            offset += 36 + (size+3)//4*4
            values = transform(value) if key.rstrip(b'\0') == name else [value]
            for value in values:
                properties.append(key + struct.pack('<I',(raw_size & 0xff000000)|len(value)) + value + bytes(-len(value)%4))
        descendants = []
        for _ in range(children):
            child, offset = node(offset); descendants.append(child)
        return struct.pack('<II',len(properties),children)+b''.join(properties+descendants), offset
    result, consumed = node(0); assert consumed == len(blob)
    blob[:] = result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--red', action='store_true', help='Capture the missing-production-support red case only')
    a = parser.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    subprocess.run(['python3', str(ROOT/'Tools/g17-preservation-e2e/build.py'), '--output', str(out/'consumer')], check=True)
    exe = out/'consumer/mu-g17-consumer'
    results = []

    def run(name, fixture='report', mutation=None, flags=(), expected=0, count=1):
        source = a.fixtures/fixture; case = out/name; case.mkdir(exist_ok=True)
        adt = bytearray((source/'guest.adt').read_bytes())
        if mutation: mutation(adt)
        (case/'guest.adt').write_bytes(adt)
        source_args = source/'guest-bootargs.bin'; args = case/'guest-bootargs.bin'
        bootargs = bytearray(source_args.read_bytes())
        assert struct.unpack_from('<I',bootargs,104)[0] == len((source/'guest.adt').read_bytes())
        struct.pack_into('<I',bootargs,104,len(adt)); args.write_bytes(bootargs)
        preserve = (source/'record.bin').read_bytes() if (source/'record.bin').exists() else (a.fixtures/'report/record.bin').read_bytes()
        base, end, dram_base, dram_end = struct.unpack_from('<QQQQ', preserve, 24)
        command = [str(exe), str(case/'guest.adt'), hex(base), hex(end-base), hex(dram_end-dram_base),
                   str(case/'result.json'), '--fd-base', hex(base), '--bootargs', str(args),
                   '--bootargs-sha256', sha(args), '--acpi-output', str(case/'AG17.bin'), *flags]
        process = subprocess.run(command, capture_output=True, text=True)
        (case/'run.log').write_text(process.stdout+process.stderr)
        passed = process.returncode == expected
        details = {}
        if process.returncode == 0:
            report = json.loads((case/'result.json').read_text()); table = (case/'AG17.bin').read_bytes()
            passed &= report.get('report_table_count') == count and len(table) == 724*count
            if count:
                payload_at, payload_size = property_span(adt, REPORT)
                passed &= table[:4] == b'AG17' and sum(table) % 256 == 0
                passed &= table[36:] == adt[payload_at:payload_at+payload_size]
            details = {'table_sha256': sha(case/'AG17.bin'), 'result_sha256': sha(case/'result.json')}
        results.append({'name':name,'expected_exit':expected,'actual_exit':process.returncode,
                        'passed':bool(passed),'command':command,'adt_sha256':sha(case/'guest.adt'),
                        'bootargs_sha256':sha(args), **details})

    run('admitted-report')
    if not a.red:
        run('disabled', 'disabled', count=0)
        run('preserve-only', 'preserve-only', count=0)
        def mutate(offset, value=1, repair=True):
            def operation(blob):
                at, size = property_span(blob, REPORT); payload = bytearray(blob[at:at+size])
                payload[offset] ^= value
                if repair: crc(payload)
                blob[at:at+size] = payload
            return operation
        # Semantic mutations repair CRC so field checks, not CRC alone, reject.
        offsets = [0,4,6,8,12,16,20,24,336,338,340,342,344,352,360,364,
                   368,400,432,464,496,528,530,532,536,540,544,548,
                   552,576,600,624,626,628,630,632,636,640,648,652,
                   656,660,664,668,672,676,680,684]
        for offset in offsets: run(f'field-{offset}',mutation=mutate(offset),expected=23)
        run('bad-crc',mutation=mutate(28,repair=False),expected=23)
        run('different-preservation',mutation=mutate(32+24),expected=23)
        for name, transform in [('truncated',lambda p:[p[:-1]]),('oversized',lambda p:[p+b'\0']),
                                ('duplicate',lambda p:[p,p])]:
            run(f'report-{name}',mutation=lambda b,t=transform:rewrite_property(b,REPORT,t),expected=23)
        run('duplicate-preserve-property',mutation=lambda b:rewrite_property(b,PRESERVE,lambda p:[p,p]),expected=23)
        def missing_preservation(blob):
            at, _ = property_span(blob, PRESERVE); blob[at-36] = ord('x')
        run('report-without-preservation',mutation=missing_preservation,expected=23)
        run('existing-table', flags=('--existing-report-table',), expected=23)
        run('repeat-publication', flags=('--repeat-report-publication',), expected=24)
        run('install-failure', flags=('--report-install-failure',), expected=23)
        run('sdt-unavailable', flags=('--report-sdt-unavailable',), expected=23)
        run('sdt-enumeration-failure', flags=('--report-sdt-error',), expected=23)
        run('sdt-nonterminating', flags=('--report-sdt-endless',), expected=23)
        run('initial-map-failure', flags=('--map-fault',), expected=21)
        run('ready-map-failure', flags=('--ready-map-fault',), expected=22)
        run('ready-report-duplicate', flags=('--ready-report-duplicate',), expected=22)
        run('ready-report-tamper', flags=('--ready-report-tamper',), expected=22)
        run('ready-report-missing', flags=('--ready-report-missing',), expected=22)
    receipt = {'schema':'mu.g17.report-e2e.v1','hardware_touched':False,
               'source_sha256':{str(path.relative_to(ROOT)):sha(path) for path in [pathlib.Path(__file__).resolve(), ROOT/'Tools/g17-report-e2e/FAILURE-MODEL.md']},
               'consumer_build_receipt_sha256':sha(out/'consumer/build-receipt.json'), 'cases':results,
               'passed':all(x['passed'] for x in results)}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'passed':receipt['passed'],'cases':len(results),'receipt':str(out/'receipt.json')}))
    raise SystemExit(0 if receipt['passed'] else 1)

if __name__ == '__main__': main()

#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Execute the production dynamic ANS publisher against recorded ADT fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AcpiPlatform.c')
EXPECTED = [[0x481600000, 0x88000], [0x485cc0000, 0x60000], [0x485c50000, 0xc000]]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-unfixed', action='store_true')
    args = parser.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    include = out/'include'; include.mkdir()
    source = (ROOT/SOURCE).read_text()
    start = source.index('STATIC\nEFI_STATUS\nAcpiPlatformInstallAppleAnsTable (')
    end = source.index('\n}\n', start) + 3
    (include/'publication.inc').write_text(source[start:end])
    (include/'Uefi.h').write_text('#include "memory-peim-host.h"\n')
    files = [SOURCE, Path('Tests/e2e/ans-acpi-host.c'), Path('Tests/e2e/ans-acpi.py'),
             Path('Tests/e2e/memory-peim-host.h'), Path('Docs/windows-ans-secure-bar.md'),
             Path('Silicon/Apple/AppleSiliconPkg/Include/Drivers/AppleAnsHardware.h')]
    hashes = {}
    for name in files:
        dest = out/'sources'/name; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT/name, dest); hashes[str(name)] = hashlib.sha256(dest.read_bytes()).hexdigest()
    results = []; commands = []
    for platform in ([8142] if args.expect_unfixed else [8142, 8103]):
        binary = out/f'publisher-{platform}'
        command = ['clang', '-std=c17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-macro-redefined',
                   '-fsanitize=address,undefined', f'-DSILICON_PLATFORM={platform}',
                   '-I', str(include), '-I', str(ROOT/'Tests/e2e'),
                   '-I', str(ROOT/'Silicon/Apple/AppleSiliconPkg/Include'),
                   str(ROOT/'Tests/e2e/ans-acpi-host.c'), '-o', str(binary)]
        commands.append(command)
        build = subprocess.run(command, text=True, capture_output=True)
        (out/f'compile-{platform}.log').write_text(build.stdout+build.stderr)
        assert build.returncode == 0, f'compile failed: {out}/compile-{platform}.log'
        cases = ['normal'] if args.expect_unfixed else (
            ['normal', 'missing', 'zero', 'short', 'unaligned-base', 'unaligned-size', 'wrap',
             'overlap-asc', 'overlap-ans', 'overlap-sart', 'sart2', 'disabled']
            if platform == 8142 else ['normal', 'missing', 'sart2', 'disabled'])
        for case in cases:
            run = subprocess.run([str(binary), case], text=True, capture_output=True)
            (out/f'{platform}-{case}.log').write_text(run.stdout+run.stderr)
            assert run.returncode == 0
            result = json.loads(run.stdout); result.update(platform=platform, case=case)
            if args.expect_unfixed:
                assert result['published'] and result['memory'] == EXPECTED
            elif (platform == 8142 and case == 'normal') or (platform == 8103 and case != 'disabled'):
                assert result['published'] and result['status'] == 0
                assert result['memory'] == EXPECTED + ([[0x4c5cc0000, 0x10000]] if platform == 8142 else [])
                assert result['sta'] == 15 and result['cca'] == 1 and result['irq'] == 996
                assert result['ctrl_reads'] == (1 if platform == 8142 else 0)
            else:
                assert not result['published'] and result['status'] != 0
            results.append(result)
    # Compile the resource sequence emitted by the production T8142 function.
    result = results[0]
    resources = '\n'.join('QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, '
        f'0x{base:X}, 0x{base+size-1:X}, 0, 0x{size:X})' for base, size in result['memory'])
    asl = out/'published.asl'
    asl.write_text('DefinitionBlock ("", "SSDT", 2, "APPLE ", "ANSREPLY", 1) { Scope (\\_SB) { Device (ANS0) {'
                   f'Name (_HID, "{result["hid"]}") Name (_UID, 0) Name (_STA, 15) Name (_CCA, 1) '
                   'Name (_CRS, ResourceTemplate () {'+resources+' Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {996} }) } } }\n')
    compiled = subprocess.run(['iasl', '-p', str(out/'published'), str(asl)], text=True, capture_output=True)
    (out/'iasl.log').write_text(compiled.stdout+compiled.stderr); assert compiled.returncode == 0
    receipt = {'schema': 'mu.ans-acpi.replay.v1', 'hardware_qualified': False,
               'aml_backend': 'modeled; constructor-call trace compiled with iASL',
               'result': 'unfixed-reproduced' if args.expect_unfixed else 'passed',
               'sources': hashes, 'commands': commands, 'cases': results,
               'aml_sha256': hashlib.sha256((out/'published.aml').read_bytes()).hexdigest()}
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'result': receipt['result'], 'cases': len(results), 'aml_sha256': receipt['aml_sha256']}))

if __name__ == '__main__': main()

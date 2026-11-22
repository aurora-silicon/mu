#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the pinned inputs of the J804 windows FD's overridden modules.

The fork changes the J804 windows FD depends on live in
Silicon/Apple/AppleSiliconPkg/Override (PrePiLib with the progress callback,
CpuDxe, the build-time DXE core patch and the ArmExceptionLib that
AuraArmExceptionLib #includes), not in the submodules. This guard checks:
  * each of those override modules still has its reviewed content (sha256 over
    the sorted list of "<sha256>  <path>" lines of its tracked files);
  * AuraExceptionSupport.S, minus its five prefix lines, equals the override's
    ExceptionSupport.S;
  * MU_BASECORE, Common/MU and Silicon/ARM/TIANO are checked out at their
    gitlinks and are clean (Platform/Overrides.py fails the build otherwise;
    this just fails earlier);
  * the Common/MU sources AuraVarPolicyFoundationDxe #includes have their
    reviewed hashes.
Host-only; reads the checkout, writes nothing.

  python3 Tools/check-j804-windows-inputs.py           check
  python3 Tools/check-j804-windows-inputs.py --print   print the current digests
"""
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERRIDE = 'Silicon/Apple/AppleSiliconPkg/Override'
MODULES = {
    'ArmPkg/Library/ArmExceptionLib':
        '8c8d3d41ad8f69c30aef6d7d0403a8de3b6c300baf93bdc04983dd9b9fb7dd22',
    'ArmPkg/Drivers/CpuDxe':
        'd7725ebe2a65d0ba567e03301945794c91353f415dfa4b0c9302742c022efb8e',
    'EmbeddedPkg/Library/PrePiLib':
        '5d29ac4f7c1f8271a9324aec3fe4612fa8878ba43a1c3c2f00a8b8b625c8eab5',
    'Patches/MU_BASECORE':
        'e61d5ed8e10757457dae6883296b93259a95795e84891b2d4ea5eab2050bac13',
}
CLEAN_SUBMODULES = ('MU_BASECORE', 'Common/MU', 'Silicon/ARM/TIANO')
# Public Common/MU ba613eea328e801d9582059a8e0af414097a7fa8 (the gitlink).
MU_INPUTS = {
    'MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c':
        '1713498676f38c64108772f6564b9140ec7f83090cde189ec9bb1e9cc02dd1d4',
    'MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf':
        '119999e86df929b63e7b22d85891c39aec0a245c2c5d069a3e049cd3688a8462',
    'MsCorePkg/Include/Guid/MuVarPolicyFoundationDxe.h':
        'ceeca0aec0a364b295a11f85b105efb8f689c6a77ee0477be31ac78224881254',
}
ASSEMBLY = ROOT / 'Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/AuraExceptionSupport.S'


def git(*args, cwd=ROOT):
    return subprocess.run(['git', '-C', str(cwd), *args],
                          check=True, capture_output=True).stdout


def fail(message):
    raise SystemExit(f'check-j804-windows-inputs: {message}')


def module_digest(module):
    listing = git('ls-files', '-z', '--', f'{OVERRIDE}/{module}').split(b'\0')
    lines = []
    for raw in sorted(p for p in listing if p):
        path = raw.decode()
        digest = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
        lines.append(f'{digest}  {path[len(OVERRIDE) + 1:]}\n')
    if not lines:
        fail(f'no tracked files under {OVERRIDE}/{module}')
    return hashlib.sha256(''.join(lines).encode()).hexdigest()


def main(argv):
    if argv[1:] == ['--print']:
        for module in MODULES:
            print(f'{module_digest(module)}  {module}')
        return 0
    for module, expected in MODULES.items():
        if module_digest(module) != expected:
            fail(f'unreviewed override module {OVERRIDE}/{module}')
    gitlinks = {}
    for line in git('ls-files', '-s').decode().splitlines():
        meta, _, path = line.partition('\t')
        if meta.startswith('160000 '):
            gitlinks[path] = meta.split()[1]
    for sub in CLEAN_SUBMODULES:
        head = git('rev-parse', 'HEAD', cwd=ROOT / sub).decode().strip()
        if head != gitlinks.get(sub):
            fail(f'{sub} is at {head}, expected its gitlink {gitlinks.get(sub)}')
        if git('status', '--porcelain', '--untracked-files=no', cwd=ROOT / sub).strip():
            fail(f'{sub} has local changes; fork changes belong in {OVERRIDE}')
    for path, expected in MU_INPUTS.items():
        if hashlib.sha256((ROOT / 'Common/MU' / path).read_bytes()).hexdigest() != expected:
            fail(f'unreviewed Common/MU input {path}')
    body = b''.join(ASSEMBLY.read_bytes().splitlines(keepends=True)[5:])
    public = (ROOT / OVERRIDE / 'ArmPkg/Library/ArmExceptionLib/AArch64/ExceptionSupport.S').read_bytes()
    if body.replace(b'\r\n', b'\n') != public.replace(b'\r\n', b'\n'):
        fail(f'unreviewed exception assembly body: {ASSEMBLY.relative_to(ROOT)}')
    print('J804 windows overrides: pinned inputs verified')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

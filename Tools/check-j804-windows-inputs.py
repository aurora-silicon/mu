#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the pinned public inputs of the imported Aura Windows overlays.

AuraArmExceptionLib and AuraVarPolicyFoundationDxe #include pinned TIANO and
Microsoft Mu sources. This guard (modelled on windows-mu-handoff's
check-j813-exception-base.py and check-j813-phase-policy.py, same expected
hashes) checks:
  * the pinned Git objects of those inputs still have the reviewed hashes;
  * AuraExceptionSupport.S, minus its five prefix lines, equals the pinned
    ExceptionSupport.S object;
  * the working tree of each input differs from the pinned object only by
    the Tools/j873-patches change for that file (or not at all).
Host-only; reads the checkout, writes nothing.
"""
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIANO = 'Silicon/ARM/TIANO'
MU = 'Common/MU'
PINNED = {
    # Public TIANO 7b3db1e6fe68d5d456512a88c217ec1dcde31e3b.
    (TIANO, '7b3db1e6fe68d5d456512a88c217ec1dcde31e3b'): {
        'ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.c':
            'af01b3d6f21e866ee7f4d7eefc1dbf69f49be315d5dcc0cc5b4cbf1a1b06b033',
        'ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c':
            'dd0f0adaa06e7aa0128a5d0405f83717428b19fa99354db40c7d0749be5405a8',
        'ArmPkg/Library/ArmExceptionLib/AArch64/ExceptionSupport.S':
            '576090cd5ea74943082f624cbc1f61be053190ac252b5d23f9768427af7f15e2',
    },
    # Public Common/MU ba613eea328e801d9582059a8e0af414097a7fa8.
    (MU, 'ba613eea328e801d9582059a8e0af414097a7fa8'): {
        'MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c':
            '1713498676f38c64108772f6564b9140ec7f83090cde189ec9bb1e9cc02dd1d4',
        'MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf':
            '119999e86df929b63e7b22d85891c39aec0a245c2c5d069a3e049cd3688a8462',
        'MsCorePkg/Include/Guid/MuVarPolicyFoundationDxe.h':
            'ceeca0aec0a364b295a11f85b105efb8f689c6a77ee0477be31ac78224881254',
    },
}
# Working-tree changes allowed on top of the pinned objects.
PATCHES = {
    TIANO: ('arm-stack-alignment.patch',
            {'ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c'}),
    MU: ('mu-plus-ebs.patch',
         {'MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c'}),
}
ASSEMBLY = ROOT / 'Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/AuraExceptionSupport.S'


def git(repo, *args):
    return subprocess.run(['git', '-C', str(ROOT / repo), *args],
                          check=True, capture_output=True).stdout


def fail(message):
    raise SystemExit(f'check-j804-windows-inputs: {message}')


def main():
    for (repo, commit), files in PINNED.items():
        head = git(repo, 'rev-parse', 'HEAD').decode().strip()
        if head != commit:
            fail(f'{repo} is at {head}, expected pinned {commit}')
        for path, expected in files.items():
            blob = git(repo, 'show', f'{commit}:{path}')
            if hashlib.sha256(blob).hexdigest() != expected:
                fail(f'unreviewed pinned input {repo}/{path}')
        patch_name, allowed = PATCHES[repo]
        changed = set(git(repo, 'diff', '--name-only').decode().split())
        watched = changed & set(files)
        if not watched <= allowed:
            fail(f'{repo}: unexpected working-tree changes {sorted(watched - allowed)}')
        if watched:
            # Rebuild "pinned object + patch" in a scratch directory and
            # require the working tree to equal it byte for byte.
            patch = ROOT / 'Tools/j873-patches' / patch_name
            with tempfile.TemporaryDirectory() as scratch:
                for path in sorted(allowed):
                    target = Path(scratch) / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(git(repo, 'show', f'{commit}:{path}'))
                subprocess.run(['git', 'apply', '--ignore-space-change', str(patch)],
                               cwd=scratch, check=True, capture_output=True)
                for path in sorted(watched):
                    expected_bytes = (Path(scratch) / path).read_bytes()
                    if (ROOT / repo / path).read_bytes() != expected_bytes:
                        fail(f'{repo}/{path} differs from the pinned object by more than {patch_name}')
    body = b''.join(ASSEMBLY.read_bytes().splitlines(keepends=True)[5:])
    public = git(TIANO, 'show',
                 '7b3db1e6fe68d5d456512a88c217ec1dcde31e3b:'
                 'ArmPkg/Library/ArmExceptionLib/AArch64/ExceptionSupport.S')
    if body.replace(b'\r\n', b'\n') != public.replace(b'\r\n', b'\n'):
        fail(f'unreviewed exception assembly body: {ASSEMBLY.relative_to(ROOT)}')
    print('J804 windows overlays: pinned public inputs verified')


if __name__ == '__main__':
    sys.exit(main())

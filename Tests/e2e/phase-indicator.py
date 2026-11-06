#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Replay the hardware EBS assertion through the complete production phase driver."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = Path('Common/MU/MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c')
WRAPPER = Path('Silicon/Apple/AppleSiliconPkg/Drivers/AuraVarPolicyFoundationDxe/AuraVarPolicyFoundationDxe.c')
RUNTIME = Path('MU_BASECORE/MdePkg/Library/UefiRuntimeLib/RuntimeLib.c')
CORE = Path('MU_BASECORE/MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-unfixed', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    include = out/'include'; include.mkdir()
    source = UPSTREAM if args.expect_unfixed else WRAPPER
    # Check the actual Core sequence represented by the host service scheduler.
    core = (ROOT/CORE).read_text()
    begin = core.index('if (!mExitBootServicesCalled)')
    before = core.index('CoreNotifySignalList (&gEfiEventBeforeExitBootServicesGuid)', begin)
    once = core.index('mExitBootServicesCalled = TRUE;', before)
    terminate = core.index('CoreTerminateMemoryMap (MapKey)', once)
    normal = core.index('CoreNotifySignalList (&gEfiEventExitBootServicesGuid)', terminate)
    assert begin < before < once < terminate < normal
    runtime = (ROOT/RUNTIME).read_text()
    transition = re.search(r'VOID\s+EFIAPI\s+RuntimeLibExitBootServicesEvent\s*\([^)]*\)\s*\{[^}]*\}', runtime)
    assert transition and 'mEfiAtRuntime = TRUE;' in transition[0]
    assert re.search(r'EVT_SIGNAL_EXIT_BOOT_SERVICES,\s+TPL_NOTIFY,\s+RuntimeLibExitBootServicesEvent', runtime)
    (include/'runtime-transition.inc').write_text(transition[0]+'\n')
    for name in ('Library/DebugLib.h', 'Library/BaseLib.h', 'Library/UefiBootServicesTableLib.h',
                 'Library/UefiRuntimeServicesTableLib.h', 'Library/BaseMemoryLib.h',
                 'Library/MemoryAllocationLib.h', 'Library/VariablePolicyHelperLib.h',
                 'Protocol/VariablePolicy.h', 'Guid/EventGroup.h', 'Uefi.h'):
        path = include/name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "phase-indicator-host.h"\n')
    files = (source, UPSTREAM, RUNTIME, CORE,
             Path('Common/MU/MsCorePkg/Include/Guid/MuVarPolicyFoundationDxe.h'),
             Path('Docs/windows-phase-indicator.md'),
             *(Path('Tests/e2e')/name for name in ('phase-indicator.py', 'phase-indicator-host.c', 'phase-indicator-host.h')))
    hashes = {}
    for path in files:
        dest = out/'sources'/path; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT/path, dest); hashes[str(path)] = hashlib.sha256(dest.read_bytes()).hexdigest()
    command = ['clang', '-std=c17', '-fshort-wchar', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-pointer-sign', '-fsanitize=address,undefined',
               '-I', str(include), '-I', str(ROOT/'Tests/e2e'),
               '-I', str(ROOT/'Common/MU/MsCorePkg/Include'),
               '-DPOLICY_SOURCE="'+str(ROOT/source)+'"',
               str(ROOT/'Tests/e2e/phase-indicator-host.c'), '-o', str(out/'phase-indicator')]
    built = subprocess.run(command, text=True, capture_output=True)
    (out/'compile.log').write_text(built.stdout+built.stderr)
    assert built.returncode == 0, f'compile failed: {out}/compile.log'
    results = []
    for case in (('unfixed',) if args.expect_unfixed else ('normal', 'missing-phases', 'retry', 'service-failure')):
        run = subprocess.run([str(out/'phase-indicator'), case], text=True, capture_output=True)
        (out/(case+'.log')).write_text(run.stdout+run.stderr)
        assert run.returncode == 0, f'{case} failed: {out}/{case}.log'
        results.append({'case': case, 'exit_status': run.returncode})
    receipt = {'schema': 'mu.phase-indicator.replay.v1', 'hardware_qualified': False,
               'result': 'unfixed-reproduced' if args.expect_unfixed else 'passed',
               'sources': hashes, 'command': command, 'cases': results}
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'result': receipt['result'], 'cases': results}))

if __name__ == '__main__': main()

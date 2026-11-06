#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Run production Mu runtime forwarding against a logged modeled HVC boundary."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
FILES=("Tests/e2e/runtime-forwarding-host.h","Tests/e2e/runtime-forwarding-host.c",
       "Tests/e2e/memory-peim-host.h","Tests/e2e/runtime-forwarding.py",
       "Docs/windows-runtime-forwarding.md",
       "Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/AuraExceptionForward.c",
       "Silicon/Apple/AppleSiliconPkg/Include/Library/AuraExceptionForward.h",
       "Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AuraN1Read.c",
       "Silicon/Apple/AppleSiliconPkg/Include/IndustryStandard/AuraN1Handoff.h")
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    headers=out/'include';headers.mkdir()
    for relative in ('Uefi.h','Base.h','Library/SynchronizationLib.h','Library/ArmHvcLib.h','Library/BaseMemoryLib.h','Library/CpuExceptionHandlerLib.h'):
        dest=headers/relative;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('#include "runtime-forwarding-host.h"\n')
    hashes={}
    for relative in FILES:
        dest=out/'sources'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/relative,dest)
        hashes[relative]=hashlib.sha256(dest.read_bytes()).hexdigest()
    sources=[ROOT/FILES[5],ROOT/FILES[7],ROOT/FILES[1]]
    command=['clang','-std=c17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I',str(headers),'-I',str(ROOT/'Tests/e2e'),'-I',str(ROOT/'Silicon/Apple/AppleSiliconPkg/Include'),
             *map(str,sources),'-o',str(out/'runtime-forwarding')]
    build=subprocess.run(command,text=True,capture_output=True);(out/'compile.log').write_text(build.stdout+build.stderr)
    assert build.returncode==0,f'compile failed: {out}/compile.log'
    run=subprocess.run([str(out/'runtime-forwarding')],text=True,capture_output=True);(out/'run.log').write_text(run.stdout+run.stderr)
    assert run.returncode==0,f'run failed: {out}/run.log'
    receipt={'schema':'mu.runtime-forwarding.e2e.v1','hardware_qualified':False,'sources':hashes,'command':command,
             'result':json.loads(run.stdout)}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt['result']))
if __name__=='__main__': main()

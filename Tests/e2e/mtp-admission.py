#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Replay actual MTP AML publication admission against the Aura handoff marker."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCE='Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AcpiPlatform.c'
ASL='Platform/MacBookAir2026Pkg/AcpiTables/MTP.asl'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--expect-unfixed',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report={'result':'failed','hardware_touched':False,'sources':{},'commands':[],'cases':[]}
    def run(command,log):
        command=[str(x) for x in command];report['commands'].append(command)
        r=subprocess.run(command,cwd=out,text=True,capture_output=True,timeout=30)
        (out/log).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
    try:
        for n in [SOURCE,ASL,'Tests/e2e/mtp-admission-host.c','Tests/e2e/mtp-admission.py','Docs/windows-mtp-admission.md']:
            p=out/'sources'/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/n,p);report['sources'][n]=sha(p)
        source=(out/'sources'/SOURCE).read_text()
        gate=''
        if not args.expect_unfixed:
            start=source.index('STATIC\nBOOLEAN\nAppleMtpTableAdmitted (')
            end=source.index('\n}\n',start)+3;gate=source[start:end]
        (out/'mtp-gate.inc').write_text(gate)
        match=re.search(r'      if \(!AppleN1TableAdmitted[^\n]*(?:\n[^\n]*)*?\n      }\n',source)
        assert match,'publication admission branch missing'
        (out/'mtp-publication.inc').write_text(match[0])
        run(['iasl','-p',out/'MTP',out/'sources'/ASL],'iasl.log')
        run(['clang','-std=c17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',
             '-I',out,out/'sources/Tests/e2e/mtp-admission-host.c','-o',out/'mtp-host'],'compile.log')
        for case in [2] if args.expect_unfixed else range(9):
            result=json.loads(run([out/'mtp-host',out/'MTP.aml',case],f'case-{case}.log'))
            expected=1 if args.expect_unfixed or case in [0,1,6,7] else 0
            assert result['published']==expected and result['freed']==(not expected)
            report['cases'].append(result)
        report.update(result='unfixed-reproduced' if args.expect_unfixed else 'passed',aml_sha256=sha(out/'MTP.aml'),
                      boundary='Actual MTP AML and production publication admission; modeled ADT property and install service')
    except (OSError,ValueError,AssertionError,subprocess.SubprocessError) as e:report['error']=str(e)
    finally:(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['result']+': '+str(out/'receipt.json'))
    return int(report['result']=='failed')
if __name__=='__main__':raise SystemExit(main())

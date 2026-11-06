#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Execute production MemoryPeim with modeled firmware services; no hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path("Silicon/Apple/T8142FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c")
CASES=("normal", "at-base", "at-end", "zero-adt", "oversize-adt", "unaligned",
       "outside-ram", "overlap-fd", "overlap-uefi", "overlap-existing-hole", "overlap-appended",
       "mtp-active", "mtp-disabled", "mtp-absent", "mtp-no-chosen", "mtp-invalid-length", "mtp-invalid-value",
       "mtp-outside-below", "mtp-outside-above", "mtp-partial-start", "mtp-partial-end",
       "mtp-at-base", "mtp-at-end", "mtp-overlap-fd", "mtp-overlap-uefi",
       "mtp-overlap-boot", "mtp-overlap-hole")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expect-unfixed", action="store_true")
    parser.add_argument("--expect-mtp-unfixed", action="store_true")
    parser.add_argument("--expect-mtp-legacy-unfixed", action="store_true")
    args=parser.parse_args(); out=args.output.resolve(); out.mkdir(parents=True,exist_ok=False)
    include=out/"include"; include.mkdir()
    # Keep the real production source and public data structures. Only service
    # headers absent from a native process receive the host-service declarations.
    headers=("PiPei.h", "Base.h", "Library/ArmMmuLib.h", "Library/ArmPlatformLib.h",
             "Library/DebugLib.h", "Library/HobLib.h", "Library/MemoryAllocationLib.h",
             "Library/PcdLib.h", "Library/IoLib.h", "Library/PrintLib.h")
    for name in headers:
        dest=include/name; dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text('#include "memory-peim-host.h"\n')
    files=(SOURCE, Path("Tests/e2e/memory-peim-host.h"), Path("Tests/e2e/memory-peim-host.c"),
           Path("Tests/e2e/memory-peim.py"), Path("Docs/windows-boot-handoff-memory.md"), Path("Docs/windows-mtp-memory.md"),
           Path("Silicon/Apple/AppleSiliconPkg/Include/Library/AppleDTLib.h"),
           Path("Silicon/Apple/AppleSiliconPkg/Include/AppendedRamdisk.h"),
           Path("Silicon/Apple/AppleSiliconPkg/Include/IndustryStandard/AuraMtpHandoff.h"),
           Path("Silicon/Apple/T8142FamilyPkg/Include/Library/T8142FamilyVirtualMemoryMapDefines.h"))
    hashes={}
    for name in files:
        dest=out/"sources"/name; dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/name,dest); hashes[str(name)]=hashlib.sha256(dest.read_bytes()).hexdigest()
    command=["clang", "-std=c17", "-D_GNU_SOURCE", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
             "-Wno-unused-function", "-fsanitize=address,undefined", "-I",str(include),
             "-I",str(ROOT/"Tests/e2e"), "-I",str(ROOT/"Silicon/Apple/AppleSiliconPkg/Include"),
             "-I",str(ROOT/"Silicon/Apple/T8142FamilyPkg/Include"),
             "-include",str(ROOT/"Tests/e2e/memory-peim-host.h"),str(ROOT/SOURCE),
             str(ROOT/"Tests/e2e/memory-peim-host.c"),"-o",str(out/"memory-peim")]
    built=subprocess.run(command,text=True,capture_output=True)
    (out/"compile.log").write_text(built.stdout+built.stderr)
    if built.returncode: raise RuntimeError(f"compile failed: {out/'compile.log'}")
    # ASan reserves a shadow span covering the literal 1-TiB physical ABI
    # address. Keep ASan for relocatable cases; execute the actual-address
    # MTP cases with UBSan and checked VM allocation instead.
    mtp_command=[x.replace("-fsanitize=address,undefined","-fsanitize=undefined") for x in command]
    mtp_command[-1]=str(out/"memory-peim-mtp")
    mtp_built=subprocess.run(mtp_command,text=True,capture_output=True)
    (out/"compile-mtp.log").write_text(mtp_built.stdout+mtp_built.stderr)
    if mtp_built.returncode: raise RuntimeError("MTP compile failed")
    results=[]
    for case in (("mtp-absent",) if args.expect_mtp_legacy_unfixed else ("mtp-active",) if args.expect_mtp_unfixed else ("normal",) if args.expect_unfixed else CASES):
        result=subprocess.run([str(out/("memory-peim-mtp" if case.startswith("mtp-") else "memory-peim")),case],text=True,capture_output=True)
        (out/(case+".log")).write_text(result.stdout+result.stderr)
        results.append({"case":case,"exit_status":result.returncode})
        if args.expect_mtp_unfixed or args.expect_mtp_legacy_unfixed:
            assert result.returncode == 6 and "MTP staging or RTKit pool remains allocatable" in result.stderr
        elif args.expect_unfixed:
            assert result.returncode == 3 and "remains allocatable" in result.stderr
        else: assert result.returncode == 0, f"case failed: {case}; see {out}"
    receipt={"schema":"mu.memory-peim.e2e.v1", "result":"unfixed-reproduced" if args.expect_unfixed or args.expect_mtp_unfixed or args.expect_mtp_legacy_unfixed else "passed",
             "hardware_qualified":False, "source_sha256":hashes,"compile_command":command,"mtp_compile_command":mtp_command,"cases":results}
    (out/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt))

if __name__ == "__main__": main()

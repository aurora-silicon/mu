#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build real Mu ADT/admission/PEI/DXE code with bounded host firmware services."""
import argparse, hashlib, json, pathlib, subprocess, shlex
ROOT = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=pathlib.Path, required=True)
a = parser.parse_args(); out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
includes = ['Tools/g17-preservation-e2e/include', 'MU_BASECORE/MdePkg/Include',
            'MU_BASECORE/MdePkg/Include/AArch64', 'MU_BASECORE/MdeModulePkg/Include', 'MU_BASECORE/ArmPkg/Include',
            'Silicon/Apple/AppleSiliconPkg/Include']
sources = ['Tools/g17-preservation-e2e/consumer.c',
           'Silicon/Apple/AppleSiliconPkg/PrePi/AdtParser.c',
           'Silicon/Apple/AppleSiliconPkg/Library/AppleDTLib/AppleDTLib.c',
           'Silicon/Apple/AppleSiliconPkg/Library/AuraG17PreservationLib/AuraG17Preservation.c',
           'Silicon/Apple/AppleSiliconPkg/Library/AuraG17PreservationLib/AuraG17Firmware.c',
           'Silicon/Apple/T8142FamilyPkg/Library/MemoryInitPeiLib/AuraG17Pei.c',
           'Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AuraG17Preservation.c']
command = ['clang', '-std=c17', '-fshort-wchar', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
           '-fsanitize=address,undefined']
for inc in includes: command += ['-I', str(ROOT/inc)]
compile_flags = command[:]
# Only the host's post-CopyMem physical destination needs translation. Include
# the real source after its public headers so this macro replaces the call,
# never the parser's declaration or implementation.
wrapper = out/'early-setup-host.c'
wrapper.write_text('#include <Uefi.h>\n#include <Library/AppleDTLib.h>\n'
                   'int HostDtCheck(void *, size_t, size_t *);\n'
                   '#define dt_check HostDtCheck\n'
                   '#include "' + str(ROOT/'Silicon/Apple/AppleSiliconPkg/PrePi/AdtParser.c') + '"\n')
compiled_sources = [str(wrapper) if x.endswith('/PrePi/AdtParser.c') else str(ROOT/x) for x in sources]
command += compiled_sources + ['-o', str(out/'mu-g17-consumer')]
p = subprocess.run(command, capture_output=True, text=True)
(out/'compile.log').write_text(p.stdout+p.stderr)
if p.returncode: raise SystemExit(f'compile failed: {out / "compile.log"}')
files = sources + ['Tools/g17-preservation-e2e/build.py',
         'Tools/g17-preservation-e2e/include/Library/PcdLib.h',
         'Silicon/Apple/T8142FamilyPkg/Library/MemoryInitPeiLib/MemoryReservation.h',
         'Silicon/Apple/AppleSiliconPkg/Include/IndustryStandard/AuraG17Preservation.h',
         'Silicon/Apple/AppleSiliconPkg/Include/Library/AuraG17PreservationLib.h']
# Let the compiler report transitive inputs, including real EDK2 type headers.
dependencies = set()
for source in compiled_sources:
    dep = subprocess.run(compile_flags + ['-M', '-MT', 'g17', source],
                         check=True, capture_output=True, text=True)
    flattened = dep.stdout.replace('\\\n', ' ')
    dependencies.update(shlex.split(flattened.partition(':')[2]))
dependency_hashes = {}
for name in sorted(dependencies):
    path = pathlib.Path(name).resolve()
    try: key = str(path.relative_to(ROOT))
    except ValueError: key = str(path)
    dependency_hashes[key] = hashlib.sha256(path.read_bytes()).hexdigest()
receipt = {'schema':'mu.g17.consumer-build.v1', 'hardware_touched':False, 'command':command,
           'source_sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
           'compiler_dependency_sha256':dependency_hashes,
           'binary_sha256':hashlib.sha256((out/'mu-g17-consumer').read_bytes()).hexdigest()}
(out/'build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(out/'mu-g17-consumer')

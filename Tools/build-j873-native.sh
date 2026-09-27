#!/bin/sh
# SPDX-License-Identifier: MIT
# Native J873 Mu firmware qualification; no target access or disk installation.
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$source_root"
test "$(git branch --show-current)" = j873 || {
    echo "J873 firmware must be built from the j873 branch" >&2
    exit 1
}
test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64
python3 "$source_root/Tools/apply-j873-patches.py"
venv=${AURORADBG_MU_NATIVE_VENV:-$source_root/.venv-native}
if test ! -x "$venv/bin/stuart_build"; then
    python3.12 -m venv "$venv"
    "$venv/bin/python" -m pip install -r pip-requirements.txt 'setuptools<81'
fi
llvm_bin=${AURORADBG_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
lld_link=${AURORADBG_LLD_LINK:-/opt/homebrew/opt/lld/bin/lld-link}
profile=${AURORADBG_MU_PROFILE:-native}
case "$profile" in
    native) windows_flag=FALSE; output_suffix=mu ;;
    windows) windows_flag=TRUE; output_suffix=mu-windows ;;
    *) echo "Unsupported J873 Mu profile: $profile" >&2; exit 1 ;;
esac
output_root=${AURORADBG_MU_OUTPUT_ROOT:-$source_root/../windows/build-$output_suffix}
mkdir -p "$output_root/toolchain" "$output_root/artifacts"
toolchain_dir=$output_root/toolchain
for tool in clang llvm-lib llvm-rc; do
    test -x "$llvm_bin/$tool"
    ln -sfn "$llvm_bin/$tool" "$toolchain_dir/$tool"
done
test -x "$lld_link"
ln -sfn "$lld_link" "$toolchain_dir/lld-link"
base_tools=$source_root/MU_BASECORE/BaseTools
host_tools=$base_tools/Bin/Mu-Basetools_extdep/MacOs-ARM-64
test -x "$host_tools/GenFfs" || {
    echo "Missing native BaseTools: run stuart_update and build BaseTools first" >&2
    exit 1
}
export PATH="$venv/bin:$toolchain_dir:$llvm_bin:$base_tools/BinWrappers/PosixLike:$host_tools:$PATH"
export CLANG_BIN="$toolchain_dir/"
export CLANG_HOST_BIN=/usr/bin/
export UNIX_IASL_BIN=$(command -v iasl)
log=$output_root/native-build.log
"$venv/bin/stuart_build" -c Platform/MacMini2026Pkg/PlatformBuild.py \
    TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG "BLD_*_J873_WINDOWS=$windows_flag" > "$log" 2>&1 || {
    tail -50 "$log" >&2
    exit 1
}
fd=$source_root/Build/MacMini2026-AARCH64/DEBUG_CLANGPDB/FV/J873MACMINI2026_EFI.fd
test -f "$fd"
cp "$fd" "$output_root/artifacts/J873MACMINI2026_EFI.fd"
"$venv/bin/python" - "$source_root" "$output_root/artifacts" "$profile" <<'PY'
import hashlib, json, pathlib, struct, subprocess, sys
root, output = map(pathlib.Path, sys.argv[1:3])
profile = sys.argv[3]
fd = output / 'J873MACMINI2026_EFI.fd'
data = fd.read_bytes()
assert len(data) == 0x1e00000
assert data[0x38:0x3c] == b'ARM\x64'
assert struct.unpack_from('<Q', data, 0x10)[0] == len(data)
assert data[0x8028:0x802c] == b'_FVH'
record = dict(schema='aurora.j873.mu-native.v1',
              status='BUILT_NOT_HARDWARE_QUALIFIED', profile=profile,
              soc='T8152', model='J873gAP', windows_pe_booted=False,
              target_storage_writes=False, acpi_platform_published=profile == 'windows',
              front_usb=profile == 'windows',
              rear_usb=profile == 'windows',
              nvme=profile == 'windows',
              smc=profile == 'windows',
              guest_cpu_ids=[6,7,8,9,10,11,0,1,2,3,4,5] if profile == 'windows' else [],
              firmware=dict(path=fd.name, size=len(data), sha256=hashlib.sha256(data).hexdigest()),
              source_commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
              source_dirty=bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True)))
(output / 'manifest.json').write_text(json.dumps(record, indent=2)+'\n')
print('J873_MU_BUILT', record['firmware']['sha256'])
PY

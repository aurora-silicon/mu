#!/bin/sh
# SPDX-License-Identifier: MIT
# J804 M1 firmware build: the J873 native profile plus the four M1 changes
# (Null serial with panel checkpoints, ADT-driven RAM reservations, native-EL2
# safety, J804 startup.nsh). Host-only: no target access, no disk writes.
#
# Usage:  sh Tools/build-j804-m1.sh
#
# Environment:
#   WM6_J804_M1=FALSE        build the plain J873 native profile from this
#                            branch instead (toolchain baseline)
#   WM6_OUTPUT_ROOT          output directory (default ../out/j804-m1, or
#                            ../out/j873-native-baseline with WM6_J804_M1=FALSE)
#   WM6_VENV                 venv made by Tools/wm6-env.sh (default ../venv)
#   WM6_CLEAN=1              delete Build/MacMini2026-AARCH64 first
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$source_root"
test "$(git branch --show-current)" = j804-m1 || {
    echo "J804 M1 firmware must be built from the j804-m1 branch" >&2
    exit 1
}
test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64
case "$source_root" in
    *' '*) echo "EDK2 cannot build in a path that contains spaces" >&2; exit 1 ;;
esac
python3 "$source_root/Tools/apply-j873-patches.py"
parent=$(dirname -- "$source_root")
venv=${WM6_VENV:-${AURORADBG_MU_NATIVE_VENV:-$parent/venv}}
if test ! -x "$venv/bin/stuart_build"; then
    echo "Missing venv $venv: run Tools/wm6-env.sh first" >&2
    exit 1
fi
llvm_bin=${AURORADBG_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
lld_link=${AURORADBG_LLD_LINK:-/opt/homebrew/opt/lld/bin/lld-link}
j804_m1=${WM6_J804_M1:-TRUE}
case "$j804_m1" in
    TRUE) profile=j804-m1; default_out=$parent/out/j804-m1 ;;
    FALSE) profile=j873-native-baseline; default_out=$parent/out/j873-native-baseline ;;
    *) echo "WM6_J804_M1 must be TRUE or FALSE" >&2; exit 1 ;;
esac
output_root=${WM6_OUTPUT_ROOT:-$default_out}
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
    echo "Missing native BaseTools: run Tools/wm6-env.sh first" >&2
    exit 1
}
build_dir=$source_root/Build/MacMini2026-AARCH64
if test "${WM6_CLEAN:-0}" = 1; then
    rm -rf "$build_dir"
fi
export PATH="$venv/bin:$toolchain_dir:$llvm_bin:$base_tools/BinWrappers/PosixLike:$host_tools:$PATH"
export CLANG_BIN="$toolchain_dir/"
export CLANG_HOST_BIN=/usr/bin/
export UNIX_IASL_BIN=$(command -v iasl)
log=$output_root/build.log
"$venv/bin/stuart_build" -c Platform/MacMini2026Pkg/PlatformBuild.py \
    TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG "BLD_*_J873_WINDOWS=FALSE" \
    "BLD_*_J804_M1=$j804_m1" "BLD_*_SOURCE_PREFIX_MAP=$source_root" \
    > "$log" 2>&1 || {
    tail -50 "$log" >&2
    exit 1
}
out=$build_dir/DEBUG_CLANGPDB
fd=$out/FV/J873MACMINI2026_EFI.fd
test -f "$fd"
fd_name=J804M1_EFI.fd
test "$j804_m1" = TRUE || fd_name=J873MACMINI2026_EFI.fd
cp "$fd" "$output_root/artifacts/$fd_name"
"$venv/bin/python" "$source_root/Tools/wm6-manifest.py" \
    --source-root "$source_root" --build-dir "$out" \
    --fd "$output_root/artifacts/$fd_name" --profile "$profile" \
    --out "$output_root/artifacts"

#!/bin/sh
# SPDX-License-Identifier: MIT
# J804 windows-profile firmware build (Platform/MacBookPro2026Pkg): Mu for
# EL1 under the aura carrier, with the J804 M1 safety changes kept.
# Host-only: no target access, no disk writes outside the build and output
# directories.
#
# Usage:  sh Tools/build-j804-windows.sh
#
# Environment:
#   WM6_OUTPUT_ROOT          output directory (default ../out/j804-windows)
#   WM6_VENV                 venv made by Tools/wm6-env.sh (default ../venv)
#   WM6_CLEAN=1              delete Build/MacBookPro2026-AARCH64 first
#   WM6_J804_ANS=TRUE        ANS0 present (_STA 0xF) instead of disabled
#   WM6_J804_VUART=TRUE      carrier vUART build (serial lib, COM0, DBG2)
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$source_root"
test "$(git branch --show-current)" = j804-windows || {
    echo "J804 windows firmware must be built from the j804-windows branch" >&2
    exit 1
}
test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64
case "$source_root" in
    *' '*) echo "EDK2 cannot build in a path that contains spaces" >&2; exit 1 ;;
esac
python3 "$source_root/Tools/apply-j873-patches.py"
python3 "$source_root/Tools/check-j804-windows-inputs.py"
parent=$(dirname -- "$source_root")
venv=${WM6_VENV:-${AURORADBG_MU_NATIVE_VENV:-$parent/venv}}
if test ! -x "$venv/bin/stuart_build"; then
    echo "Missing venv $venv: run Tools/wm6-env.sh first" >&2
    exit 1
fi
llvm_bin=${AURORADBG_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
lld_link=${AURORADBG_LLD_LINK:-/opt/homebrew/opt/lld/bin/lld-link}
ans=${WM6_J804_ANS:-FALSE}
vuart=${WM6_J804_VUART:-FALSE}
for flag in "$ans" "$vuart"; do
    case "$flag" in TRUE|FALSE) ;; *) echo "WM6_J804_* must be TRUE or FALSE" >&2; exit 1 ;; esac
done
output_root=${WM6_OUTPUT_ROOT:-$parent/out/j804-windows}
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
build_dir=$source_root/Build/MacBookPro2026-AARCH64
if test "${WM6_CLEAN:-0}" = 1; then
    rm -rf "$build_dir"
fi
export PATH="$venv/bin:$toolchain_dir:$llvm_bin:$base_tools/BinWrappers/PosixLike:$host_tools:$PATH"
export CLANG_BIN="$toolchain_dir/"
export CLANG_HOST_BIN=/usr/bin/
export UNIX_IASL_BIN=$(command -v iasl)
log=$output_root/build.log
"$venv/bin/stuart_build" -c Platform/MacBookPro2026Pkg/PlatformBuild.py \
    TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG \
    "BLD_*_J804_WINDOWS_ANS=$ans" "BLD_*_J804_WINDOWS_VUART=$vuart" \
    "BLD_*_SOURCE_PREFIX_MAP=$source_root" \
    > "$log" 2>&1 || {
    tail -50 "$log" >&2
    exit 1
}
out=$build_dir/DEBUG_CLANGPDB
fd=$out/FV/J804MACBOOKPRO2026_EFI.fd
test -f "$fd"
cp "$fd" "$output_root/artifacts/J804WIN_EFI.fd"
"$venv/bin/python" "$source_root/Tools/wm6-manifest.py" \
    --source-root "$source_root" --build-dir "$out" \
    --fd "$output_root/artifacts/J804WIN_EFI.fd" --profile j804-windows \
    --out "$output_root/artifacts"
# The build log names the checkout path; keep it out of the hand-over files.
sed "s|$source_root|<mu>|g; s|$parent|<wm6>|g; s|$HOME|<home>|g" "$log" > "$log.tmp" && mv "$log.tmp" "$log"

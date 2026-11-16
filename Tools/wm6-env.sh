#!/bin/sh
# SPDX-License-Identifier: MIT
# Prepare the host build environment for this tree (macOS on arm64 with
# Homebrew). Host-only: nothing here talks to a target machine.
#
# Usage:  sh Tools/wm6-env.sh
#
# Environment overrides:
#   WM6_VENV               Python venv to create or reuse (default: ../venv
#                          next to this checkout)
#   AURORADBG_LLVM_BIN     Homebrew llvm bin directory
#   AURORADBG_LLD_LINK     lld-link binary
#   WM6_REBUILD_BASETOOLS  set to 1 to rebuild the C BaseTools even if present
#
# Steps (the recipe that is known to work for the j873 line):
#   1. check the toolchain: Homebrew llvm + lld, iasl, nasm, python3.12
#   2. git submodule update --init --recursive (pinned, non-tip SHAs)
#   3. Tools/apply-j873-patches.py (two submodule patches)
#   4. Python 3.12 venv with pip-requirements.txt plus setuptools<81
#   5. stuart_update for MacMini2026Pkg
#   6. build the C BaseTools and install them as the MacOs-ARM-64 extdep,
#      which the Mu-Basetools extdep does not ship
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$source_root"

fail() { echo "wm6-env: $*" >&2; exit 1; }

test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64 ||
    fail "needs macOS on arm64"
case "$source_root" in
    *' '*) fail "EDK2 cannot build in a path that contains spaces: $source_root" ;;
esac

llvm_bin=${AURORADBG_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
lld_link=${AURORADBG_LLD_LINK:-/opt/homebrew/opt/lld/bin/lld-link}
for tool in clang llvm-lib llvm-rc; do
    test -x "$llvm_bin/$tool" || fail "missing $llvm_bin/$tool (brew install llvm)"
done
test -x "$lld_link" || fail "missing $lld_link (brew install lld)"
command -v iasl >/dev/null || fail "missing iasl (brew install acpica)"
command -v nasm >/dev/null || fail "missing nasm (brew install nasm)"
command -v python3.12 >/dev/null || fail "missing python3.12 (brew install python@3.12)"

echo "== submodules"
git submodule update --init --recursive
git submodule status

echo "== j873 submodule patches"
python3 Tools/apply-j873-patches.py

venv=${WM6_VENV:-$(dirname -- "$source_root")/venv}
if test ! -x "$venv/bin/stuart_build"; then
    echo "== venv $venv"
    python3.12 -m venv "$venv"
    "$venv/bin/python" -m pip install -r pip-requirements.txt 'setuptools<81'
fi

echo "== stuart_update"
"$venv/bin/stuart_update" -c Platform/MacMini2026Pkg/PlatformBuild.py \
    TOOL_CHAIN_TAG=CLANGPDB

host_tools=MU_BASECORE/BaseTools/Bin/Mu-Basetools_extdep/MacOs-ARM-64
if test "${WM6_REBUILD_BASETOOLS:-0}" = 1 || test ! -x "$host_tools/GenFfs"; then
    echo "== C BaseTools"
    make -C MU_BASECORE/BaseTools/Source/C -j"$(sysctl -n hw.ncpu)"
    mkdir -p "$host_tools"
    cp MU_BASECORE/BaseTools/Source/C/bin/* "$host_tools/"
fi

echo "== toolchain"
"$llvm_bin/clang" --version | head -1
"$lld_link" --version | head -1
iasl -v 2>/dev/null | grep -i version | head -1 || true
nasm -v
"$venv/bin/python" --version
"$venv/bin/python" -m pip list 2>/dev/null | grep -i '^edk2' || true
echo "wm6-env: ready (venv $venv)"

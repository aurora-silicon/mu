#!/bin/sh
# SPDX-License-Identifier: MIT
# Native Apple Silicon host build for the J813 M5 MacBook Air firmware.
set -eu

profile=${1:-uefi-shell-aic}
test "$profile" = uefi-shell-aic || {
    echo "error: unsupported J813 Mu profile: $profile" >&2
    exit 2
}

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64 || {
    echo "error: the native J813 build requires an Apple Silicon Mac" >&2
    exit 1
}

mu_python=${AURORADBG_MU_PYTHON:-$(command -v python3.12 || command -v python3)}
venv=${AURORADBG_MU_NATIVE_VENV:-$source_root/.venv-native}
if test ! -x "$venv/bin/stuart_build"; then
    "$mu_python" -m venv --clear "$venv"
    "$venv/bin/python" -m pip install --disable-pip-version-check -r "$source_root/pip-requirements.txt"
    "$venv/bin/python" -m pip install --disable-pip-version-check 'setuptools<81'
fi

llvm_bin=${AURORADBG_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}
for tool in clang llvm-lib llvm-rc; do
    test -x "$llvm_bin/$tool" || {
        echo "error: Homebrew LLVM tool missing: $llvm_bin/$tool" >&2
        exit 1
    }
done
lld_link=${AURORADBG_LLD_LINK:-$(command -v lld-link || true)}
test -n "$lld_link" && test -x "$lld_link" || {
    echo "error: lld-link is missing (install Homebrew lld)" >&2
    exit 1
}
for tool in make iasl nasm; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: required native build tool is missing: $tool" >&2
        exit 1
    }
done

output_root=${AURORADBG_MU_OUTPUT_ROOT:-$(dirname "$source_root")/AuroraSilicon/build/m5-air}
commit=$(git -C "$source_root" rev-parse HEAD)
output_dir=$output_root/$profile/$commit
artifact_dir=$output_dir/artifacts
toolchain_dir=$output_dir/native-toolchain
log=$output_dir/native-build.log
mkdir -p "$artifact_dir" "$toolchain_dir"

# CLANGPDB accepts a single prefix for tools that Homebrew splits between the
# llvm and lld formulae, so assemble a target-local prefix.
ln -sfn "$llvm_bin/clang" "$toolchain_dir/clang"
ln -sfn "$llvm_bin/llvm-lib" "$toolchain_dir/llvm-lib"
ln -sfn "$llvm_bin/llvm-rc" "$toolchain_dir/llvm-rc"
ln -sfn "$lld_link" "$toolchain_dir/lld-link"

base_tools=$source_root/MU_BASECORE/BaseTools
host_tools=$base_tools/Bin/Mu-Basetools_extdep/MacOs-ARM-64
if test ! -x "$host_tools/GenFfs"; then
    base_tools_work=$output_root/.native-basetools
    mkdir -p "$base_tools_work"
    ditto "$base_tools/Source/C" "$base_tools_work"
    make -C "$base_tools_work" EDK2_PATH="$source_root/MU_BASECORE" \
        -j "${AURORADBG_MU_JOBS:-$(sysctl -n hw.logicalcpu)}"
    test ! -e "$host_tools" || test -L "$host_tools" || {
        echo "error: refusing to replace native BaseTools directory: $host_tools" >&2
        exit 1
    }
    mkdir -p "$(dirname "$host_tools")"
    ln -sfn "$base_tools_work/bin" "$host_tools"
fi

export PATH="$venv/bin:$toolchain_dir:$llvm_bin:$base_tools/BinWrappers/PosixLike:$host_tools:$PATH"
export CLANG_BIN="$toolchain_dir/"
export CLANG_HOST_BIN=/usr/bin/
export UNIX_IASL_BIN=$(command -v iasl)

platform_build=Platform/MacBookAir2026Pkg/PlatformBuild.py
: > "$log"
echo "Building J813 $profile"
if ! "$venv/bin/stuart_build" -c "$platform_build" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG >> "$log" 2>&1; then
    # A cold parallel EDK2 build can race GenFw and llvm-rc for HelloWorld's HII
    # resource. Complete that deterministic one-file step and retry once.
    if grep -q 'HelloWorldhii.lib' "$log" && grep -q 'Exactly one input file' "$log"; then
        rc=$(find "$source_root/Build/MacBookAir2026-AARCH64" -name HelloWorldhii.rc -print | head -1)
        test -n "$rc"
        "$toolchain_dir/llvm-rc" "/Fo$(dirname "$rc")/HelloWorldhii.lib" "$rc"
        "$venv/bin/stuart_build" -c "$platform_build" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG >> "$log" 2>&1 || {
            tail -80 "$log" >&2
            exit 1
        }
    else
        tail -80 "$log" >&2
        exit 1
    fi
fi

fd=$source_root/Build/MacBookAir2026-AARCH64/DEBUG_CLANGPDB/FV/J813MACBOOKAIR2026_EFI.fd
test -f "$fd"
artifact=$artifact_dir/J813MACBOOKAIR2026_EFI.fd
cp "$fd" "$artifact"
digest=$(shasum -a 256 "$artifact" | awk '{print $1}')
size=$(stat -f %z "$artifact")
clean=true
test -z "$(git -C "$source_root" status --porcelain=v1 --untracked-files=no --ignore-submodules=none)" || clean=false
manifest=$artifact_dir/manifest.json
"$venv/bin/python" - "$manifest" "$commit" "$clean" "$profile" "$size" "$digest" <<'PY'
import json
import pathlib
import sys

path, commit, clean, profile, size, digest = sys.argv[1:]
record = {
    "schema": "aurora.j813.mu-profile.v1",
    "artifact_status": "READY_FOR_SUPERVISED_HARDWARE_TEST",
    "hardware_touched": False,
    "profile": {"name": profile, "aic": True},
    "source": {"commit": commit, "clean": clean == "true"},
    "firmware": {
        "path": "artifacts/J813MACBOOKAIR2026_EFI.fd",
        "size": int(size),
        "sha256": digest,
    },
}
pathlib.Path(path).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
PY

echo "READY_TO_TEST $profile"
echo "source=$commit"
echo "fd=$artifact"
echo "sha256=$digest"
echo "manifest=$manifest"

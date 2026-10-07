#!/bin/sh
# SPDX-License-Identifier: MIT
# Native Linux aarch64 host build for Aurora Mu platforms (CLANGPDB, DEBUG).
#
#   Tools/build-linux-native.sh <PlatformBuild.py> <FD file name> [BLD_*_NAME=VALUE ...]
#
# Example (J873 Windows profile):
#   Tools/build-linux-native.sh Platform/MacMini2026Pkg/PlatformBuild.py \
#       J873MACMINI2026_EFI.fd 'BLD_*_J873_WINDOWS=TRUE'
#
# No target access, flashing or disk installation happens here.
#
# Optional environment:
#   AURORADBG_MU_ENV_SH       host env script sourced first (default: ../env.sh if present)
#   AURORADBG_MU_OUTPUT_ROOT  artifact/cache root (default: ../mu-out)
#   AURORADBG_MU_NATIVE_VENV  Python 3.11/3.12 venv (default: .venv-native)
#   AURORADBG_LLVM_BIN        directory holding clang/llvm-lib/llvm-rc/lld-link (default: from PATH)
#   AURORADBG_MU_HOST_CC      compiler for BaseTools C tools: clang (default) or gcc
#   AURORADBG_MU_JOBS         BaseTools make parallelism (default: nproc)
#   AURORADBG_MU_PROFILE      artifact subdirectory name (default: derived from the defines)
#   AURORADBG_MU_CLEAN=1      delete this platform's Build/<name>-AARCH64 tree first
#   AURORADBG_MU_APPLY_J873_PATCHES  auto (default; MacMini2026Pkg and MacBookProM5Pkg), 1 or 0
set -eu

usage() {
    echo "usage: $0 <PlatformBuild.py> <FD file name> [BLD_*_NAME=VALUE ...]" >&2
    exit 2
}
test "$#" -ge 2 || usage
platform_arg=$1
fd_name=$2
shift 2
case "$fd_name" in
    */*|'') echo "error: pass the FD file name only, not a path: $fd_name" >&2; exit 2 ;;
esac
for define in "$@"; do
    case "$define" in
        BLD_*=*) ;;
        *) echo "error: extra arguments must be stuart BLD_*_NAME=VALUE defines: $define" >&2; exit 2 ;;
    esac
done

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
env_sh=${AURORADBG_MU_ENV_SH:-$(dirname "$source_root")/env.sh}
# shellcheck disable=SC1090
test ! -f "$env_sh" || . "$env_sh"

test "$(uname -s)" = Linux && test "$(uname -m)" = aarch64 || {
    echo "error: this script builds on a Linux aarch64 host only" >&2
    exit 1
}
cd "$source_root"

case "$platform_arg" in
    /*) platform_build=$platform_arg ;;
    *) platform_build=$source_root/$platform_arg ;;
esac
test -f "$platform_build" || { echo "error: no such PlatformBuild.py: $platform_arg" >&2; exit 1; }
platform_build=$(CDPATH= cd -- "$(dirname -- "$platform_build")" && pwd)/$(basename -- "$platform_build")
platform_rel=${platform_build#"$source_root"/}
package_dir=$(dirname "$platform_build")

# Resolve the DSC the builder hardcodes, then its PLATFORM_NAME and output tree.
dsc_rel=$(sed -n 's/.*"ACTIVE_PLATFORM", *"\([^"]*\.dsc\)", *"Platform Hardcoded".*/\1/p' "$platform_build" | head -1)
test -n "$dsc_rel" || { echo "error: cannot find ACTIVE_PLATFORM in $platform_rel" >&2; exit 1; }
dsc=$(dirname "$package_dir")/$dsc_rel
test -f "$dsc" || { echo "error: active DSC not found: $dsc" >&2; exit 1; }
dsc_value() {
    sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([^[:space:]#]*\).*/\1/p" "$dsc" | head -1
}
platform_name=$(dsc_value PLATFORM_NAME)
output_directory=$(dsc_value OUTPUT_DIRECTORY | sed 's/\$(ARCH)/AARCH64/')
test -n "$platform_name" && test -n "$output_directory" || {
    echo "error: $dsc lacks PLATFORM_NAME or OUTPUT_DIRECTORY" >&2
    exit 1
}

for submodule in MU_BASECORE Common/MU Common/TIANO Common/MU_OEM_SAMPLE \
    Silicon/ARM/TIANO Common/MU_DFCI mu_feature_debugger; do
    test -e "$source_root/$submodule/.git" || {
        echo "error: submodule is not initialized: $submodule" >&2
        echo "run: git -C '$source_root' submodule update --init --recursive" >&2
        exit 1
    }
done

apply_j873=${AURORADBG_MU_APPLY_J873_PATCHES:-auto}
if test "$apply_j873" = auto; then
    case "$platform_rel" in
        Platform/MacMini2026Pkg/*|Platform/MacBookProM5Pkg/*) apply_j873=1 ;;
        *) apply_j873=0 ;;
    esac
fi

# Host tools. EDK2's CLANGPDB accepts one prefix for clang, llvm-lib, llvm-rc
# and lld-link, so assemble a platform-local prefix from whatever is resolved.
find_tool() {
    if test -n "${AURORADBG_LLVM_BIN:-}"; then
        test -x "$AURORADBG_LLVM_BIN/$1" && { echo "$AURORADBG_LLVM_BIN/$1"; return 0; }
        return 1
    fi
    command -v "$1"
}
for tool in make iasl; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: required native build tool is missing: $tool" >&2
        exit 1
    }
done

output_root=${AURORADBG_MU_OUTPUT_ROOT:-$(dirname "$source_root")/mu-out}
mkdir -p "$output_root"
output_root=$(CDPATH= cd -- "$output_root" && pwd)
# Keep every temporary download and unpack (stuart ext deps) on the build volume.
export TMPDIR="$output_root/.tmp"
mkdir -p "$TMPDIR"

lock_dir=$output_root/.native-build-lock
if ! mkdir "$lock_dir" 2>/dev/null; then
    echo "error: another native Mu build owns $source_root/Build ($lock_dir)" >&2
    exit 1
fi
trap 'rmdir "$lock_dir" 2>/dev/null || true' EXIT
trap 'exit 130' HUP INT TERM

toolchain_dir=$output_root/$platform_name/toolchain
mkdir -p "$toolchain_dir"
for tool in clang llvm-lib llvm-rc lld-link; do
    path=$(find_tool "$tool") || {
        echo "error: LLVM tool missing: $tool (set AURORADBG_LLVM_BIN)" >&2
        exit 1
    }
    ln -sfn "$path" "$toolchain_dir/$tool"
done

mu_python=${AURORADBG_MU_PYTHON:-python3.12}
venv=${AURORADBG_MU_NATIVE_VENV:-$source_root/.venv-native}
if test ! -x "$venv/bin/stuart_build"; then
    if command -v uv >/dev/null 2>&1; then
        uv venv --python 3.12 "$venv"
        VIRTUAL_ENV=$venv uv pip install -r "$source_root/pip-requirements.txt" 'setuptools<81'
    else
        "$mu_python" -m venv --clear "$venv"
        "$venv/bin/python" -m pip install --disable-pip-version-check \
            -r "$source_root/pip-requirements.txt" 'setuptools<81'
    fi
fi
"$venv/bin/python" -c 'import sys; raise SystemExit(0 if (3, 11) <= sys.version_info[:2] < (3, 13) else 1)' || {
    echo "error: the pinned Mu Python tools require Python 3.11 or 3.12: $venv" >&2
    exit 1
}

if test "$apply_j873" = 1; then
    "$venv/bin/python" "$source_root/Tools/apply-j873-patches.py"
fi

commit=$(git -C "$source_root" rev-parse HEAD)
dirty=false
test -z "$(git -C "$source_root" status --porcelain=v1 --untracked-files=no --ignore-submodules=none)" || dirty=true

if test -n "${AURORADBG_MU_PROFILE:-}"; then
    profile=$AURORADBG_MU_PROFILE
elif test "$#" -eq 0; then
    profile=default
else
    profile=$(printf '%s\n' "$@" | sed 's/^BLD_[^_]*_//' | tr '=' '-' | tr '\n' '+' \
        | sed 's/+$//' | tr -c 'A-Za-z0-9._+-' '_')
fi
output_dir=$output_root/$platform_name/$commit/$profile
artifact_dir=$output_dir/artifacts
mkdir -p "$artifact_dir"
log=$output_dir/native-build.log
: > "$log"
started=$(date +%s)

run_logged() {
    label=$1
    shift
    echo "$label"
    if ! "$@" >> "$log" 2>&1; then
        echo "error: $label failed; tail of $log:" >&2
        tail -80 "$log" >&2
        exit 1
    fi
}

# BaseTools C tools, built once per (MU_BASECORE source, host compiler) from an
# output-local copy so no object files land inside the MU_BASECORE gitlink.
base_tools=$source_root/MU_BASECORE/BaseTools
host_cc=${AURORADBG_MU_HOST_CC:-clang}
case "$host_cc" in
    clang) host_make_args="CC=clang CXX=clang++" ;;
    # GCC 15+ defaults to C23, where glibc's qualifier-preserving strstr breaks
    # EfiRom.c, and GCC 16 flags a counter-only variable in StringFuncs.c.
    gcc) host_make_args="CC=gcc CXX=g++" host_extra_optflags="-std=gnu17 -Wno-error=unused-but-set-variable" ;;
    *) echo "error: AURORADBG_MU_HOST_CC must be clang or gcc" >&2; exit 1 ;;
esac
base_tools_key=$(
    {
        git -C "$source_root/MU_BASECORE" rev-parse HEAD
        git -C "$source_root/MU_BASECORE" diff HEAD -- BaseTools/Source/C
        git -C "$source_root/MU_BASECORE" ls-files --others --exclude-standard -- BaseTools/Source/C
        "$host_cc" --version | head -1
        echo "$host_make_args ${host_extra_optflags:-}"
    } | sha256sum | cut -c1-16
)
base_tools_work=$output_root/.native-basetools/$base_tools_key
base_tools_bin=$base_tools_work/bin
if test ! -f "$base_tools_work/.aurora-basetools-built"; then
    rm -rf "$base_tools_work"
    mkdir -p "$base_tools_work"
    cp -a "$base_tools/Source/C/." "$base_tools_work/"
    # shellcheck disable=SC2086
    run_logged "Building native EDK2 BaseTools ($host_cc, key $base_tools_key)" \
        make -C "$base_tools_work" EDK2_PATH="$source_root/MU_BASECORE" \
        $host_make_args ${host_extra_optflags:+"EXTRA_OPTFLAGS=$host_extra_optflags"} \
        -j "${AURORADBG_MU_JOBS:-$(nproc)}"
    test -x "$base_tools_bin/GenFfs" && test -x "$base_tools_bin/GenFw"
    : > "$base_tools_work/.aurora-basetools-built"
else
    echo "Using cached native EDK2 BaseTools (key $base_tools_key)"
fi

export PATH="$venv/bin:$toolchain_dir:$PATH"
export CLANG_BIN="$toolchain_dir/"
export CLANG_HOST_BIN="$(dirname "$(command -v make)")/"
export UNIX_IASL_BIN="$(command -v iasl)"

# Stuart verifies every global ext dep before building. Fetching is a no-op
# once each dep's state file matches its pinned version.
run_logged "Verifying pinned Mu ext deps (stuart_update)" \
    "$venv/bin/stuart_update" -c "$platform_rel" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG

# The Mu-Basetools release archive ships prebuilt Linux-ARM-64 binaries, and
# stuart resolves the host-specific directory Linux-ARM-64 first. Point that
# name at the tools built above from the pinned source; keep the download.
host_tools=$base_tools/Bin/Mu-Basetools_extdep/Linux-ARM-64
test -f "$base_tools/Bin/Mu-Basetools_extdep/extdep_state.yaml" || {
    echo "error: stuart_update did not provide Mu-Basetools_extdep" >&2
    exit 1
}
if test -d "$host_tools" && test ! -L "$host_tools"; then
    rm -rf "$host_tools.prebuilt"
    mv "$host_tools" "$host_tools.prebuilt"
fi
ln -sfn "$base_tools_bin" "$host_tools"
export PATH="$host_tools:$base_tools/BinWrappers/PosixLike:$PATH"

build_tree=$source_root/$output_directory
if test "${AURORADBG_MU_CLEAN:-0}" = 1; then
    echo "Removing $output_directory"
    rm -rf "$build_tree"
fi

echo "Building $platform_name ($profile) with native CLANGPDB"
if ! "$venv/bin/stuart_build" -c "$platform_rel" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG "$@" >> "$log" 2>&1; then
    echo "error: stuart_build failed; tail of $log:" >&2
    tail -80 "$log" >&2
    exit 1
fi

fd=$build_tree/DEBUG_CLANGPDB/FV/$fd_name
test -f "$fd" || { echo "error: FD not produced: $fd" >&2; exit 1; }
artifact=$artifact_dir/$fd_name
cp "$fd" "$artifact"
digest=$(sha256sum "$artifact" | awk '{print $1}')
size=$(stat -c %s "$artifact")
elapsed=$(( $(date +%s) - started ))
{
    echo "platform=$platform_name"
    echo "platform_build=$platform_rel"
    echo "profile=$profile"
    echo "defines=$*"
    echo "source_commit=$commit"
    echo "source_dirty=$dirty"
    echo "host=$(uname -srm)"
    echo "clang=$("$toolchain_dir/clang" --version | head -1)"
    echo "lld_link=$("$toolchain_dir/lld-link" --version | head -1)"
    echo "basetools_key=$base_tools_key ($host_cc)"
    echo "fd=$fd_name"
    printf 'size=%s (0x%x)\n' "$size" "$size"
    echo "sha256=$digest"
    echo "elapsed_seconds=$elapsed"
} > "$artifact_dir/build-info.txt"

echo "BUILT $platform_name $profile"
echo "source=$commit dirty=$dirty"
echo "log=$log"
echo "fd=$artifact"
printf 'size=%s (0x%x)\n' "$size" "$size"
echo "sha256=$digest"
echo "elapsed=${elapsed}s"

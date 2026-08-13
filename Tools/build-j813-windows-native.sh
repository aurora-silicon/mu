#!/bin/sh
# SPDX-License-Identifier: MIT
# Native Apple Silicon host build for the J813 M5 MacBook Air firmware.
set -eu

profile=${1:-uefi-shell-aic}
case "$profile" in
    uefi-shell-aic)
        ans=false
        mtp_hid_build=TRUE
        ;;
    internal-storage)
        ans=true
        ans_block_io=TRUE
        mtp_hid_build=FALSE
        ;;
    internal-storage-windows)
        # internal-storage, plus the \_SB.ANS0 ACPI device Windows needs to bind
        # a storage driver.  Kept as a separate profile rather than flipping the
        # one above: that profile is the proven firmware-owned boot path, and
        # publishing a device to Windows is exactly the kind of change worth
        # being able to A/B against it.
        ans=true
        ans_block_io=TRUE
        ans_acpi=TRUE
        # Must stay identical to internal-storage above except for ans_acpi.
        # It was briefly TRUE here, which made this profile differ from the
        # proven one in two ways at once; the resulting boot spun inside Mu
        # (guest PCs clustered under the Mu vbar, framebuffer at 0.03 fps) and
        # never reached Setup, and the second variable made that unattributable.
        mtp_hid_build=FALSE
        ;;
    storage-probe)
        ans=true
        ans_block_io=FALSE
        mtp_hid_build=FALSE
        ;;
    *)
        echo "error: unsupported J813 Mu profile: $profile" >&2
        exit 2
        ;;
esac

ans_acpi=${ans_acpi:-FALSE}
if test "$ans" = true; then
    ans_enable=TRUE
    ans_dxe=TRUE
    ans_preserve=TRUE
else
    ans_enable=FALSE
    ans_dxe=FALSE
    ans_block_io=FALSE
    ans_preserve=FALSE
    ans_acpi=FALSE
fi

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test "$(uname -s)" = Darwin && test "$(uname -m)" = arm64 || {
    echo "error: the native J813 build requires an Apple Silicon Mac" >&2
    exit 1
}
cd "$source_root"

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
if ! "$venv/bin/stuart_build" -c "$platform_build" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG \
    "BLD_*_NTASI_ENABLE_ANS=$ans_enable" \
    "BLD_*_NTASI_ANS_PUBLISH_ACPI=$ans_acpi" \
    "BLD_*_NTASI_ANS_DXE_BRINGUP=$ans_dxe" \
    "BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO=$ans_block_io" \
    "BLD_*_NTASI_ANS_PRESERVE_FOR_OS=$ans_preserve" \
    "BLD_*_MTP_HID_BUILD=$mtp_hid_build" >> "$log" 2>&1; then
    # Homebrew llvm-rc parses an absolute POSIX input path beginning with '/' as
    # an option. Complete the deterministic HII resource step from its output
    # directory with relative paths, then retry the incremental build once.
    if grep -q 'HelloWorldhii.lib' "$log" && grep -q 'Exactly one input file' "$log"; then
        rc=$(find "$source_root/Build/MacBookAir2026-AARCH64" -name HelloWorldhii.rc -print | head -1)
        test -n "$rc"
        rc_dir=$(dirname "$rc")
        (
            cd "$rc_dir"
            "$toolchain_dir/llvm-rc" /FoHelloWorldhii.lib HelloWorldhii.rc
        )
        "$venv/bin/stuart_build" -c "$platform_build" TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG \
            "BLD_*_NTASI_ENABLE_ANS=$ans_enable" \
            "BLD_*_NTASI_ANS_PUBLISH_ACPI=$ans_acpi" \
            "BLD_*_NTASI_ANS_DXE_BRINGUP=$ans_dxe" \
            "BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO=$ans_block_io" \
            "BLD_*_NTASI_ANS_PRESERVE_FOR_OS=$ans_preserve" \
            "BLD_*_MTP_HID_BUILD=$mtp_hid_build" >> "$log" 2>&1 || {
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
"$venv/bin/python" - "$manifest" "$commit" "$clean" "$profile" "$size" "$digest" \
    "$ans_enable" "$ans_dxe" "$ans_block_io" "$ans_preserve" "$ans_acpi" <<'PY'
import json
import pathlib
import sys

(path, commit, clean, profile, size, digest, ans, ans_dxe, ans_block_io,
 ans_preserve, ans_acpi) = sys.argv[1:]
enabled = lambda value: value == "TRUE"
record = {
    "schema": "aurora.j813.mu-profile.v1",
    "artifact_status": "READY_FOR_SUPERVISED_HARDWARE_TEST",
    "hardware_touched": False,
    "profile": {
        "name": profile,
        "aic": True,
        "ans": enabled(ans),
        "ans_acpi": enabled(ans_acpi),
        "ans_dxe": enabled(ans_dxe),
        "ans_block_io": enabled(ans_block_io),
        "ans_preserve": enabled(ans_preserve),
        "baseline_capabilities": {
            "usb3_deferred_pipe_switch_port_mask": 0,
            "usb_dwc3_reset_dart_handoff":
                "m1n1_reset_clamped_mu_dart_bypass_release_v1",
        },
        "pmu_contract": {
            "backend": "m1n1_el2_t8142",
            "windows_pmu_compat_dxe_embedded": False,
            "microsoft_pe_images_modified": False,
        },
    },
    "build": {
        "pcds": {
            "PcdAppleUsb3PipeSwitchPortMask": 0,
            "PcdAppleAnsPublishAcpiDevice": enabled(ans_acpi),
            "PcdAppleAnsPerformDxeBringUp": enabled(ans_dxe),
            "PcdAppleAnsPublishBlockIo": enabled(ans_block_io),
            "PcdAppleAnsPreserveForOs": enabled(ans_preserve),
            "PcdAppleAnsPmgrResetBase": "0x380700300",
            "PcdAppleAnsPmgrApcieStBase": "0x380700410",
            "PcdAppleAnsPmgrApcieStSysBase": "0x380700520",
            "PcdAppleAnsPmgrApcieSt1SysBase": "0x0",
        },
    },
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

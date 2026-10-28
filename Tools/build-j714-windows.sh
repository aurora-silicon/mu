#!/usr/bin/env bash
# Build the J714s RAM-boot profile used for N1 bring-up. No deployment.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "$root/Tools/build-linux-native.sh" \
 Platform/MacBookProM5Pkg/PlatformBuild.py J714MACBOOKPROM5_EFI.fd \
 'BLD_*_J714_WINDOWS=TRUE' 'BLD_*_J714_HARDWARE=TRUE' \
 'BLD_*_J714_LARGE_BANK=TRUE' 'BLD_*_J714_NATIVE_FIQ=TRUE' \
 'BLD_*_J714_BGR_DIAGNOSTIC=FALSE' 'BLD_*_J714_MONITOR_CPU=TRUE' \
 'BLD_*_J714_NVME=TRUE' 'BLD_*_J714_USB3=TRUE' \
 'BLD_*_J714_USB_HOSTS=TRUE' 'BLD_*_J714_SMC=TRUE' \
 'BLD_*_J714_KBL=TRUE' 'BLD_*_J714_FULL_RAM=TRUE' \
 'BLD_*_J714_USB_INSTALLER=FALSE' 'BLD_*_J714_N1=TRUE' "$@"

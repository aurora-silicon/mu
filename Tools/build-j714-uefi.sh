#!/usr/bin/env bash
# Host-only interactive UEFI candidate. No target access or disk writes.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export AURORADBG_MU_PROFILE=${AURORADBG_MU_PROFILE:-j714-interactive-uefi}
exec "$root/Tools/build-j714-windows.sh" \
 'BLD_*_J714_UEFI_MENU=TRUE' 'BLD_*_J714_UEFI_KBL=TRUE' \
 'BLD_*_J714_UEFI_MTP=TRUE' \
 'BLD_*_J714_UEFI_ANS=FALSE' 'BLD_*_J714_DCP=FALSE' "$@"

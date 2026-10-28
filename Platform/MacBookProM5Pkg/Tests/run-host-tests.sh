#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tests="$root/Platform/MacBookProM5Pkg/Tests"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
"${CC:-clang}" -std=c11 -fshort-wchar -ffunction-sections -fdata-sections \
 -fsanitize=address,undefined -g -Wl,--gc-sections \
 -I"$root/MU_BASECORE/MdePkg/Include" -I"$root/MU_BASECORE/MdePkg/Include/AArch64" \
 -I"$root/MU_BASECORE/MdeModulePkg/Include" \
 "$tests/BootMenuTests.c" -o "$out/boot-menu"
"$out/boot-menu"
shared="$root/Platform/MacBookProM5Pkg/Drivers/J714MtpKeyboardDxe/Shared"
"${CC:-clang}" -std=c11 -fshort-wchar -ffunction-sections -fdata-sections \
 -fsanitize=address,undefined -g -Wl,--gc-sections \
 -I"$root/MU_BASECORE/MdePkg/Include" -I"$root/MU_BASECORE/MdePkg/Include/AArch64" \
 -I"$root/MU_BASECORE/MdeModulePkg/Include" -I"$root/Silicon/ARM/TIANO/ArmPkg/Include" \
 -I"$root/Common/MU/HidPkg/Include" -I"$root/Silicon/Apple/AppleSiliconPkg/Include" \
 "$tests/MtpKeyboardTests.c" "$shared/AppleMtpBootKeyboardCore.c" \
 "$shared/AppleDockChannel.c" "$shared/AppleMtpProtocol.c" \
 "$shared/AppleMtpStream.c" "$shared/AppleMtpBootstrap.c" "$shared/AppleMtpHid.c" \
 -o "$out/mtp-keyboard"
"$out/mtp-keyboard"
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
s=(Path(sys.argv[1])/'Platform/MacBookProM5Pkg/Drivers/J714Usb3Dxe/J714Usb3Dxe.c').read_text()
a=s.index('STATIC BOOLEAN IsFirmwareMenu(')
b=s.index('STATIC VOID UpdateBootServicesCrc',a)
(Path(sys.argv[2])/'FirmwareBoundaryUnderTest.h').write_text(s[a:b])
PY
"${CC:-clang}" -std=c11 -fshort-wchar -fsanitize=address,undefined -g \
 -I"$root/MU_BASECORE/MdePkg/Include" -I"$root/MU_BASECORE/MdePkg/Include/AArch64" \
 -I"$out" "$tests/TimerBoundaryTests.c" -o "$out/timer-boundary"
"$out/timer-boundary"

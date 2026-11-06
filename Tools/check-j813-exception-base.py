#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Reject unreviewed or dirty public inputs to the J813 exception overlay."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Silicon/ARM/TIANO/ArmPkg/Library/ArmExceptionLib"
# Public TIANO commit 7b3db1e6fe68d5d456512a88c217ec1dcde31e3b.
EXPECTED = {
    "ArmExceptionLib.c": "af01b3d6f21e866ee7f4d7eefc1dbf69f49be315d5dcc0cc5b4cbf1a1b06b033",
    "AArch64/AArch64Exception.c": "dd0f0adaa06e7aa0128a5d0405f83717428b19fa99354db40c7d0749be5405a8",
    "AArch64/ExceptionSupport.S": "576090cd5ea74943082f624cbc1f61be053190ac252b5d23f9768427af7f15e2",
}

def main():
    for relative, expected in EXPECTED.items():
        actual = hashlib.sha256((BASE / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f"unreviewed J813 exception base: {BASE / relative}")
    assembly = ROOT / "Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/AuraExceptionSupport.S"
    body = b"".join(assembly.read_bytes().splitlines(keepends=True)[5:])
    public_body = (BASE / "AArch64/ExceptionSupport.S").read_bytes()
    # The root repository uses text=auto; tolerate Git's CRLF normalization.
    if body.replace(b"\r\n", b"\n") != public_body.replace(b"\r\n", b"\n"):
        raise SystemExit(f"unreviewed J813 exception assembly body: {assembly}")
    print("J813 exception overlay: pinned public inputs verified")

if __name__ == "__main__":
    main()

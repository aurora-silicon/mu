#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Reject unreviewed or dirty inputs to the J813 exception overlay."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "Silicon/Apple/AppleSiliconPkg/Override/ArmPkg/Library/ArmExceptionLib"
# The ArmExceptionLib override: Microsoft bd7850321a76 plus the J813 change
# from mu_silicon_arm_tiano 7b3db1e6fe68 (LF, as the repository checks it out).
EXPECTED = {
    "ArmExceptionLib.c": "a7b0273488abf5a50c9df11b4a32c81f2421d68f9623e15ff35a56a1bcf52c2f",
    "AArch64/AArch64Exception.c": "079f7da5de1d954c4e41fe3ce42badd0efabfe8cd6208521cbf494b62eea4dd8",
    "AArch64/ExceptionSupport.S": "ad268ca17c500cfb94de9c9423ab7fdae1ad5c547c93ab0c50fdb255d24edb96",
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
    print("J813 exception overlay: override inputs verified")

if __name__ == "__main__":
    main()

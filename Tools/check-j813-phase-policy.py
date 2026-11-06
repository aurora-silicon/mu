#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Verify the public inputs reused by the Windows phase-policy wrapper."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Public Common/MU commit ba613eea328e801d9582059a8e0af414097a7fa8.
EXPECTED = {
    "MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c":
        "1713498676f38c64108772f6564b9140ec7f83090cde189ec9bb1e9cc02dd1d4",
    "MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf":
        "119999e86df929b63e7b22d85891c39aec0a245c2c5d069a3e049cd3688a8462",
    "MsCorePkg/Include/Guid/MuVarPolicyFoundationDxe.h":
        "ceeca0aec0a364b295a11f85b105efb8f689c6a77ee0477be31ac78224881254",
}

def main():
    for relative, expected in EXPECTED.items():
        source = ROOT/"Common/MU"/relative
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"unreviewed Windows phase-policy input: {source}")
    print("J813 phase policy: pinned public inputs verified")

if __name__ == "__main__":
    main()

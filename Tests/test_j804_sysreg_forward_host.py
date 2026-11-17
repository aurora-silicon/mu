#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Compile and run the AuraExceptionForward host test (Tests/j804).

Builds the imported Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/
AuraExceptionForward.c unchanged, with ASan/UBSan, against host stand-ins and
a modelled carrier HVC, and runs it. Host-only; skips without clang.
"""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
HERE = REPO / "Tests" / "j804"
SOURCE = REPO / "Silicon/Apple/AppleSiliconPkg/Library/AuraArmExceptionLib/AuraExceptionForward.c"
STUBS = ("Uefi.h", "Library/CpuExceptionHandlerLib.h", "Library/ArmHvcLib.h",
         "Library/SynchronizationLib.h", "Library/BaseMemoryLib.h")


@unittest.skipUnless(shutil.which("clang"), "clang not found")
class AuraSysregForwardHost(unittest.TestCase):
    def test_forwarding_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            include = Path(tmp) / "include"
            for stub in STUBS:
                path = include / stub
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "sysreg_forward_host.h"\n')
            exe = Path(tmp) / "sysreg-forward"
            command = ["clang", "-std=c17", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-I", str(include), "-I", str(HERE),
                       "-I", str(REPO / "Silicon/Apple/AppleSiliconPkg/Include"),
                       str(SOURCE), str(HERE / "sysreg_forward_host.c"), "-o", str(exe)]
            build = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('"result": "pass"', run.stdout)


if __name__ == "__main__":
    unittest.main()

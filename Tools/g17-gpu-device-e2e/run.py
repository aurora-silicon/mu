#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline check of the J813 G17 GPU SSDT: real built GPU.aml through the real admission code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCE = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AuraG17GpuDevice.c"
# The display-owner selection the admission code consults (no marker: legacy).
SELECTION = SOURCE.with_name("AuraJ813UnifiedDisplay.c")
AML = ROOT / ("Build/MacBookAir2026-AARCH64/DEBUG_CLANGPDB/AARCH64/MacBookAir2026Pkg/"
              "AcpiTables/DeviceAcpiTables/OUTPUT/GPU.aml")
# _CRS contract (AuroraSilicon integration/agx-wddm-g17-boot/README.md) with the
# saved J813 resident-ADT regions used by harness.c.
EXPECTED = [
    (0x580000000, 0x1000000), (0x582600000, 0x88000), (0x582E00000, 0x88000),
    (0x103FFF30000, 0x4000), (0x103FFFB8000, 0x4000), (0x103FFF38000, 0x80000),
    (0x103FFF34000, 0x4000), (0x1000248C000, 0x134000), (0x100025C0000, 0x130000),
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def qword_memory(table):
    """Independent parse of every QWord memory descriptor in the table."""
    found, i = [], 36
    while i + 46 <= len(table):
        if table[i] == 0x8A and table[i + 1:i + 3] == b"\x2b\x00" and table[i + 3] == 0:
            # tag, length(2), type, general flags, type flags, then
            # granularity, minimum, maximum, translation, length (ACPI 6.4.3.5.1)
            _, mn, mx, _, ln = struct.unpack_from("<QQQQQ", table, i + 6)
            found.append((mn, mx, ln))
            i += 46
        else:
            i += 1
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"result": "incomplete", "scope": "offline; not hardware qualification",
              "sources": {str(p.relative_to(ROOT)): sha(p) for p in
                          [SOURCE, SELECTION, HERE / "harness.c", HERE / "run.py",
                           ROOT / "Platform/J813BoardPkg/AcpiTables/GPU.asl"]},
              "built_aml": {"path": str(AML.relative_to(ROOT)), "sha256": sha(AML)},
              "commands": []}

    def run(command, name):
        command = list(map(str, command))
        report["commands"].append(command)
        with (out / name).open("wb") as log:
            return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                  timeout=300).returncode

    try:
        cc = os.environ.get("CC", "clang")
        for pcd in (1, 0):
            binary = out / f"harness-pcd{pcd}"
            assert run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                        f"-DHOST_PCD_G17_GPU_PROBE={pcd}", "-I", HERE / "shim",
                        HERE / "harness.c", SOURCE, SELECTION, "-o", binary],
                       f"build-pcd{pcd}.log") == 0
            assert run([binary, AML, out / "GPU.patched.aml"], f"run-pcd{pcd}.log") == 0, \
                f"harness failed with PCD {pcd}"
            log = (out / f"run-pcd{pcd}.log").read_text()
            report[f"checks_pcd{pcd}"] = int(log.strip().splitlines()[-1].split()[-1])
        template = AML.read_bytes()
        patched = (out / "GPU.patched.aml").read_bytes()
        assert len(patched) == len(template) and patched[:4] == b"SSDT"
        resources = qword_memory(patched)
        assert len(resources) == len(EXPECTED), resources
        for (mn, mx, ln), (base, size) in zip(resources, EXPECTED):
            assert (mn, mx, ln) == (base, base + size - 1, size), hex(mn)
        report["resources"] = [{"base": hex(b), "size": hex(s)} for b, s in EXPECTED]
        (out / "GPU.patched.aml").write_bytes(patched)
        rc = run(["iasl", "-p", out / "GPU.patched", "-d", out / "GPU.patched.aml"], "iasl.log")
        report["iasl_disassembly"] = rc == 0
        report["result"] = "passed"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"result": report["result"], "receipt": str(out / "result.json")}))


if __name__ == "__main__":
    main()

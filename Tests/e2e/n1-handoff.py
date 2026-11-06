#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline handoff fault campaign and compiled AML contract; never opens hardware.

The failure model is in Docs/j813-n1-handoff.md. This is not a substitute for
the Aura -> Mu -> Windows device bring-up and ownership test described there.
"""
import argparse
import hashlib
import json
import platform
import re
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SILICON = Path("Silicon/Apple/AppleSiliconPkg")
SOURCES = [
    Path("Docs/j813-n1-handoff.md"),
    Path("Tests/e2e/n1-handoff.c"),
    Path("Tests/e2e/n1-handoff.py"),
    Path("Tests/e2e/runtime-forwarding-host.h"),
    Path("Tests/e2e/memory-peim-host.h"),
    SILICON / "Drivers/AcpiPlatformDxe/AuraN1Read.c",
    SILICON / "Drivers/AcpiPlatformDxe/AuraN1Handoff.c",
    SILICON / "Drivers/AcpiPlatformDxe/AcpiPlatform.c",
    SILICON / "Include/IndustryStandard/AuraN1Handoff.h",
    Path("Platform/MacBookAir2026Pkg/AcpiTables/N1.asl"),
]
CASES = """success radios-present missing truncated version flags identity stream
resource read-failure link-down root-id control-id bme bar bar-high rom geometry
protected live-tcr live-ttbr""".split()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    commands = []

    def run(argv, log):
        commands.append([str(a) for a in argv])
        result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False)
        (out / log).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}); see {out / log}")
        return result.stdout

    for relative in SOURCES:
        target = out / "sources" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, target)
    include = out / "include"
    for name in ("Uefi.h", "Library/ArmHvcLib.h", "Library/BaseMemoryLib.h"):
        target = include / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "runtime-forwarding-host.h"\n')
    run(["clang", "-std=c17", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-DAURA_N1_HVC_E2E", "-I", str(include), "-I", "Tests/e2e",
         "-fsanitize=address,undefined", "-I", str(SILICON / "Include"),
         str(SILICON / "Drivers/AcpiPlatformDxe/AuraN1Handoff.c"),
         str(SILICON / "Drivers/AcpiPlatformDxe/AuraN1Read.c"),
         "Tests/e2e/n1-handoff.c", "-o", str(out / "handoff")], "compile.log")
    cases = []
    for name in CASES:
        output = run([str(out / "handoff"), name], name + ".jsonl")
        result = json.loads(output.splitlines()[-1])
        assert result["case"] == name and result["result"] == "passed", result
        cases.append(result)

    asl = "Platform/MacBookAir2026Pkg/AcpiTables/N1.asl"
    run(["iasl", "-ve", "-p", str(out / "N1"), asl], "iasl.log")
    run(["iasl", "-ve", "-p", str(out / "N1-repeat"), asl], "iasl-repeat.log")
    aml = (out / "N1.aml").read_bytes()
    assert aml == (out / "N1-repeat.aml").read_bytes(), "non-repeatable AML"
    assert aml[:4] == b"SSDT" and aml[16:24] == b"AURA813N"
    assert struct.unpack_from("<I", aml, 4)[0] == len(aml) and sum(aml) % 256 == 0
    output = run(["acpiexec", "-b",
                  r"execute \_SB.N1PC._HID; execute \_SB.N1PC._STA; "
                  r"execute \_SB.N1PC._CCA; execute \_SB.N1PC._CRS",
                  str(out / "N1.aml")], "acpiexec.log")
    assert '[String] Length 08 = "AURA8130"' in output
    assert "[Integer] = 000000000000000F" in output
    assert "[Integer] = 0000000000000001" in output
    assert "AE_" not in output, "AML evaluation error"
    resource_bytes = bytes.fromhex(" ".join(re.findall(
        r"^\s+[0-9A-F]{4}: ((?:[0-9A-F]{2} )+)", output, flags=re.MULTILINE)))
    assert len(resource_bytes) == 6 * 46 + 2, "unexpected resource count or type"
    assert resource_bytes[-2:] == b"\x79\x00", "missing EndTag"
    expected = [(0x1CB0000000, 0x200000), (0x488000000, 0x20000),
                (0x488028000, 0x4000), (0x48F010000, 0x1000),
                (0x39A000300, 4), (0xB80000000, 0x600000)]
    resources = []
    for index, (base, length) in enumerate(expected):
        desc = resource_bytes[index * 46:(index + 1) * 46]
        # QWord Memory, exclusive ResourceConsumer, fixed min/max, non-cacheable RW.
        assert desc[:6] == b"\x8a\x2b\x00\x00\x0d\x01", desc.hex()
        assert struct.unpack_from("<5Q", desc, 6) == (0, base, base + length - 1, 0, length)
        resources.append({"base": hex(base), "length": hex(length)})
    run(["git", "diff", "--binary", "HEAD"], "tracked.patch")
    submodules = run(["git", "submodule", "status", "--recursive"], "submodules.txt")
    receipt = {
        "schema": "aurora.mu.n1-offline-evidence.v1", "status": "passed",
        "hardware_touched": False, "host": platform.platform(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "sources": {str(p): sha256(out / "sources" / p) for p in SOURCES},
        "commands": commands, "cases": cases, "acpi_resources": resources,
        "submodules": submodules.splitlines(),
        "artifacts": {str(p.relative_to(out)): sha256(p) for p in sorted(out.glob("*")) if p.is_file()},
        "limits": ["Host register model only; no MMIO was issued.",
                   "AML evaluation does not establish Windows PnP resource assignment.",
                   "Full Aura to Mu to Windows hardware qualification remains required."],
    }
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"status": receipt["status"], "cases": len(cases),
                      "receipt": str(out / "receipt.json"), "aml_sha256": sha256(out / "N1.aml")}))


if __name__ == "__main__":
    main()

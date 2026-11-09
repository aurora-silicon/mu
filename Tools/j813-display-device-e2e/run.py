#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline check of the J813 DCP SSDT: DISP.asl compiled by iasl, its _CRS
parsed independently and compared with the AppleDisplay contract, and the
real admission code run against fixture ADTs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
TABLES = ROOT / "Platform/MacBookAir2026Pkg/AcpiTables"
ASL = TABLES / "DISP.asl"
SOURCE = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AuraJ813DisplayDevice.c"

# AppleDisplayJ813Core.h role order; t8142-j813-display.dtsi (linux e50ba38).
EXPECTED = [
    ("DISP_0", 0x284000000, 0xB1C000),
    ("DISP_1", 0x286240000, 0x10000),
    ("DISP0_DART", 0x286300000, 0xC000),
    ("DCP_DART", 0x286340000, 0x20000),
    ("DISP_2", 0x2863AC000, 0x4000),
    ("DISP_3", 0x286800000, 0x800000),
    ("DISP_4", 0x382700000, 0x114000),
]
# Sub-windows that must stay inside a published window (Linux reg / pmgr).
INSIDE = [
    ("asc", 0x286E00000, 0x4000, "DISP_3"),
    ("mailbox", 0x286E08000, 0x4000, "DISP_3"),
    ("iop-vbar", 0x286850000, 0x4000, "DISP_3"),
    ("bw-scratch", 0x382720000, 0x8, "DISP_4"),
    ("bw-doorbell", 0x3827A4000, 0x4, "DISP_4"),
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def descriptors(table):
    """Independent parse of every QWord memory descriptor in the table."""
    memory, i = [], 36
    while i + 46 <= len(table):
        if table[i] == 0x8A and table[i + 1:i + 3] == b"\x2b\x00" and table[i + 3] == 0:
            # tag, length(2), type, general flags, type flags, then
            # granularity, minimum, maximum, translation, length (ACPI 6.4.3.5.1)
            _, mn, mx, _, ln = struct.unpack_from("<QQQQQ", table, i + 6)
            memory.append((mn, mx, ln))
            i += 46
        else:
            i += 1
    return memory


def crs_items(table):
    """Walk the _CRS resource template item by item (ACPI 6.4.1/6.4.3).
    Returns [(kind, bytes)]; kind is 'qword-memory', 'end' or 'other:<tag>'."""
    at = table.find(b"_CRS")
    assert at > 0 and table[at + 4] == 0x11, "_CRS is not a Buffer"
    i = at + 5
    lead = table[i]
    i += 1 + (lead >> 6)                  # PkgLength
    op = table[i]
    i += {0x0A: 2, 0x0B: 3, 0x0C: 5}[op]  # BufferSize integer
    items = []
    while True:
        tag = table[i]
        if tag & 0x80:
            length = 3 + struct.unpack_from("<H", table, i + 1)[0]
            kind = "qword-memory" if tag == 0x8A and table[i + 3] == 0 else f"other:{tag:#x}"
        else:
            length = 1 + (tag & 7)
            kind = "end" if (tag >> 3) == 0x0F else f"other:{tag:#x}"
        items.append((kind, table[i:i + length]))
        i += length
        if kind == "end":
            return items


def contract_from_header(path):
    text = Path(path).read_text()
    values = dict(re.findall(r"#define (ASI_J813_\w+) UINT64_C\((0x[0-9a-fA-F]+)\)", text))
    return [(name, int(values[f"ASI_J813_{name}_BASE"], 16),
             int(values[f"ASI_J813_{name}_LENGTH"], 16)) for name, _, _ in EXPECTED]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--contract", type=Path,
                        help="AuroraSilicon drivers/AppleDisplay/AppleDisplayJ813Core.h")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"result": "incomplete", "scope": "offline; not hardware qualification",
              "sources": {str(p.relative_to(ROOT)): sha(p) for p in
                          [ASL, SOURCE, HERE / "harness.c", HERE / "run.py"]},
              "commands": [], "checks": []}

    def run(command, name):
        command = list(map(str, command))
        report["commands"].append(command)
        with (out / name).open("wb") as log:
            return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                  timeout=300).returncode

    def check(ok, what):
        report["checks"].append({"ok": bool(ok), "what": what})
        assert ok, what

    try:
        check(run(["iasl", "-p", out / "DISP", ASL], "iasl-disp.log") == 0, "DISP.asl compiles")
        table = (out / "DISP.aml").read_bytes()
        report["aml_sha256"] = hashlib.sha256(table).hexdigest()
        check(table[:4] == b"SSDT" and table[16:24] == b"AURA818D", "SSDT AURA818D")
        check(b"AURA8180" in table, "_HID AURA8180")
        memory = descriptors(table)
        items = crs_items(table)
        report["crs_items"] = [kind for kind, _ in items]
        check([kind for kind, _ in items] == ["qword-memory"] * len(EXPECTED) + ["end"],
              "_CRS holds only the QWord memory windows (no interrupt)")
        check(len(memory) == len(EXPECTED), f"{len(memory)} memory windows")
        for (mn, mx, ln), (name, base, size) in zip(memory, EXPECTED):
            check((mn, mx, ln) == (base, base + size - 1, size), f"{name} {base:#x}+{size:#x}")
        spans = sorted((b, b + s, n) for n, b, s in EXPECTED)
        check(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), "windows do not overlap")
        by_name = {n: (b, s) for n, b, s in EXPECTED}
        for sub, base, size, parent in INSIDE:
            pb, ps = by_name[parent]
            check(pb <= base and base + size <= pb + ps, f"{sub} inside {parent}")
        if args.contract:
            check(contract_from_header(args.contract) == EXPECTED,
                  "matches AppleDisplayJ813Core.h")
            report["contract_sha256"] = sha(args.contract)
        # No overlap with the GPU device's fixed windows in the same image.
        check(run(["iasl", "-p", out / "GPU", TABLES / "GPU.asl"], "iasl-gpu.log") == 0,
              "GPU.asl compiles")
        gpu = descriptors((out / "GPU.aml").read_bytes())
        fixed = [(mn, mx) for mn, mx, _ in gpu if mn < (1 << 60)]
        check(all(mx < b or e <= mn for mn, mx in fixed for b, e, _ in spans),
              "no overlap with AURA8170 fixed windows")

        cc = os.environ.get("CC", "clang")
        for pcd in (1, 0):
            binary = out / f"harness-pcd{pcd}"
            check(run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                       f"-DHOST_PCD_PcdAppleJ813DisplayDevice={pcd}", "-I", HERE / "shim",
                       HERE / "harness.c", SOURCE, "-o", binary], f"build-pcd{pcd}.log") == 0,
                  f"harness builds with PCD {pcd}")
            check(run([binary, out / "DISP.aml"], f"run-pcd{pcd}.log") == 0,
                  f"admission cases pass with PCD {pcd}")
            log = (out / f"run-pcd{pcd}.log").read_text()
            report[f"admission_checks_pcd{pcd}"] = int(log.strip().splitlines()[-1].split()[-1])
        report["resources"] = [{"role": n, "base": hex(b), "size": hex(s)} for n, b, s in EXPECTED]
        report["result"] = "passed"
    except Exception as error:
        report["result"] = "failed"
        report["error"] = repr(error)
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("result",) if k in report}))
    return 0 if report["result"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

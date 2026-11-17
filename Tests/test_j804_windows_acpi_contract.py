#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Contract for the J804 windows-profile ACPI tables (MacBookPro2026Pkg).

Checks the tables the last build produced (Build/MacBookPro2026-AARCH64,
made by Tools/build-j804-windows.sh) against PLAN 0.6 and the values shared
with the EL2 carrier in Platform/MacBookPro2026Pkg/Include/J804Windows.h:

  * MADT: twelve enabled GICCs, the boot CPU (MPIDR 0x10100) first, the J804
    CPU map, UIDs 0..11, one GICv3 distributor and one redistributor range
    at the carrier's bases, no ITS and no MSI frame;
  * GTDT: physical/virtual timer INTIDs 17/18 as the carrier injects them,
    no secure, EL2 or platform timers;
  * FADT: hardware-reduced, PSCI over SMC, PreferredPmProfile MOBILE, no
    low-power S0 idle;
  * DSDT: exactly the expected device set (three ACPI0010 clusters with
    twelve ACPI0007 CPUs, SMC0 NTAS2032, ANS0 NTAS2004), ANS0 disabled by
    default with the Linux-proven mailbox page and published GSIV 996, and
    none of the forbidden blocks (USB controllers, i2c1/i2c3, the J873 ANS
    mailbox page, the QSPI/NVRAM controller).

Skips when no build is present. Host-only.
"""

from __future__ import annotations

import re
import struct
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "Build" / "MacBookPro2026-AARCH64" / "DEBUG_CLANGPDB" / "AARCH64" / "MacBookPro2026Pkg"
HEADER = REPO / "Platform" / "MacBookPro2026Pkg" / "Include" / "J804Windows.h"

CPU_MPIDR = {0: 0x0, 1: 0x1, 2: 0x2, 3: 0x3, 4: 0x4, 5: 0x5,
             6: 0x10100, 7: 0x10101,
             8: 0x40102, 9: 0x40103, 10: 0x40104, 11: 0x40105}

# Physical blocks that must never appear in a J804 windows DSDT.
FORBIDDEN_QWORDS = {
    0x40A430000: "usb-drd1 xHCI (USB prerequisite not met)",
    0x412430000: "usb-drd3 xHCI (USB prerequisite not met)",
    0x402430000: "usb-drd0 (aura console/proxy port)",
    0x331014000: "i2c1",
    0x33101C000: "i2c3",
    0x316010000: "J873 ANS mailbox page (J804 uses 0x316020000)",
    0x331118000: "QSPI NVRAM region",
}


def header_defines() -> dict[str, int]:
    values = {}
    for name, value in re.findall(r"#define\s+(J804_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", HEADER.read_text()):
        values[name] = int(value, 0)
    return values


def find_table(signature: str) -> bytes:
    for path in sorted(BUILD.rglob("*")):
        if path.suffix in (".acpi", ".aml") and path.is_file():
            blob = path.read_bytes()
            if blob[:4] == signature.encode():
                return blob
    raise FileNotFoundError(signature)


def length_ok(blob: bytes) -> bool:
    length = struct.unpack_from("<I", blob, 4)[0]
    return length == len(blob)


# ---- minimal AML walk: devices, their _HID/_UID/_STA and _CRS buffers ----

def pkg_length(aml: bytes, pos: int) -> tuple[int, int]:
    lead = aml[pos]
    count = lead >> 6
    if count == 0:
        return lead & 0x3F, 1
    value = lead & 0x0F
    for i in range(count):
        value |= aml[pos + 1 + i] << (4 + 8 * i)
    return value, count + 1


def integer(aml: bytes, pos: int) -> tuple[int, int]:
    op = aml[pos]
    if op == 0x00:
        return 0, 1
    if op == 0x01:
        return 1, 1
    if op == 0xFF:
        return 0xFFFFFFFFFFFFFFFF, 1
    size = {0x0A: 1, 0x0B: 2, 0x0C: 4, 0x0E: 8}[op]
    return int.from_bytes(aml[pos + 1:pos + 1 + size], "little"), size + 1


def devices(aml: bytes) -> list[dict]:
    found = []
    pos = 36
    for match in re.finditer(rb"\x5b\x82", aml):
        start = match.start()
        if start < pos:
            continue
        length, size = pkg_length(aml, start + 2)
        name_at = start + 2 + size
        name = aml[name_at:name_at + 4].decode("ascii", "replace")
        end = start + 2 + length
        found.append(dict(name=name, start=start, end=end))
    # Attribute each Name() to the innermost device that contains it.
    for dev in found:
        dev["inner"] = [d for d in found if d is not dev and dev["start"] < d["start"] < dev["end"]]
    result = []
    for dev in found:
        props = {}
        for match in re.finditer(rb"\x08(_HID|_UID|_STA|_CRS)", aml[dev["start"]:dev["end"]]):
            at = dev["start"] + match.start()
            if any(inner["start"] < at < inner["end"] for inner in dev["inner"]):
                continue
            key = match.group(1).decode()
            value_at = at + 5
            op = aml[value_at]
            if op == 0x0D:  # string
                end = aml.index(b"\x00", value_at + 1)
                props[key] = aml[value_at + 1:end].decode()
            elif op == 0x11:  # buffer
                length, size = pkg_length(aml, value_at + 1)
                count, isize = integer(aml, value_at + 1 + size)
                data_at = value_at + 1 + size + isize
                props[key] = aml[data_at:data_at + count]
            else:
                props[key] = integer(aml, value_at)[0]
        result.append(dict(name=dev["name"], **props))
    return result


def resources(crs: bytes) -> list[tuple]:
    out = []
    pos = 0
    while pos < len(crs):
        tag = crs[pos]
        if tag & 0x80:
            length = struct.unpack_from("<H", crs, pos + 1)[0]
            body = crs[pos + 3:pos + 3 + length]
            if tag == 0x8A:  # QWord address space
                _, _, _, gran, low, high, trans, size = struct.unpack_from("<BBBQQQQQ", body)
                out.append(("qword", low, high, size))
            elif tag == 0x89:  # extended interrupt
                flags, count = body[0], body[1]
                out.append(("irq", flags, list(struct.unpack_from(f"<{count}I", body, 2))))
            else:
                out.append(("large", tag))
            pos += 3 + length
        else:
            if (tag >> 3) == 0x0F:  # end tag
                break
            pos += 1 + (tag & 7)
    return out


@unittest.skipUnless(BUILD.is_dir(), "no MacBookPro2026 build; run Tools/build-j804-windows.sh")
class J804WindowsAcpiContract(unittest.TestCase):
    def setUp(self) -> None:
        self.h = header_defines()

    def test_madt(self) -> None:
        madt = find_table("APIC")
        self.assertTrue(length_ok(madt))
        pos = 44
        gicc, gicd, gicr, other = [], [], [], []
        while pos < len(madt):
            kind, length = madt[pos], madt[pos + 1]
            entry = madt[pos:pos + length]
            if kind == 0x0B:
                uid = struct.unpack_from("<I", entry, 8)[0]
                flags = struct.unpack_from("<I", entry, 12)[0]
                pmu = struct.unpack_from("<I", entry, 20)[0]
                mpidr = struct.unpack_from("<Q", entry, 68)[0]
                eff = entry[76]
                gicc.append((uid, flags, pmu, mpidr, eff))
            elif kind == 0x0C:
                gicd.append((struct.unpack_from("<Q", entry, 8)[0], entry[20]))
            elif kind == 0x0E:
                gicr.append(struct.unpack_from("<QI", entry, 4))
            else:
                other.append(kind)
            pos += length
        self.assertEqual(len(gicc), self.h["J804_CPU_COUNT"])
        self.assertEqual(gicc[0][3], self.h["J804_BOOT_CPU_MPIDR"], "boot CPU must be first")
        self.assertEqual(gicc[0][0], 6)
        self.assertEqual({g[0]: g[3] for g in gicc}, CPU_MPIDR)
        self.assertTrue(all(g[1] & 1 for g in gicc), "every GICC enabled")
        self.assertTrue(all(g[2] == 0 for g in gicc), "no PMU interrupt described")
        self.assertEqual(gicd, [(self.h["J804_GICD_BASE"], 3)])
        self.assertEqual(gicr, [(self.h["J804_GICR_BASE"],
                                 self.h["J804_CPU_COUNT"] * self.h["J804_GICR_FRAME_SIZE"])])
        self.assertNotIn(0x0F, other, "no ITS")
        self.assertNotIn(0x0D, other, "no MSI frame")
        self.assertEqual(other, [])

    def test_gtdt(self) -> None:
        gtdt = find_table("GTDT")
        self.assertTrue(length_ok(gtdt))
        (cnt_ctl, _res, sec, sec_f, ns, ns_f, virt, virt_f, el2, el2_f,
         cnt_read, count, offset) = struct.unpack_from("<QIIIIIIIIIQII", gtdt, 36)
        self.assertEqual((ns, virt), (self.h["J804_TIMER_P_INTID"], self.h["J804_TIMER_V_INTID"]))
        self.assertEqual((ns_f, virt_f), (0, 0), "level triggered, active high")
        self.assertEqual((sec, el2, count), (0, 0, 0))
        self.assertEqual(cnt_ctl, 0xFFFFFFFFFFFFFFFF)
        self.assertEqual(cnt_read, 0xFFFFFFFFFFFFFFFF)

    def test_fadt(self) -> None:
        fadt = find_table("FACP")
        self.assertTrue(length_ok(fadt))
        self.assertEqual(fadt[45], 2, "PreferredPmProfile MOBILE")
        flags = struct.unpack_from("<I", fadt, 112)[0]
        self.assertTrue(flags & (1 << 20), "HW_REDUCED_ACPI")
        self.assertFalse(flags & (1 << 21), "no LOW_POWER_S0_IDLE_CAPABLE")
        arm = struct.unpack_from("<H", fadt, 129)[0]
        self.assertEqual(arm, 1, "PSCI compliant, SMC conduit")

    def test_dsdt(self) -> None:
        dsdt = find_table("DSDT")
        self.assertTrue(length_ok(dsdt))
        devs = {d["name"]: d for d in devices(dsdt)}
        cpus = {f"CPU{i}" if i < 10 else f"CP{i}" for i in range(12)}
        self.assertEqual(set(devs), {"CLU0", "CLU1", "CLU2", "SMC0", "ANS0"} | cpus,
                         "unexpected or missing DSDT devices")
        for name in ("CLU0", "CLU1", "CLU2"):
            self.assertEqual(devs[name]["_HID"], "ACPI0010")
        self.assertEqual(sorted(devs[n]["_UID"] for n in cpus), list(range(12)))
        self.assertTrue(all(devs[n]["_HID"] == "ACPI0007" for n in cpus))

        smc = devs["SMC0"]
        self.assertEqual(smc["_HID"], "NTAS2032")
        self.assertEqual([r[1:] for r in resources(smc["_CRS"])],
                         [(0x30C600000, 0x30C673FFF, 0x74000),
                          (0x30DE00000, 0x30DF1FFFF, 0x120000),
                          (0x30C710000, 0x30C713FFF, 0x4000)])

        ans = devs["ANS0"]
        self.assertEqual(ans["_HID"], "NTAS2004")
        self.assertEqual(ans["_STA"], 0, "ANS0 disabled in the default build")
        crs = resources(ans["_CRS"])
        qwords = [r[1:] for r in crs if r[0] == "qword"]
        self.assertIn((0x316020000, 0x316023FFF, 0x4000), qwords, "Linux-proven mailbox page")
        self.assertEqual(qwords[0], (0x321600000, 0x321673FFF, 0x74000))
        self.assertEqual([r[2] for r in crs if r[0] == "irq"], [[self.h["J804_GSIV_ANS"]]])

        for name, dev in devs.items():
            for res in resources(dev.get("_CRS", b"")):
                if res[0] == "qword":
                    for base, why in FORBIDDEN_QWORDS.items():
                        self.assertFalse(res[1] <= base <= res[2], f"{name} covers {why}")
                if res[0] == "irq":
                    for gsiv in res[2]:
                        self.assertLess(gsiv, 1020, f"{name} names GSIV {gsiv} beyond the GIC limit")

    def test_published_gsivs_are_legal(self) -> None:
        for key in ("J804_GSIV_XHC1", "J804_GSIV_ANS", "J804_GSIV_XHC2"):
            self.assertTrue(32 <= self.h[key] < 1020, key)


if __name__ == "__main__":
    unittest.main()

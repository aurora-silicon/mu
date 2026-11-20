#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Contract for the J804 windows-profile ACPI tables (MacBookPro2026Pkg).

Checks the tables the last build produced (Build/MacBookPro2026-AARCH64,
made by Tools/build-j804-windows.sh) against PLAN 0.6 and the values shared
with the EL2 carrier in Platform/MacBookPro2026Pkg/Include/J804Windows.h:

  * MADT: twelve enabled GICCs, the boot CPU (MPIDR 0x10100) first, the J804
    CPU map, UIDs 0..11, efficiency classes E 0 < M 1 < P 2 per MPIDR (ACPI:
    lower = more power efficient), one GICv3 distributor and one
    redistributor range at the carrier's bases, no ITS and no MSI frame;
  * GTDT: physical/virtual timer INTIDs 17/18 as the carrier injects them,
    no secure, EL2 or platform timers;
  * FADT: hardware-reduced, PSCI over SMC, PreferredPmProfile MOBILE, no
    low-power S0 idle;
  * DSDT: exactly the expected device set (three ACPI0010 clusters with
    twelve ACPI0007 CPUs, SMC0 NTAS2032 with _STA 0 because the T8152
    carrier denies the SMC pages, no ANS0; COM0 only in a J804_WINDOWS_VUART
    build, AHC0 only in a J804_WINDOWS_AHCI build), and none of the
    forbidden blocks (USB controllers: XHC1 is never in the DSDT, i2c1/i2c3,
    the J873 ANS mailbox page,
    the QSPI/NVRAM controller, the PMGR SPI/QSPI power states, the DCP
    mailbox). ANS0 checks run only on a J804_WINDOWS_ANS build.
  * AHC0 (D1, J804_WINDOWS_AHCI build): NTAS2010, _CLS 01/06/01, _CCA 1,
    _STA 0xF, one QWord memory range = the ABAR page of J804Windows.h and one
    level, active-high interrupt = J804_GSIV_AHCI.
  * XHC1 (D4, J804_WINDOWS_USB build): the only platform SSDT, OEM table ID
    J804XHC1, declaring just \_SB.XHC1: PNP0D10, _UID 1, _CCA 1, _STA 0xF,
    one QWord memory range = the two xHCI pages of usb-drd1 and one level,
    active-high interrupt = J804_GSIV_XHC1, no power or wake methods. A
    non-USB build has no J804XHC1 SSDT.
  * GPU0 (G0, J804_WINDOWS_GPU build): the template SSDT, OEM table ID
    J804GPU0, declaring just \_SB.GPU0: the project _HID J804_GPU0_HID (not
    an NTAS ID), _UID 0, _CCA 1, _STA 0xF, four QWord memory ranges whose
    values are the J804GpuHandoff.h patch sentinels (sgx window, GFX
    post-office page, pool, handoff page), no interrupt, no power or wake
    methods, and the handoff IPA sentinel in _DSD. The template is also
    patched by the host test (Tools/HostTests/J804GpuHandoff) with a
    synthetic handoff and checked again. A non-GPU build has no J804GPU0
    SSDT; no DSDT ever declares GPU0.
  * INP0 (I0, J804_WINDOWS_INPUT build): the platform SSDT with OEM table
    ID J804INP0, declaring just \_SB.INP0: NTAS2040 (J804_INPUT_HID, not
    AuroraSilicon's NTAS0050), _UID 0, _CCA 1, _STA 0xF, one QWord memory
    range = the carrier's 16 KiB input page and one level, active-high
    interrupt = J804_GSIV_INPUT, no power or wake methods and no _DSD. A
    non-input build has no J804INP0 SSDT; no DSDT ever declares INP0.
  * GPU0 with the scanout service (G6-S, J804_WINDOWS_SCANOUT build): a
    second J804GPU0 template (J804Gpu0ScnSsdt), the same GPU0 plus resource 5
    = the carrier's 16 KiB scanout page and resource 6 = one level,
    active-high interrupt J804_GSIV_SCANOUT, and the scanout values in _DSD.
    J804GpuAcpiDxe installs one of the two; a non-scanout build carries only
    the plain template and 0xFF at the FD marker offset 0x150.

The build flags come from the stamp Tools/build-j804-windows.sh writes in
the build tree (all FALSE when it is missing). Skips when no build is
present. The D1, D4 and I0 contract checks between J804Windows.h, the FDF marker
blocks and the DSC need no build; the D4 and I0 SSDTs are also compiled on
their own with cc -E and iasl when both are installed. Host-only.
"""

from __future__ import annotations

import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "Build" / "MacBookPro2026-AARCH64" / "DEBUG_CLANGPDB" / "AARCH64" / "MacBookPro2026Pkg"
HEADER = REPO / "Platform" / "MacBookPro2026Pkg" / "Include" / "J804Windows.h"
FAMILY_DSC = REPO / "Silicon" / "Apple" / "T8152FamilyPkg" / "T8152FamilyPkg.dsc.inc"
J804M1_H = REPO / "Silicon" / "Apple" / "AppleSiliconPkg" / "Include" / "J804M1.h"
FLAGS_STAMP = REPO / "Build" / "MacBookPro2026-AARCH64" / ".wm6-j804-windows-flags"
FDF = REPO / "Platform" / "MacBookPro2026Pkg" / "MacBookPro2026.fdf"
DSC = REPO / "Platform" / "MacBookPro2026Pkg" / "MacBookPro2026.dsc"
ACPI_SRC = REPO / "Platform" / "MacBookPro2026Pkg" / "AcpiTables"
SSDT_XHC1 = ACPI_SRC / "SsdtXhc1.asl"
SSDT_GPU0 = ACPI_SRC / "SsdtGpu0.asl"
SSDT_INP0 = ACPI_SRC / "SsdtInp0.asl"
SSDT_GPU0_SCN = ACPI_SRC / "SsdtGpu0Scn.asl"
SCANOUT_HOST_TEST = REPO / "Tools" / "HostTests" / "J804ScanoutHandshake"
GPU_HANDOFF_H = REPO / "Silicon" / "Apple" / "AppleSiliconPkg" / "Include" / "J804GpuHandoff.h"
GPU_HOST_TEST = REPO / "Tools" / "HostTests" / "J804GpuHandoff"

CPU_MPIDR = {0: 0x0, 1: 0x1, 2: 0x2, 3: 0x3, 4: 0x4, 5: 0x5,
             6: 0x10100, 7: 0x10101,
             8: 0x40102, 9: 0x40103, 10: 0x40104, 11: 0x40105}

# ACPI 6.5 MADT GICC Processor Power Efficiency Class: lower numbers are more
# power efficient (Windows EfficiencyClass: higher = faster). E < M < P.
EFF_E, EFF_M, EFF_P = 0, 1, 2
CPU_EFFICIENCY = {**{m: EFF_E for m in range(6)},
                  0x10100: EFF_P, 0x10101: EFF_P,
                  **{m: EFF_M for m in range(0x40102, 0x40106)}}

# Physical blocks that must never appear in a J804 windows DSDT.
FORBIDDEN_QWORDS = {
    0x40A430000: "usb-drd1 xHCI (only in the gated J804XHC1 SSDT)",
    0xF50000000: "carrier input page (only in the gated J804INP0 SSDT)",
    0x412430000: "usb-drd3 xHCI (no build describes it)",
    0x402430000: "usb-drd0 (aura console/proxy port)",
    0x331014000: "i2c1",
    0x33101C000: "i2c3",
    0x316010000: "J873 ANS mailbox page (J804 uses 0x316020000)",
    0x331118000: "QSPI NVRAM region",
    0x300700D40: "PMGR ps_spi_p (SPI power domain)",
    0x300701140: "PMGR ps_qspi (QSPI NOR power domain)",
    0x316020500: "DCP mailbox (shared post-office page)",
}

# Methods XHC1 must not have: power states, power resources, wake (D4).
XHC1_FORBIDDEN_METHODS = (b"_PS0", b"_PS1", b"_PS2", b"_PS3", b"_PSC", b"_PR0", b"_PR2",
                          b"_PR3", b"_PRW", b"_DSW", b"_S0W", b"_S3W", b"_S4W", b"_S0D",
                          b"_S3D", b"_S4D", b"_RMV", b"_EJ0")


# GPU0 (G0): the J804GpuHandoff.h patch sentinels, in _CRS order (_MIN,
# _MAX, _LEN per resource), and the _DSD handoff IPA sentinel.
def gpu_sentinel(k: int) -> tuple[int, int, int]:
    low = 0x5AA5000000000000 | (k << 40)
    length = 0x0000005A00000000 | (k << 28)
    return low, low + length - 1, length


GPU_SENTINEL_IPA = 0x5AA5A55A5AA5A55A
# The synthetic handoff Tools/HostTests/J804GpuHandoff patches the template with.
GPU_SYNTHETIC = [(0x440000000, 0x4000000), (0x360080000, 0x4000), (0x900000000, 0x10000000),
                 (0x806000000, 0x4000)]


def build_flags() -> dict[str, bool]:
    flags = {"J804_WINDOWS_ANS": False, "J804_WINDOWS_VUART": False, "J804_WINDOWS_AHCI": False,
             "J804_WINDOWS_USB": False, "J804_WINDOWS_GPU": False, "J804_WINDOWS_INPUT": False,
             "J804_WINDOWS_SCANOUT": False}
    if FLAGS_STAMP.is_file():
        for item in FLAGS_STAMP.read_text().split():
            name, _, value = item.partition("=")
            flags[name] = value == "TRUE"
    return flags


def header_strings() -> dict[str, str]:
    return dict(re.findall(r'#define\s+(J804_\w+)\s+"([^"]*)"', HEADER.read_text()))


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


def find_tables(signature: str) -> list[bytes]:
    """Every distinct table with this signature in the platform build tree."""
    found = []
    for path in sorted(BUILD.rglob("*")):
        if path.suffix in (".acpi", ".aml") and path.is_file():
            blob = path.read_bytes()
            if blob[:4] == signature.encode() and blob not in found:
                found.append(blob)
    return found


def checksum_ok(blob: bytes) -> bool:
    return sum(blob) % 256 == 0


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
        for match in re.finditer(rb"\x08(_HID|_UID|_STA|_CRS|_CCA|_CLS)", aml[dev["start"]:dev["end"]]):
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
            elif op == 0x12:  # package of integers
                length, size = pkg_length(aml, value_at + 1)
                count = aml[value_at + 1 + size]
                at = value_at + 2 + size
                items = []
                for _ in range(count):
                    item, isize = integer(aml, at)
                    items.append(item)
                    at += isize
                props[key] = items
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
                self.assertEqual(entry[77:80], b"\0\0\0", "reserved bytes after the class")
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
        self.assertEqual({g[3]: g[4] for g in gicc}, CPU_EFFICIENCY,
                         "efficiency class per MPIDR (E 0 < M 1 < P 2)")
        boot = [g for g in gicc if g[3] == self.h["J804_BOOT_CPU_MPIDR"]]
        e_core = [g for g in gicc if g[3] == 0x0]
        self.assertGreater(boot[0][4], e_core[0][4], "P core must not be more efficient than E")
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
        flags = build_flags()
        expected = {"CLU0", "CLU1", "CLU2", "SMC0"} | cpus
        expected |= {"ANS0"} if flags["J804_WINDOWS_ANS"] else set()
        expected |= {"COM0"} if flags["J804_WINDOWS_VUART"] else set()
        expected |= {"AHC0"} if flags["J804_WINDOWS_AHCI"] else set()
        self.assertEqual(set(devs), expected,
                         f"unexpected or missing DSDT devices for build flags {flags}")
        for name in ("CLU0", "CLU1", "CLU2"):
            self.assertEqual(devs[name]["_HID"], "ACPI0010")
        self.assertEqual(sorted(devs[n]["_UID"] for n in cpus), list(range(12)))
        self.assertTrue(all(devs[n]["_HID"] == "ACPI0007" for n in cpus))

        smc = devs["SMC0"]
        self.assertEqual(smc["_HID"], "NTAS2032")
        self.assertEqual(smc["_STA"], 0, "SMC0 disabled: the T8152 carrier denies the SMC pages")
        self.assertEqual([r[1:] for r in resources(smc["_CRS"])],
                         [(0x30C600000, 0x30C673FFF, 0x74000),
                          (0x30DE00000, 0x30DF1FFFF, 0x120000),
                          (0x30C710000, 0x30C713FFF, 0x4000)])

        self.assertEqual(self.h["J804_ANS_PS_REG"] & ~0x3FFF, self.h["J804_ANS_PMGR_PAGE"])
        self.assertEqual(self.h["J804_ANS_MBOX_WINDOW"] & ~0x3FFF, self.h["J804_ANS_MBOX_PAGE"])
        self.assertEqual(self.h["J804_ANS_MBOX_WINDOW_SIZE"], 0x80)
        for name in ("J804_PMGR_PS_SPI_P", "J804_PMGR_PS_QSPI", "J804_DCP_MBOX"):
            self.assertIn(self.h[name], FORBIDDEN_QWORDS, name)

        for name, dev in devs.items():
            for res in resources(dev.get("_CRS", b"")):
                if res[0] == "qword":
                    for base, why in FORBIDDEN_QWORDS.items():
                        self.assertFalse(res[1] <= base <= res[2], f"{name} covers {why}")
                if res[0] == "irq":
                    for gsiv in res[2]:
                        self.assertLess(gsiv, 1020, f"{name} names GSIV {gsiv} beyond the GIC limit")

    def test_ahc0(self) -> None:
        if not build_flags()["J804_WINDOWS_AHCI"]:
            self.skipTest("not a J804_WINDOWS_AHCI build")
        devs = {d["name"]: d for d in devices(find_table("DSDT"))}
        ahc0 = devs["AHC0"]
        self.assertEqual(ahc0["_HID"], "NTAS2010")
        self.assertEqual(ahc0["_UID"], 0)
        self.assertEqual(ahc0["_CLS"], [0x01, 0x06, 0x01], "mass storage / SATA / AHCI 1.0")
        self.assertEqual(ahc0["_CCA"], 1, "coherent DMA")
        self.assertEqual(ahc0["_STA"], 0x0F)
        base, size = self.h["J804_AHCI_BASE"], self.h["J804_AHCI_SIZE"]
        res = resources(ahc0["_CRS"])
        self.assertEqual(res, [("qword", base, base + size - 1, size),
                               ("irq", 0x01, [self.h["J804_GSIV_AHCI"]])],
                         "ABAR page, then one consumer level active-high exclusive SPI")

    def test_xhc1(self) -> None:
        ssdts = [t for t in find_tables("SSDT") if t[16:24] == b"J804XHC1"]
        if not build_flags()["J804_WINDOWS_USB"]:
            self.assertEqual(ssdts, [], "a non-USB build carries no J804XHC1 SSDT")
            return
        self.assertEqual(len(ssdts), 1, "exactly one J804XHC1 SSDT in a USB build")
        check_xhc1_ssdt(self, ssdts[0], self.h)

    def test_gpu0(self) -> None:
        ssdts = [t for t in find_tables("SSDT") if t[16:24] == b"J804GPU0"]
        self.assertNotIn("GPU0", {d["name"] for d in devices(find_table("DSDT"))},
                         "GPU0 is never in the DSDT")
        if not build_flags()["J804_WINDOWS_GPU"]:
            self.assertEqual(ssdts, [], "a non-GPU build carries no J804GPU0 SSDT")
            return
        sentinels = [gpu_sentinel(k) for k in range(1, 5)]
        if not build_flags()["J804_WINDOWS_SCANOUT"]:
            self.assertEqual(len(ssdts), 1, "exactly one J804GPU0 SSDT in a GPU build")
            check_gpu0_ssdt(self, ssdts[0], sentinels, GPU_SENTINEL_IPA)
            return
        self.assertEqual(len(ssdts), 2, "the plain and the scanout J804GPU0 SSDT")
        scn = [t for t in ssdts if struct.pack("<Q", 0xF60000000) in t]
        plain = [t for t in ssdts if t not in scn]
        self.assertEqual((len(scn), len(plain)), (1, 1))
        check_gpu0_ssdt(self, plain[0], sentinels, GPU_SENTINEL_IPA)
        check_gpu0_ssdt(self, scn[0], sentinels, GPU_SENTINEL_IPA, scanout=self.h)

    def test_inp0(self) -> None:
        ssdts = [t for t in find_tables("SSDT") if t[16:24] == b"J804INP0"]
        self.assertNotIn("INP0", {d["name"] for d in devices(find_table("DSDT"))},
                         "INP0 is never in the DSDT")
        if not build_flags()["J804_WINDOWS_INPUT"]:
            self.assertEqual(ssdts, [], "a non-input build carries no J804INP0 SSDT")
            return
        self.assertEqual(len(ssdts), 1, "exactly one J804INP0 SSDT in an input build")
        check_inp0_ssdt(self, ssdts[0], self.h)

    def test_platform_ssdts(self) -> None:
        flags = build_flags()
        ids = sorted(t[16:24] for t in find_tables("SSDT"))
        want = sorted(([b"J804XHC1"] if flags["J804_WINDOWS_USB"] else [])
                      + ([b"J804GPU0"] if flags["J804_WINDOWS_GPU"] else [])
                      + ([b"J804GPU0"] if flags["J804_WINDOWS_SCANOUT"] else [])
                      + ([b"J804INP0"] if flags["J804_WINDOWS_INPUT"] else []))
        self.assertEqual(ids, want, f"platform SSDTs for build flags {flags}")

    def test_published_gsivs_are_legal(self) -> None:
        for key in ("J804_GSIV_XHC1", "J804_GSIV_ANS", "J804_GSIV_XHC2", "J804_GSIV_INPUT"):
            self.assertTrue(32 <= self.h[key] < 1020, key)


def check_xhc1_ssdt(case: unittest.TestCase, ssdt: bytes, h: dict[str, int]) -> None:
    """The D4 SSDT: only \\_SB.XHC1, with the contract's resources."""
    case.assertTrue(length_ok(ssdt))
    case.assertTrue(checksum_ok(ssdt), "SSDT checksum")
    case.assertEqual(ssdt[10:16], b"AURORA")
    case.assertEqual(ssdt[16:24], b"J804XHC1")
    devs = {d["name"]: d for d in devices(ssdt)}
    case.assertEqual(set(devs), {"XHC1"}, "the SSDT declares XHC1 and nothing else")
    xhc1 = devs["XHC1"]
    case.assertEqual(xhc1["_HID"], "PNP0D10")
    case.assertEqual(xhc1["_UID"], 1)
    case.assertEqual(xhc1["_CCA"], 1, "coherent DMA through the carrier's DART identity map")
    case.assertEqual(xhc1["_STA"], 0x0F)
    base, size = h["J804_XHC1_BASE"], h["J804_XHC_SIZE"]
    case.assertEqual(resources(xhc1["_CRS"]),
                     [("qword", base, base + size - 1, size), ("irq", 0x01, [h["J804_GSIV_XHC1"]])],
                     "the two xHCI pages, then one consumer level active-high exclusive SPI")
    for name in XHC1_FORBIDDEN_METHODS:
        case.assertNotIn(name, ssdt, f"XHC1 must not have {name.decode()}")
    for forbidden in (0x402430000, 0x412430000, 0x40A43C100):
        case.assertFalse(base <= forbidden < base + size, hex(forbidden))


def check_inp0_ssdt(case: unittest.TestCase, ssdt: bytes, h: dict[str, int]) -> None:
    """The I0 SSDT: only \\_SB.INP0, with the contract's resources."""
    case.assertTrue(length_ok(ssdt))
    case.assertTrue(checksum_ok(ssdt), "SSDT checksum")
    case.assertEqual(ssdt[10:16], b"AURORA")
    case.assertEqual(ssdt[16:24], b"J804INP0")
    devs = {d["name"]: d for d in devices(ssdt)}
    case.assertEqual(set(devs), {"INP0"}, "the SSDT declares INP0 and nothing else")
    inp0 = devs["INP0"]
    case.assertEqual(inp0["_HID"], "NTAS2040")
    case.assertEqual(inp0["_HID"], header_strings()["J804_INPUT_HID"])
    case.assertEqual(inp0["_UID"], 0)
    case.assertEqual(inp0["_CCA"], 1)
    case.assertEqual(inp0["_STA"], 0x0F)
    base, size = h["J804_INPUT_BASE"], h["J804_INPUT_SIZE"]
    case.assertEqual(resources(inp0["_CRS"]),
                     [("qword", base, base + size - 1, size), ("irq", 0x01, [h["J804_GSIV_INPUT"]])],
                     "the input page, then one consumer level active-high exclusive SPI")
    for name in XHC1_FORBIDDEN_METHODS + (b"_DSD", b"_DMA", b"_PRS", b"_SRS", b"_DIS"):
        case.assertNotIn(name, ssdt, f"INP0 must not have {name.decode()}")


# Methods GPU0 must not have: the carrier owns the GPU's power (G0).
GPU0_FORBIDDEN_METHODS = XHC1_FORBIDDEN_METHODS + (b"_PRS", b"_SRS", b"_DIS")


def check_gpu0_ssdt(case: unittest.TestCase, ssdt: bytes, ranges: list[tuple[int, int, int]],
                    ipa: int, scanout: dict[str, int] | None = None) -> None:
    """The G0 SSDT, as a template (sentinels) or patched (ranges, ipa); with `scanout` (the
    header values) the G6-S variant: the same plus the scanout page and its SPI."""
    case.assertTrue(length_ok(ssdt))
    case.assertTrue(checksum_ok(ssdt), "SSDT checksum")
    case.assertEqual(ssdt[10:16], b"AURORA")
    case.assertEqual(ssdt[16:24], b"J804GPU0")
    devs = {d["name"]: d for d in devices(ssdt)}
    case.assertEqual(set(devs), {"GPU0"}, "the SSDT declares GPU0 and nothing else")
    gpu0 = devs["GPU0"]
    hid = header_strings()["J804_GPU0_HID"]
    case.assertEqual(gpu0["_HID"], hid)
    case.assertEqual(gpu0["_UID"], 0)
    case.assertEqual(gpu0["_CCA"], 1)
    case.assertEqual(gpu0["_STA"], 0x0F)
    extra = []
    if scanout is not None:
        base, size = scanout["J804_SCANOUT_BASE"], scanout["J804_SCANOUT_SIZE"]
        extra = [("qword", base, base + size - 1, size),
                 ("irq", 0x01, [scanout["J804_GSIV_SCANOUT"]])]
        for key in (b"aura,scanout-ipa\x00", b"aura,scanout-gsiv\x00", b"aura,scanout-size\x00",
                    b"aura,scanout-version\x00"):
            case.assertIn(key, ssdt)
    else:
        case.assertNotIn(b"aura,scanout", ssdt)
    case.assertEqual(resources(gpu0["_CRS"]), [("qword", *r) for r in ranges] + extra,
                     "sgx window, GFX post-office page, pool, handoff page; with the scanout "
                     "service its page and one level active-high SPI")
    crs = gpu0["_CRS"]
    # Type-specific flags: bit 0 read-write, bits 2:1 memory type (0 non-cacheable, 1 cacheable).
    case.assertEqual([crs[5 + 46 * i] for i in range(4)], [0x01, 0x01, 0x03, 0x02],
                     "sgx and mailbox non-cacheable RW, pool cacheable RW, handoff cacheable RO")
    case.assertEqual(ssdt.count(b"\x0e" + struct.pack("<Q", ipa)), 1, "_DSD handoff IPA")
    case.assertIn(b"aura,handoff-ipa\x00", ssdt)
    for name in GPU0_FORBIDDEN_METHODS:
        case.assertNotIn(name, ssdt, f"GPU0 must not have {name.decode()}")


class J804WindowsGpuContract(unittest.TestCase):
    """G0 GPU: J804Windows.h, J804GpuHandoff.h, the SSDT template, the DSC and the
    patcher (no build)."""

    def handoff_defines(self) -> dict[str, int]:
        text = GPU_HANDOFF_H.read_text()
        return {n: int(v.rstrip("ULul"), 0) for n, v in
                re.findall(r"#define\s+(J804_GPU_\w+)\s+(0x[0-9A-Fa-f]+[ULul]*|\d+[ULul]*)\b", text)}

    def test_header_values(self) -> None:
        hid = header_strings()["J804_GPU0_HID"]
        self.assertRegex(hid, r"^[A-Z]{4}[0-9A-F]{4}$", "an ACPI ID: four letters, four hex digits")
        self.assertFalse(hid.startswith("NTAS"), "never an AuroraSilicon NTAS ID (D5)")
        g = self.handoff_defines()
        text = GPU_HANDOFF_H.read_text()
        self.assertIn('#define J804_GPU_ADT_PROPERTY       "aura,windows-gpu-v1"', text)
        self.assertLess(len("aura,windows-gpu-v1"), 32, "ADT property keys are 32 bytes")
        # The carrier contract (G0-CARRIER-CONTRACT 7, gpu_t8152_admit.h).
        self.assertEqual((g["J804_GPU_ADT_PROPERTY_SIZE"], g["J804_GPU_HANDOFF_MAGIC"],
                          g["J804_GPU_HANDOFF_VERSION"], g["J804_GPU_HANDOFF_HEADER"],
                          g["J804_GPU_HANDOFF_SIZE"], g["J804_GPU_RANGE_STRIDE"],
                          g["J804_GPU_PERF_STRIDE"]),
                         (16, 0x31484741, 1, 0x100, 0x4000, 32, 16))
        self.assertEqual(struct.pack("<I", g["J804_GPU_HANDOFF_MAGIC"]), b"AGH1")
        self.assertEqual([g[f"J804_GPU_HO_{n}"] for n in
                          ("STATE", "RANGE_COUNT", "LINES", "MAILBOX_NUM", "POOL_BASE",
                           "HANDOFF_IPA", "DRAM_SIZE", "RECEIPT")],
                         [0x10, 0x1c, 0x34, 0x44, 0x50, 0x60, 0x78, 0x80])
        self.assertEqual([g[f"J804_GPU_C_{n}"] for n in
                          ("SGX", "SGX_HOLE", "ASC_CTRL", "ASC", "IOP_VBAR", "MAILBOX", "FW_RW",
                           "FW_RO", "FW_TEXT", "POOL", "HANDOFF", "FW_MMIO", "COUNT")],
                         list(range(1, 14)))
        # G2 (carrier gpu_t8152_admit.h GPU_HS_SGX_BOOT, GPU_HO_BOOT_CONTROL).
        self.assertEqual((g["J804_GPU_S_SGX_BOOT"], g["J804_GPU_S_KNOWN"],
                          g["J804_GPU_HO_BOOT_CONTROL"], g["J804_GPU_HO_RESERVED1"]),
                         (0x80, 0xff, 0xec, 0xf0))
        self.assertEqual((g["J804_GPU_POOL_ALIGN"], g["J804_GPU_CHIP_ID"], g["J804_GPU_BOARD_ID"]),
                         (32 << 20, 0x8152, 0x22))
        self.assertEqual(g["J804_GPU_SENTINEL_IPA"], GPU_SENTINEL_IPA)

    def test_template_sentinels(self) -> None:
        asl = re.sub(r"//.*", "", SSDT_GPU0.read_text())
        values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{16})\b", asl)]
        want = []
        for k in range(1, 5):
            low, high, length = gpu_sentinel(k)
            want += [low, high, length]
        self.assertEqual(values, want + [GPU_SENTINEL_IPA],
                         "the template's QWords are J804GpuHandoff.h's sentinels, in order")
        self.assertNotIn("Interrupt", asl, "GPU0 lists no interrupt in G0")
        for name in GPU0_FORBIDDEN_METHODS:
            self.assertNotIn(name.decode(), asl)
        self.assertIn("Name (_HID, J804_GPU0_HID)", asl)

    def test_sources(self) -> None:
        self.assertNotIn("GPU", (ACPI_SRC / "DSDT.asl").read_text().split("DefinitionBlock", 1)[1],
                         "GPU0 is never in the DSDT")
        inf = (ACPI_SRC / "J804Gpu0Ssdt.inf").read_text()
        guid = re.search(r"FILE_GUID = ([0-9A-F-]+)", inf).group(1)
        driver = (REPO / "Platform" / "MacBookPro2026Pkg" / "Drivers" / "J804GpuAcpiDxe"
                  / "J804GpuAcpiDxe.c").read_text()
        g = guid.split("-")
        c_guid = (f"0x{g[0].lower()}, 0x{g[1].lower()}, 0x{g[2].lower()}, {{ "
                  + ", ".join(f"0x{g[3][i:i + 2].lower()}" for i in (0, 2)) + ", "
                  + ", ".join(f"0x{g[4][i:i + 2].lower()}" for i in range(0, 12, 2)) + " }")
        self.assertIn(c_guid, driver, "J804GpuAcpiDxe installs the J804Gpu0Ssdt FILE_GUID")
        prepi = (REPO / "Silicon" / "Apple" / "AppleSiliconPkg" / "PrePi" / "AdtParser.c").read_text()
        self.assertIn("#define J804_GPU_HANDOFF_NO_SSDT\n#include <J804GpuHandoff.h>", prepi)
        main = (REPO / "Silicon" / "Apple" / "AppleSiliconPkg" / "PrePi" / "PrePi.c").read_text()
        self.assertIn("    MuCheckpoint(0x12); J804_GPU_RESERVE_POOL_HOB ();\n", main,
                      "the pool HOB call shares an existing line: PrePi.c keeps its __LINE__s")
        self.assertNotIn("J804_WINDOWS_GPU", main, "no GPU-only lines in PrePi.c")

    def test_dsc_defaults_off(self) -> None:
        dsc = DSC.read_text()
        self.assertIn("!ifndef J804_WINDOWS_GPU\n  DEFINE J804_WINDOWS_GPU = FALSE\n!endif", dsc)
        self.assertIn("!if $(J804_WINDOWS_GPU) == TRUE\n!if $(J804_WINDOWS_AHCI) == FALSE\n!error", dsc,
                      "a GPU build needs the AHCI profile")
        gpu_on, rest = dsc.split("!if $(J804_WINDOWS_GPU) == TRUE\n!if $(J804_WINDOWS_USB) == TRUE\n", 1)[1] \
            .split("\n!else\n!if $(J804_WINDOWS_USB) == TRUE\n", 1)
        self.assertEqual(gpu_on.count("-DJ804_WINDOWS_GPU=1"), 6)
        self.assertNotIn("GPU", rest.split("  GCC:*_*_AARCH64_ASLPP_FLAGS", 1)[0],
                         "without the flag the CC/PP flags are those of the AHCI/USB builds")
        components = dsc.split("[Components.common]", 1)[1]
        self.assertIn("!if $(J804_WINDOWS_GPU) == TRUE\n"
                      "  MacBookPro2026Pkg/AcpiTables/J804Gpu0Ssdt.inf\n"
                      "!if $(J804_WINDOWS_SCANOUT) == TRUE\n"
                      "  MacBookPro2026Pkg/AcpiTables/J804Gpu0ScnSsdt.inf\n"
                      "  MacBookPro2026Pkg/Drivers/J804GpuAcpiDxe/J804GpuAcpiDxe.inf {\n"
                      "    <BuildOptions>\n"
                      "      GCC:*_*_AARCH64_CC_FLAGS = -DJ804_GPU_SCANOUT=1\n"
                      "  }\n"
                      "!else\n"
                      "  MacBookPro2026Pkg/Drivers/J804GpuAcpiDxe/J804GpuAcpiDxe.inf\n"
                      "!endif\n!endif",
                      components)
        fdf = FDF.read_text()
        self.assertIn("!if $(J804_WINDOWS_GPU) == TRUE\n", fdf)
        self.assertNotIn("J804GPU", fdf.split("0x000008000|", 1)[0], "no GPU FD marker")

    @unittest.skipUnless(shutil.which("iasl") and shutil.which("cc") and shutil.which("make"),
                         "needs cc, make and iasl")
    def test_ssdt_compiles_and_patches(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            pre = Path(tmp) / "SsdtGpu0.asl"
            subprocess.run(["cc", "-E", "-P", "-x", "c", "-I", str(HEADER.parent),
                            str(SSDT_GPU0), "-o", str(pre)], check=True)
            subprocess.run(["iasl", "-vs", "-p", str(Path(tmp) / "SsdtGpu0"), str(pre)],
                           check=True, capture_output=True)
            template = Path(tmp) / "SsdtGpu0.aml"
            check_gpu0_ssdt(self, template.read_bytes(), [gpu_sentinel(k) for k in range(1, 5)],
                            GPU_SENTINEL_IPA)
            subprocess.run(["make", "-s", "-C", str(GPU_HOST_TEST)], check=True, capture_output=True)
            patched = Path(tmp) / "patched.aml"
            out = subprocess.run([str(GPU_HOST_TEST / "gpu_handoff_test"), str(template), str(patched)],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout)
            self.assertIn("J804_GPU_HANDOFF_TEST PASS", out.stdout)
            blob = patched.read_bytes()
            check_gpu0_ssdt(self, blob, [(b, b + n - 1, n) for b, n in GPU_SYNTHETIC],
                            GPU_SYNTHETIC[3][0])
            # The patched table still disassembles and recompiles.
            subprocess.run(["iasl", "-d", str(patched)], check=True, capture_output=True, cwd=tmp)
            subprocess.run(["iasl", "-vs", "-p", str(Path(tmp) / "round"), str(Path(tmp) / "patched.dsl")],
                           check=True, capture_output=True)


class J804WindowsScanoutContract(unittest.TestCase):
    """G6-S values shared by J804Windows.h, the FDF marker, the DSC, the SSDT variant and the
    handshake parser (no build)."""

    def setUp(self) -> None:
        self.h = header_defines()

    def test_header_values(self) -> None:
        h = self.h
        self.assertEqual((h["J804_SCANOUT_BASE"], h["J804_SCANOUT_SIZE"], h["J804_GSIV_SCANOUT"]),
                         (0xF60000000, 0x4000, 992))
        self.assertEqual(h["J804_SCANOUT_BASE"] - h["J804_INPUT_BASE"], 0x10000000)
        self.assertEqual((h["J804_SCANOUT_ADT_PROPERTY_SIZE"], h["J804_SCANOUT_VERSION"],
                          h["J804_SCANOUT_FD_MARKER_OFFSET"]), (32, 1, 0x150))
        self.assertEqual(header_strings()["J804_SCANOUT_ADT_PROPERTY"], "aura,windows-scanout-v1")
        gsivs = [h[k] for k in ("J804_GSIV_AHCI", "J804_GSIV_INPUT", "J804_GSIV_SCANOUT",
                                "J804_GSIV_XHC1", "J804_GSIV_ANS", "J804_GSIV_XHC2")]
        self.assertEqual(len(set(gsivs)), len(gsivs))

    def test_fd_marker_block(self) -> None:
        fdf = FDF.read_text()
        block = fdf.split("!if $(J804_WINDOWS_SCANOUT) == TRUE\n", 1)[1].split("!endif", 1)[0]
        self.assertIn("0x000000150|0x000000010", block)
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})\b", block.split("DATA", 1)[1]))
        self.assertEqual(data, b"J804SCN1" + struct.pack("<II", 0xF60000000 >> 12, 992))

    def test_dsc_defaults_off(self) -> None:
        dsc = DSC.read_text()
        self.assertIn("!ifndef J804_WINDOWS_SCANOUT\n  DEFINE J804_WINDOWS_SCANOUT = FALSE\n!endif",
                      dsc)
        self.assertIn("!if $(J804_WINDOWS_SCANOUT) == TRUE\n!if $(J804_WINDOWS_GPU) == FALSE\n!error",
                      dsc, "a scanout build needs the GPU build")

    @unittest.skipUnless(shutil.which("iasl") and shutil.which("cc") and shutil.which("make"),
                         "needs cc, make and iasl")
    def test_ssdt_compiles_and_patches(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            pre = Path(tmp) / "SsdtGpu0Scn.asl"
            subprocess.run(["cc", "-E", "-P", "-x", "c", "-I", str(HEADER.parent),
                            str(SSDT_GPU0_SCN), "-o", str(pre)], check=True)
            subprocess.run(["iasl", "-vs", "-p", str(Path(tmp) / "SsdtGpu0Scn"), str(pre)],
                           check=True, capture_output=True)
            template = Path(tmp) / "SsdtGpu0Scn.aml"
            check_gpu0_ssdt(self, template.read_bytes(), [gpu_sentinel(k) for k in range(1, 5)],
                            GPU_SENTINEL_IPA, scanout=self.h)
            # The G0 patcher takes the variant unchanged: every sentinel once.
            subprocess.run(["make", "-s", "-C", str(GPU_HOST_TEST)], check=True, capture_output=True)
            patched = Path(tmp) / "patched.aml"
            out = subprocess.run([str(GPU_HOST_TEST / "gpu_handoff_test"), str(template), str(patched)],
                                 capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout)
            check_gpu0_ssdt(self, patched.read_bytes(), [(b, b + n - 1, n) for b, n in GPU_SYNTHETIC],
                            GPU_SYNTHETIC[3][0], scanout=self.h)

    @unittest.skipUnless(shutil.which("cc") and shutil.which("make"), "needs cc and make")
    def test_handshake_parser(self) -> None:
        subprocess.run(["make", "-s", "-C", str(SCANOUT_HOST_TEST)], check=True, capture_output=True)
        out = subprocess.run([str(SCANOUT_HOST_TEST / "scanout_handshake_test")],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout)
        self.assertIn("PASS: 0 failure(s)", out.stdout)


class J804WindowsUsbContract(unittest.TestCase):
    """D4 values shared by J804Windows.h, the FDF marker, the DSC and the SSDT (no build)."""

    def setUp(self) -> None:
        self.h = header_defines()

    def test_header_values(self) -> None:
        h = self.h
        self.assertEqual((h["J804_XHC1_BASE"], h["J804_XHC_SIZE"], h["J804_GSIV_XHC1"]),
                         (0x40A430000, 0x8000, 995))
        self.assertEqual(h["J804_XHC2_BASE"], 0x412430000)
        self.assertEqual(h["J804_XHC1_BASE"] % 0x4000, 0, "16 KiB stage-2 pages")
        self.assertEqual(h["J804_XHC_SIZE"] % 0x4000, 0)
        self.assertLessEqual(h["J804_XHC_SIZE"], 0xC100, "the dwc3 globals at +0xc100 stay out")
        self.assertNotEqual(h["J804_XHC1_BASE"], 0x402430000, "never usb-drd0")
        self.assertNotIn(h["J804_GSIV_XHC1"], {h["J804_GSIV_AHCI"], h["J804_GSIV_ANS"]})
        self.assertEqual(header_strings()["J804_USB_ADT_PROPERTY"], "aura,windows-usb-v1")
        self.assertLess(len("aura,windows-usb-v1"), 32, "ADT property keys are 32 bytes")
        self.assertEqual(h["J804_USB_ADT_PROPERTY_SIZE"], 40)
        self.assertEqual((h["J804_USB_PORT_DRD1"], h["J804_USB_PORT_DRD3"]), (1 << 1, 1 << 3))
        self.assertEqual(h["J804_USB_FD_MARKER_OFFSET"], 0x120)

    def test_fd_marker_block(self) -> None:
        fdf = FDF.read_text()
        m = re.search(r"!if \$\(J804_WINDOWS_USB\) == TRUE\n(?:#[^\n]*\n)*"
                      r"(0x[0-9A-Fa-f]+)\|(0x[0-9A-Fa-f]+)\nDATA = \{(.*?)\}", fdf, re.S)
        self.assertIsNotNone(m, "USB marker region under !if J804_WINDOWS_USB")
        offset, size = int(m.group(1), 16), int(m.group(2), 16)
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b",
                                                     re.sub(r"#.*", "", m.group(3))))
        self.assertEqual(offset, self.h["J804_USB_FD_MARKER_OFFSET"])
        self.assertEqual(size, 0x10)
        self.assertEqual(data, b"J804USB1" + struct.pack("<II", self.h["J804_XHC1_BASE"] >> 12,
                                                         self.h["J804_GSIV_XHC1"]))
        # After the AHCI marker (0x110|0x10), before the FV at 0x8000.
        self.assertGreaterEqual(offset, self.h["J804_AHCI_FD_MARKER_OFFSET"] + 0x10)
        self.assertLessEqual(offset + size, 0x8000)

    def test_dsc_defaults_off(self) -> None:
        dsc = DSC.read_text()
        self.assertIn("!ifndef J804_WINDOWS_USB\n  DEFINE J804_WINDOWS_USB = FALSE\n!endif", dsc)
        self.assertIn('!if $(J804_WINDOWS_AHCI) == FALSE\n!error', dsc,
                      "a USB build needs the AHCI profile")
        # The non-USB branch of the CC/PP flags never names USB (after the GPU branch).
        dsc = dsc.split("-DJ804_WINDOWS_GPU=1\n!endif\n!else\n", 1)[1]
        block = dsc.split("!if $(J804_WINDOWS_USB) == TRUE\n!if $(J804_WINDOWS_VUART)", 1)[1]
        usb_on, usb_off = block.split("\n!else\n!if $(J804_WINDOWS_AHCI) == TRUE\n", 1)
        self.assertEqual(usb_on.count("-DJ804_WINDOWS_USB=1"), 3)
        self.assertNotIn("USB", usb_off.split("  GCC:*_*_AARCH64_ASLPP_FLAGS", 1)[0])
        components = dsc.split("[Components.common]", 1)[1]
        self.assertIn("!if $(J804_WINDOWS_USB) == TRUE\n"
                      "  MacBookPro2026Pkg/AcpiTables/J804Xhc1Ssdt.inf\n"
                      "  MacBookPro2026Pkg/Drivers/J804XhciAcpiDxe/J804XhciAcpiDxe.inf\n!endif",
                      components)

    def test_sources(self) -> None:
        self.assertNotIn("XHC", (ACPI_SRC / "DSDT.asl").read_text().split("DefinitionBlock", 1)[1],
                         "XHC1 is never in the DSDT")
        asl = SSDT_XHC1.read_text()
        body = re.sub(r"//.*", "", asl)
        for name in XHC1_FORBIDDEN_METHODS:
            self.assertNotIn(name.decode(), body)
        for macro in ("J804_XHC1_BASE", "J804_XHC_SIZE", "J804_GSIV_XHC1"):
            self.assertIn(macro, body)
        inf = (ACPI_SRC / "J804Xhc1Ssdt.inf").read_text()
        guid = re.search(r"FILE_GUID = ([0-9A-F-]+)", inf).group(1)
        driver = (REPO / "Platform" / "MacBookPro2026Pkg" / "Drivers" / "J804XhciAcpiDxe"
                  / "J804XhciAcpiDxe.c").read_text()
        g = guid.split("-")
        c_guid = (f"0x{g[0].lower()}, 0x{g[1].lower()}, 0x{g[2].lower()}, {{ "
                  + ", ".join(f"0x{g[3][i:i + 2].lower()}" for i in (0, 2)) + ", "
                  + ", ".join(f"0x{g[4][i:i + 2].lower()}" for i in range(0, 12, 2)) + " }")
        self.assertIn(c_guid, driver, "J804XhciAcpiDxe installs the J804Xhc1Ssdt FILE_GUID")

    @unittest.skipUnless(shutil.which("iasl") and shutil.which("cc"), "needs cc and iasl")
    def test_ssdt_compiles(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            pre = Path(tmp) / "SsdtXhc1.asl"
            subprocess.run(["cc", "-E", "-P", "-x", "c", "-I", str(HEADER.parent),
                            str(SSDT_XHC1), "-o", str(pre)], check=True)
            subprocess.run(["iasl", "-vs", "-p", str(Path(tmp) / "SsdtXhc1"), str(pre)],
                           check=True, capture_output=True)
            check_xhc1_ssdt(self, (Path(tmp) / "SsdtXhc1.aml").read_bytes(), self.h)


class J804WindowsInputContract(unittest.TestCase):
    """I0 values shared by J804Windows.h, the FDF marker, the DSC and the SSDT (no build)."""

    def setUp(self) -> None:
        self.h = header_defines()

    def test_header_values(self) -> None:
        h = self.h
        # aura-win j804-windows-input proxyclient/aura/windows.py INPUT_*.
        self.assertEqual((h["J804_INPUT_BASE"], h["J804_INPUT_SIZE"], h["J804_GSIV_INPUT"]),
                         (0xF50000000, 0x4000, 991))
        self.assertEqual(h["J804_INPUT_BASE"] % 0x4000, 0, "one 16 KiB stage-2 page")
        self.assertEqual(h["J804_INPUT_BASE"] - h["J804_AHCI_BASE"], 0x10000000,
                         "in the /arm-io gap, 256 MiB above the AHCI ABAR")
        self.assertTrue(0xE80000000 <= h["J804_INPUT_BASE"]
                        and h["J804_INPUT_BASE"] + h["J804_INPUT_SIZE"] <= 0x1000000000)
        self.assertGreaterEqual(h["J804_INPUT_BASE"], h["J804_GICR_BASE"] + 12 * h["J804_GICR_FRAME_SIZE"])
        self.assertNotIn(h["J804_GSIV_INPUT"], {h["J804_GSIV_AHCI"], h["J804_GSIV_XHC1"],
                                                h["J804_GSIV_ANS"], h["J804_GSIV_XHC2"], 984})
        self.assertTrue(980 <= h["J804_GSIV_INPUT"] < 1000, "the carrier's free 980..999 block")
        strings = header_strings()
        self.assertEqual(strings["J804_INPUT_HID"], "NTAS2040")
        self.assertNotIn(strings["J804_INPUT_HID"], ("NTAS0050", "AURA0700"))
        self.assertEqual(strings["J804_INPUT_ADT_PROPERTY"], "aura,windows-input-v1")
        self.assertLess(len("aura,windows-input-v1"), 32, "ADT property keys are 32 bytes")
        self.assertEqual(h["J804_INPUT_ADT_PROPERTY_SIZE"], struct.calcsize("<QIIII"))
        self.assertEqual(h["J804_INPUT_VERSION"], 1)
        self.assertEqual(h["J804_INPUT_FEATURES_REPORTS"], 0xF)
        self.assertEqual(h["J804_INPUT_FEATURES_SOURCES"], 0xFF << 16)
        self.assertEqual(h["J804_INPUT_FD_MARKER_OFFSET"], 0x140)

    def test_fd_marker_block(self) -> None:
        fdf = FDF.read_text()
        m = re.search(r"!if \$\(J804_WINDOWS_INPUT\) == TRUE\n(?:#[^\n]*\n)*"
                      r"(0x[0-9A-Fa-f]+)\|(0x[0-9A-Fa-f]+)\nDATA = \{(.*?)\}", fdf, re.S)
        self.assertIsNotNone(m, "input marker region under !if J804_WINDOWS_INPUT")
        offset, size = int(m.group(1), 16), int(m.group(2), 16)
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b",
                                                     re.sub(r"#.*", "", m.group(3))))
        self.assertEqual(offset, self.h["J804_INPUT_FD_MARKER_OFFSET"])
        self.assertEqual(size, 0x10)
        self.assertEqual(data, b"J804INP1" + struct.pack("<II", self.h["J804_INPUT_BASE"] >> 12,
                                                         self.h["J804_GSIV_INPUT"]))
        # After the USB block (0x120) and the slot kept for usb-drd3 (0x130),
        # before the FV at 0x8000.
        self.assertGreaterEqual(offset, self.h["J804_USB_FD_MARKER_OFFSET"] + 0x20)
        self.assertLessEqual(offset + size, 0x8000)
        regions = [int(o, 16) for o in re.findall(r"^(0x[0-9A-Fa-f]+)\|0x0+10$", fdf, re.M)]
        self.assertEqual(len(regions), len(set(regions)), "no two FDF marker regions share an offset")

    def test_dsc_defaults_off(self) -> None:
        dsc = DSC.read_text()
        self.assertIn("!ifndef J804_WINDOWS_INPUT\n  DEFINE J804_WINDOWS_INPUT = FALSE\n!endif", dsc)
        self.assertIn("!if $(J804_WINDOWS_INPUT) == TRUE\n!if $(J804_WINDOWS_AHCI) == FALSE\n!error",
                      dsc, "an input build needs the AHCI profile")
        # No C or ASL define: with the flag off nothing compiled changes.
        options = dsc.split("[BuildOptions.common]", 1)[1].split("!include", 1)[0]
        self.assertNotIn("INPUT", options)
        components = dsc.split("[Components.common]", 1)[1]
        self.assertIn("!if $(J804_WINDOWS_INPUT) == TRUE\n"
                      "  MacBookPro2026Pkg/AcpiTables/J804Inp0Ssdt.inf\n"
                      "  MacBookPro2026Pkg/Drivers/J804InputAcpiDxe/J804InputAcpiDxe.inf\n!endif",
                      components)

    def test_sources(self) -> None:
        dsdt = (ACPI_SRC / "DSDT.asl").read_text().split("DefinitionBlock", 1)[1]
        self.assertNotIn("INP0", dsdt, "INP0 is never in the DSDT")
        self.assertNotIn("J804_INPUT", dsdt)
        asl = SSDT_INP0.read_text()
        body = re.sub(r"//.*", "", asl)
        for name in XHC1_FORBIDDEN_METHODS + (b"_DSD", b"_DMA"):
            self.assertNotIn(name.decode(), body)
        for macro in ("J804_INPUT_HID", "J804_INPUT_BASE", "J804_INPUT_SIZE", "J804_GSIV_INPUT"):
            self.assertIn(macro, body)
        inf = (ACPI_SRC / "J804Inp0Ssdt.inf").read_text()
        guid = re.search(r"FILE_GUID = ([0-9A-F-]+)", inf).group(1)
        driver = (REPO / "Platform" / "MacBookPro2026Pkg" / "Drivers" / "J804InputAcpiDxe"
                  / "J804InputAcpiDxe.c").read_text()
        g = guid.split("-")
        c_guid = (f"0x{g[0].lower()}, 0x{g[1].lower()}, 0x{g[2].lower()}, {{ "
                  + ", ".join(f"0x{g[3][i:i + 2].lower()}" for i in (0, 2)) + ", "
                  + ", ".join(f"0x{g[4][i:i + 2].lower()}" for i in range(0, 12, 2)) + " }")
        self.assertIn(c_guid, driver, "J804InputAcpiDxe installs the J804Inp0Ssdt FILE_GUID")
        self.assertNotIn("MmioRead", driver, "Mu never touches the input page")
        self.assertNotIn("MmioWrite", driver, "Mu never touches the input page")

    @unittest.skipUnless(shutil.which("iasl") and shutil.which("cc"), "needs cc and iasl")
    def test_ssdt_compiles(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            pre = Path(tmp) / "SsdtInp0.asl"
            subprocess.run(["cc", "-E", "-P", "-x", "c", "-I", str(HEADER.parent),
                            str(SSDT_INP0), "-o", str(pre)], check=True)
            subprocess.run(["iasl", "-vs", "-p", str(Path(tmp) / "SsdtInp0"), str(pre)],
                           check=True, capture_output=True)
            check_inp0_ssdt(self, (Path(tmp) / "SsdtInp0.aml").read_bytes(), self.h)


class J804WindowsAhciContract(unittest.TestCase):
    """D1 values shared by J804Windows.h, the FDF marker and the carrier (no build)."""

    def setUp(self) -> None:
        self.h = header_defines()

    def test_header_values(self) -> None:
        h = self.h
        self.assertEqual((h["J804_AHCI_BASE"], h["J804_AHCI_SIZE"], h["J804_GSIV_AHCI"]),
                         (0xF40000000, 0x4000, 990))
        self.assertEqual(h["J804_AHCI_BASE"] % 0x4000, 0, "one 16 KiB stage-2 page")
        self.assertTrue(32 <= h["J804_GSIV_AHCI"] < 1020)
        self.assertNotIn(h["J804_GSIV_AHCI"],
                         {h["J804_GSIV_XHC1"], h["J804_GSIV_ANS"], h["J804_GSIV_XHC2"]},
                         "990 is no AIC alias")
        # The ABAR page is clear of the vGIC distributor, redistributors and
        # ITS frame (0xF20000000 + 0x10000), and of the vUART page.
        for low, size in ((h["J804_GICD_BASE"], 0x10000),
                          (h["J804_GICR_BASE"], h["J804_CPU_COUNT"] * h["J804_GICR_FRAME_SIZE"]),
                          (0xF20000000, 0x10000), (0x331200000, 0x4000)):
            self.assertFalse(low < h["J804_AHCI_BASE"] + h["J804_AHCI_SIZE"]
                             and h["J804_AHCI_BASE"] < low + size, hex(low))
        self.assertEqual(header_strings()["J804_AHCI_ADT_PROPERTY"], "aura,windows-ahci-v1")
        self.assertLess(len("aura,windows-ahci-v1"), 32, "ADT property keys are 32 bytes")
        self.assertEqual((h["J804_AHCI_ADT_PROPERTY_SIZE"], h["J804_AHCI_ADT_PROPERTY_SHORT"]),
                         (40, 24))
        self.assertEqual((h["J804_AHCI_BOOT_DISK"], h["J804_AHCI_BOOT_RAMDISK"]), (0, 1))

    def test_fd_marker_block(self) -> None:
        fdf = FDF.read_text()
        m = re.search(r"!if \$\(J804_WINDOWS_AHCI\) == TRUE\n(?:#[^\n]*\n)*"
                      r"(0x[0-9A-Fa-f]+)\|(0x[0-9A-Fa-f]+)\nDATA = \{(.*?)\}", fdf, re.S)
        self.assertIsNotNone(m, "AHCI marker region under !if J804_WINDOWS_AHCI")
        offset, size = int(m.group(1), 16), int(m.group(2), 16)
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b",
                                                     re.sub(r"#.*", "", m.group(3))))
        self.assertEqual(offset, self.h["J804_AHCI_FD_MARKER_OFFSET"])
        self.assertEqual(size, 0x10)
        self.assertEqual(data, b"J804AHC1" + struct.pack("<II", self.h["J804_AHCI_BASE"] >> 12,
                                                         self.h["J804_GSIV_AHCI"]))
        # Between the "AURANSNVSANCHECK" block (0x100|0x10) and the FV at 0x8000.
        self.assertGreaterEqual(offset, 0x110)
        self.assertLessEqual(offset + size, 0x8000)

    def test_dsc_defaults_off(self) -> None:
        dsc = DSC.read_text()
        self.assertIn("!ifndef J804_WINDOWS_AHCI\n  DEFINE J804_WINDOWS_AHCI = FALSE\n!endif", dsc)
        self.assertIn("-DJ804_WINDOWS_AHCI=$(J804_ASL_AHCI)", dsc)
        # Without the flag the CC flags must be exactly those of the M2/M3 FDs.
        block = dsc.split("!if $(J804_WINDOWS_AHCI) == TRUE\n!if $(J804_WINDOWS_VUART)", 1)[1]
        off = block.split("\n!else\n!if $(J804_WINDOWS_VUART) == TRUE\n", 1)[1]
        off = off.split("  GCC:*_*_AARCH64_ASLPP_FLAGS", 1)[0]
        self.assertIn("-DJ804_WINDOWS=1 -DJ804_WINDOWS_VUART=1\n", off)
        self.assertNotIn("AHCI", off)


class J804WindowsHandoffContract(unittest.TestCase):
    """J804Windows.h's hand-off range is what PrePi copies to (no build needed)."""

    def test_handoff_range_matches_pcds(self) -> None:
        h = header_defines()
        dsc = FAMILY_DSC.read_text()
        boot_args = int(re.search(r"PcdBootArgsPointer\|(0x[0-9A-Fa-f]+)", dsc).group(1), 16)
        adt = int(re.search(r"PcdAdtPointer\|(0x[0-9A-Fa-f]+)", dsc).group(1), 16)
        adt_max = int(re.search(r"#define\s+J804_M1_ADT_COPY_MAX\s+(0x[0-9A-Fa-f]+)",
                                J804M1_H.read_text()).group(1), 16)
        self.assertEqual(h["J804_HANDOFF_BASE"], boot_args)
        self.assertEqual(adt, boot_args + 0x4000)
        self.assertEqual(h["J804_HANDOFF_TOP"], adt + adt_max)


if __name__ == "__main__":
    unittest.main()

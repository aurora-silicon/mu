#!/usr/bin/env python3
"""Every platform's FvMain module list survives being hoisted into one include.

Silicon/Apple/AppleSiliconPkg/AppleSiliconFvMain.fdf.inc holds the fifty-three
INF lines all seven .fdf files had in common, plus the eleven that differ,
each behind a DEFINE. This expands the include with each machine's DEFINEs and
checks the resulting INF list -- contents *and* order -- equals what that
machine's .fdf listed before the hoist.

Order matters here, so this compares sequences, not sets. The one thing that is
deliberately not unified is where AppleNANDStorageDxe sits: four machines put it
after NonDiscoverablePciDeviceDxe and two after SmbiosDxe, and the two are
exactly the machines that boot from internal storage. PLATFORM_NAND_SLOT keeps
both.

The expected lists are the ones recorded at the time of the hoist. If a machine
legitimately gains or drops a driver, update EXPECTED in the same commit.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
INCLUDE = REPO / "Silicon/Apple/AppleSiliconPkg/AppleSiliconFvMain.fdf.inc"
EXPECTED = Path(__file__).with_name("fdf_fvmain_expected.json")


def defines_of(text: str) -> dict[str, str]:
    return dict(re.findall(r'^\s*DEFINE\s+(\w+)\s*=\s*(\S+)\s*$', text, re.M))


def expand(text: str, defines: dict[str, str]) -> list[str]:
    """Enough of the EDK2 .fdf preprocessor for !if/!endif and $(VAR)."""
    out: list[str] = []
    keep = [True]
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("!if "):
            cond = line[4:]
            m = re.match(r'\$\((\w+)\)\s*(==|!=)\s*(\S+)', cond)
            if not m:
                keep.append(keep[-1])
                continue
            name, op, want = m.groups()
            have = defines.get(name, "")
            hit = (have == want) if op == "==" else (have != want)
            keep.append(keep[-1] and hit)
            continue
        if line.startswith("!else"):
            if len(keep) > 1:
                keep[-1] = keep[-2] and not keep[-1]
            continue
        if line.startswith("!endif"):
            if len(keep) > 1:
                keep.pop()
            continue
        if not keep[-1] or not line.startswith("INF "):
            continue
        out.append(re.sub(r'\$\((\w+)\)', lambda m: defines.get(m.group(1), m.group(0)), line))
    return out


class FdfFvMainExpansionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.include = INCLUDE.read_text(encoding="utf-8")
        cls.expected = json.loads(EXPECTED.read_text(encoding="utf-8"))

    def _fvmain_infs(self, fdf: Path) -> list[str]:
        text = fdf.read_text(encoding="utf-8")
        defines = defines_of(text)
        body = text[text.index("[FV.FvMain]"):]
        body = body[: body.index("[FV.FVMAIN_COMPACT]")]
        expanded = []
        for raw in body.splitlines():
            if raw.strip().startswith("!include") and "AppleSiliconFvMain.fdf.inc" in raw:
                expanded.extend(expand(self.include, defines))
            else:
                expanded.append(raw)
        return expand("\n".join(expanded), defines)

    def test_every_platform_reproduces_its_module_list(self):
        seen = set()
        for fdf in sorted(REPO.glob("Platform/*Pkg/*.fdf")):
            name = fdf.name
            seen.add(name)
            with self.subTest(platform=name):
                self.assertIn(name, self.expected, f"{name} has no recorded module list")
                self.assertEqual(
                    self._fvmain_infs(fdf), self.expected[name],
                    f"{name}'s FvMain module list changed")
        self.assertEqual(seen, set(self.expected), "platform set changed")

    def test_the_include_is_actually_used(self):
        for fdf in sorted(REPO.glob("Platform/*Pkg/*.fdf")):
            with self.subTest(platform=fdf.name):
                self.assertRegex(
                    fdf.read_text(encoding="utf-8"),
                    r"(?m)^\s*!include\s+AppleSiliconPkg/AppleSiliconFvMain\.fdf\.inc\s*$")


if __name__ == "__main__":
    unittest.main()

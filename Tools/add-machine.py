#!/usr/bin/env python3
"""Create a Mu platform package for a Mac, from its Asahi device tree.

WHY THIS IS A GENERATOR AND THE TABLES ARE NOT

The ACPI tables stay static and hand-written, because their job is to map this
machine's hardware onto the device nodes our Windows drivers bind to. That
mapping is the work and it is not derivable: _HID strings, _DSD property names,
GSIV renumbering and AML methods exist in no device tree.

What IS derivable is everything that makes a machine *this* machine rather than
its sibling: the model strings, the chassis name, the MTP firmware blob, the
panel size, the Wi-Fi board type. Asahi's t6020-j414s.dts and t6020-j416s.dts
differ in seven lines, all of that kind. So this copies a template package for a
machine we have proven, applies those substitutions, and leaves every table
otherwise untouched.

    Tools/add-machine.py --list-templates
    Tools/add-machine.py j416c --dts t6021-j416c.dts --template j414s

WHAT IT DOES NOT CLAIM

A package that builds is not a machine that boots. This gets a device to the
point where its firmware compiles against a proven SoC family package with its
own identity; it says nothing about whether that machine's ANS, display or
input actually come up. Every generated package is marked accordingly.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "Platform"))

# Tables that only make sense on a machine with a lid, an internal keyboard and
# a keyboard backlight. A desktop gets the package without them.
LAPTOP_ONLY = ("KBL.asl", "LIDA.asl", "PBTN.asl", "MTP.asl")


def dt_facts(dts: Path) -> dict:
    """The per-machine facts Asahi's device tree states outright."""
    text = dts.read_text(encoding="utf-8", errors="replace")

    def one(pattern, default=None):
        m = re.search(pattern, text)
        return m.group(1) if m else default

    return {
        "model": one(r'model\s*=\s*"([^"]+)"'),
        "target": one(r'target-type:\s*(\S+)'),
        # The machine, then the SoC: compatible lists both, machine first.
        "compatible": one(r'compatible\s*=\s*"apple,(j[a-z0-9]+)"'),
        "soc": one(r'"apple,(t[0-9a-f]{4})"'),
        "chassis": one(r'apple,chassis-name\s*=\s*"([^"]+)"'),
        "mtp_firmware": one(r'firmware-name\s*=\s*"(apple/tpmtfw-[^"]+)"'),
        "panel_width_mm": one(r'width-mm\s*=\s*<(\d+)>'),
        "panel_height_mm": one(r'height-mm\s*=\s*<(\d+)>'),
        "board_type": one(r'brcm,board-type\s*=\s*"apple,([a-z0-9]+)"'),
    }


def generate(device: str, dts: Path, template: str, platform: str,
             laptop: bool) -> Path:
    import Features
    tspec = Features.DEVICES[template]
    src = REPO / "Platform" / f"{tspec['platform']}Pkg"
    dst = REPO / "Platform" / f"{platform}Pkg"
    facts = dt_facts(dts)
    if not facts["model"]:
        raise SystemExit(f"{dts} has no model string")

    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__"))

    tfacts = dt_facts_for_template(template)
    subs = [
        (tspec["platform"], platform),
        (tspec["platform"].upper(), platform.upper()),
        (tspec["fd"].replace("_EFI.fd", ""), f"{platform.upper()}"),
    ]
    for key in ("mtp_firmware", "chassis", "board_type"):
        if tfacts.get(key) and facts.get(key):
            subs.append((tfacts[key], facts[key]))
    for key, upper in (("target", True), ("compatible", False)):
        old, new = tfacts.get(key), facts.get(key)
        if old and new:
            subs.append((old, new))
            if upper:
                subs.append((old.upper(), new.upper()))
                subs.append((old.lower(), new.lower()))
    if tfacts.get("model") and facts.get("model"):
        subs.append((tfacts["model"].replace("Apple ", ""),
                     facts["model"].replace("Apple ", "")))

    for path in sorted(dst.rglob("*")):
        if path.is_dir() or path.suffix in (".png", ".pyc"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for old, new in subs:
            if old and new:
                text = text.replace(old, new)
        path.write_text(text, encoding="utf-8")

    for path in list(dst.rglob(f"*{tspec['platform']}*")):
        path.rename(path.with_name(path.name.replace(tspec["platform"], platform)))

    # Identity the .dsc states outright, rather than inheriting the template's.
    dsc = dst / f"{platform}.dsc"
    text = dsc.read_text(encoding="utf-8")
    soc = facts["soc"]  # e.g. "t6021"
    if soc and re.fullmatch(r"t[0-9a-f]{4}", soc):
        text = re.sub(
            r"(\[PcdsFixedAtBuild[^\]]*\]\n)",
            r"\1  # This machine's SoC, from its device tree compatible. The family\n"
            r"  # package pins the base part of the family; a Max or Ultra is not it.\n"
            f"  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x{soc[1:]}\n",
            text, count=1)
    model = facts["model"].replace("Apple ", "")
    text = re.sub(r'(PcdSmbiosSystemModel\|)"[^"]*"', lambda m: f'{m.group(1)}"{model}"', text)
    text = re.sub(r'(PcdSmbiosSystemModelNumber\|)"[^"]*"',
                  lambda m: f'{m.group(1)}"{facts["target"] or device}"', text)
    text = re.sub(r'(PcdSmbiosSystemSku\|)"[^"]*"',
                  lambda m: f'{m.group(1)}"{model} ({facts["target"] or device})"', text)
    dsc.write_text(text, encoding="utf-8")

    if not laptop:
        inf = dst / "AcpiTables" / "DeviceAcpiTables.inf"
        text = inf.read_text(encoding="utf-8")
        for table in LAPTOP_ONLY:
            text = text.replace(f"  {table}\n", "")
            (dst / "AcpiTables" / table).unlink(missing_ok=True)
        inf.write_text(text)

    banner = (f"# Generated by Tools/add-machine.py from Asahi's {dts.name}.\n"
              f"# {facts['model']}\n"
              "#\n"
              "# UNVERIFIED ON HARDWARE. This compiles against a proven SoC family\n"
              "# package with this machine's own identity; nothing here has been\n"
              "# booted. Its ANS, display and input paths are inherited from the\n"
              f"# template ({template}) and have not been checked on this machine.\n#\n")
    build = dst / "PlatformBuild.py"
    build.write_text(banner + build.read_text(encoding="utf-8"))
    return dst


def dt_facts_for_template(template: str) -> dict:
    cached = REPO / "Silicon/Apple/AppleSiliconPkg/DeviceTree"
    for name in (f"t6020-{template}.dts", f"{template}.dts"):
        path = cached / name
        if path.is_file():
            return dt_facts(path)
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("device")
    parser.add_argument("--dts", type=Path, required=True)
    parser.add_argument("--template", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--desktop", action="store_true")
    args = parser.parse_args()
    dst = generate(args.device, args.dts, args.template, args.platform,
                   laptop=not args.desktop)
    print(f"{args.device}: {dst.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

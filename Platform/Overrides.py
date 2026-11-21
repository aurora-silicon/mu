# @file
# Our changes to Microsoft's Mu modules, carried inside this repository.
#
# Every submodule points at Microsoft's own repository at an upstream commit.
# Where we need a Microsoft module to behave differently we carry it here in one
# of two ways:
#
#   - an override: the whole module directory, copied from the pinned submodule
#     into Silicon/Apple/AppleSiliconPkg/Override/<Pkg>/<path>, our change on
#     top, and a "#Override" tag on the INF's first line that Mu's
#     OverrideValidation plugin re-checks against the pinned original on every
#     build (a re-pin that changes the original fails the build);
#   - a build-time patch, for a module too large to copy (the DXE core): the
#     module is extracted from the submodule's object store at the gitlink the
#     superproject pins, patched outside the submodule, and written to the
#     git-ignored Override/Generated tree. The submodule checkout is never
#     written, so a failed build cannot leave it dirty.
#
# The dsc/fdf lines name the override instead of the original. A dsc/fdf edit,
# an !if that stops matching, or a merge that restores the original line would
# drop an override silently -- the build would still succeed. So after every
# build PlatformPostBuild calls prove(): it reads the build report the build
# just wrote (BUILD_REPORT.TXT: the FD section's FV module lists and each
# module's library instances) and fails the build unless, for every override,
# the override instance (or a declared alternative, proven separately) is the
# one built and linked into the image and the original is not.
#
#     python3 Platform/Overrides.py prove <BUILD_REPORT.TXT> <workspace> [<output_base>]
#     python3 Platform/Overrides.py generate <workspace>
#
# Standard library only, so it also runs outside the stuart environment.
#
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path

OVERRIDE_ROOT = "Silicon/Apple/AppleSiliconPkg/Override"
GENERATED_ROOT = OVERRIDE_ROOT + "/Generated"

# Same order as PlatformBuildCommon.PACKAGES_PATH. Used to find the original
# module the way the build would have found it.
PACKAGES_PATH = (
    "Platform",
    "Silicon/ARM/TIANO",
    "MU_BASECORE",
    "Common/MU",
    "Common/TIANO",
    "Common/MU_OEM_SAMPLE",
    "Silicon/Apple",
    "Common/MU_DFCI",
    "mu_feature_debugger",
)

SUBMODULES = (
    "MU_BASECORE",
    "Common/MU",
    "Common/TIANO",
    "Common/MU_OEM_SAMPLE",
    "Silicon/ARM/TIANO",
    "Common/MU_DFCI",
    "mu_feature_debugger",
)


@dataclass(frozen=True)
class Patch:
    submodule: str      # workspace-relative submodule path
    module_dir: str     # module directory inside the submodule
    patch: str          # workspace-relative patch file
    output: str         # workspace-relative generated module directory


@dataclass(frozen=True)
class Alternative:
    """Another instance a profile may use instead of the override.

    includes: files of the override that the alternative must compile (proven
    from the module's .obj.deps files); excludes: path fragments that must not
    appear in those deps.
    """
    inf: str
    reason: str
    includes: tuple = ()
    excludes: tuple = ()


@dataclass(frozen=True)
class Override:
    name: str
    kind: str                       # "module" or "library"
    original: str                   # package-relative INF of the original
    override: str                   # package-relative INF of the override
    library_class: str | None = None
    alternatives: tuple = field(default_factory=tuple)
    source: str = ""


DXE_CORE_PATCH = Patch(
    submodule="MU_BASECORE",
    module_dir="MdeModulePkg/Core/Dxe",
    patch=OVERRIDE_ROOT + "/Patches/MU_BASECORE/MdeModulePkg-Core-Dxe.patch",
    output=GENERATED_ROOT + "/MdeModulePkg/Core/Dxe",
)

PATCHES = (DXE_CORE_PATCH,)

OVERRIDES = (
    Override(
        name="PrePiLib",
        kind="library",
        library_class="PrePiLib",
        original="EmbeddedPkg/Library/PrePiLib/PrePiLib.inf",
        override="AppleSiliconPkg/Override/EmbeddedPkg/Library/PrePiLib/PrePiLib.inf",
        source="mu_basecore 28a715f26c81 (PrePiLib half)",
    ),
    Override(
        name="DxeCore",
        kind="module",
        original="MdeModulePkg/Core/Dxe/DxeMain.inf",
        override="AppleSiliconPkg/Override/Generated/MdeModulePkg/Core/Dxe/DxeMain.inf",
        source="mu_basecore 28a715f26c81 (DXE core half, build-time patch)",
    ),
    Override(
        name="CpuDxe",
        kind="module",
        original="ArmPkg/Drivers/CpuDxe/CpuDxe.inf",
        override="AppleSiliconPkg/Override/ArmPkg/Drivers/CpuDxe/CpuDxe.inf",
        source="mu_silicon_arm_tiano 7b3db1e6fe68 (CpuDxe half)",
    ),
    Override(
        name="ArmExceptionLib",
        kind="library",
        library_class="CpuExceptionHandlerLib",
        original="ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.inf",
        override="AppleSiliconPkg/Override/ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.inf",
        alternatives=(
            Alternative(
                inf="AppleSiliconPkg/Library/AuraArmExceptionLib/AuraArmExceptionLib.inf",
                reason="J813 Windows profile; #includes the override's sources",
                includes=(
                    "AppleSiliconPkg/Override/ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.c",
                    "AppleSiliconPkg/Override/ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c",
                ),
                excludes=("/ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.c",
                          "/ArmPkg/Library/ArmExceptionLib/AArch64/AArch64Exception.c"),
            ),
        ),
        source="mu_silicon_arm_tiano 7b3db1e6fe68 (exception half)",
    ),
    Override(
        name="MuVarPolicyFoundationDxe",
        kind="module",
        original="MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf",
        override="AppleSiliconPkg/Override/MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf",
        alternatives=(
            Alternative(
                inf="AppleSiliconPkg/Drivers/AuraVarPolicyFoundationDxe/AuraVarPolicyFoundationDxe.inf",
                reason="J813 Windows profile keeps Microsoft's file (#Track) with its own remap",
                includes=("Common/MU/MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c",),
                excludes=("/Override/MsCorePkg/MuVarPolicyFoundationDxe/",),
            ),
        ),
        source="mu_plus b831f2c0efe9",
    ),
    Override(
        name="MsBootPolicy",
        kind="module",
        original="PcBdsPkg/MsBootPolicy/MsBootPolicy.inf",
        override="AppleSiliconPkg/Override/PcBdsPkg/MsBootPolicy/MsBootPolicy.inf",
        source="mu_plus 99c64080efb2 + fa4cfd83ae6b",
    ),
)


# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------

def resolve(workspace: Path, rel: str, packages_path=PACKAGES_PATH) -> Path | None:
    """The file the build would open for a package-relative path."""
    for entry in packages_path:
        candidate = workspace / entry / rel
        if candidate.exists():
            return candidate.resolve()
    candidate = workspace / rel
    return candidate.resolve() if candidate.exists() else None


def resolve_original(workspace: Path, rel: str) -> Path | None:
    """The original module, skipping our own package (where overrides live)."""
    return resolve(workspace, rel, tuple(p for p in PACKAGES_PATH if p != "Silicon/Apple"))


def norm(path: str) -> str:
    return os.path.realpath(os.path.normpath(path))


# --------------------------------------------------------------------------
# Build-time patches
# --------------------------------------------------------------------------

def _git(*args, cwd=None) -> bytes:
    return subprocess.run(["git", *args], cwd=cwd, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def gitlink(workspace: Path, submodule: str) -> str:
    out = _git("-C", str(workspace), "ls-tree", "HEAD", submodule).decode().split()
    if len(out) < 3 or out[1] != "commit":
        raise RuntimeError(f"no gitlink for {submodule}")
    return out[2]


def check_pins(workspace: Path) -> list[str]:
    """Each submodule checkout must be the commit the superproject pins."""
    errors = []
    for sub in SUBMODULES:
        head = _git("-C", str(workspace / sub), "rev-parse", "HEAD").decode().strip()
        pin = gitlink(workspace, sub)
        if head != pin:
            errors.append(f"{sub}: checkout {head} is not the pinned {pin}")
    return errors


def check_submodules_clean(workspace: Path) -> list[str]:
    errors = []
    for sub in SUBMODULES:
        status = _git("-C", str(workspace / sub), "status", "--porcelain=v1",
                      "--untracked-files=all", "--ignore-submodules=none").decode()
        if status.strip():
            errors.append(f"{sub} is dirty:\n" + status.rstrip())
    return errors


def generate(workspace: Path, log=print) -> int:
    """Write every build-time patched module into Override/Generated."""
    errors = check_pins(workspace)
    if errors:
        for e in errors:
            log("error: " + e)
        return 1
    for p in PATCHES:
        commit = gitlink(workspace, p.submodule)
        archive = _git("-C", str(workspace / p.submodule), "archive", "--format=tar",
                       commit, p.module_dir)
        output = workspace / p.output
        with tempfile.TemporaryDirectory(prefix="mu-override-") as tmp:
            # Outside any repository, so git apply acts on plain files.
            with tarfile.open(fileobj=BytesIO(archive)) as tar:
                tar.extractall(tmp, filter="data")
            root = Path(tmp) / p.module_dir
            for f in root.rglob("*"):
                if f.is_file():
                    data = f.read_bytes()
                    if b"\0" not in data:
                        f.write_bytes(data.replace(b"\r\n", b"\n"))
            patch = str((workspace / p.patch).resolve())
            for args in (("apply", "--check", patch), ("apply", patch)):
                r = subprocess.run(["git", *args], cwd=root, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
                if r.returncode != 0:
                    log(f"error: {p.patch} does not apply to {p.submodule}@{commit} "
                        f"{p.module_dir}:\n{r.stdout.decode(errors='replace')}")
                    return 1
            if output.exists():
                shutil.rmtree(output)
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(root, output)
        log(f"generated {p.output} from {p.submodule}@{commit[:12]} + {p.patch}")
    return 0


# --------------------------------------------------------------------------
# Build report parsing
# --------------------------------------------------------------------------

@dataclass
class ModuleReport:
    name: str = ""
    inf: str = ""
    guid: str = ""
    libraries: list = field(default_factory=list)   # (abs path, class)


def parse_report(text: str):
    lines = text.splitlines()
    modules: list[ModuleReport] = []
    image: list[tuple[str, str, str]] = []           # (fv, module name, abs inf)
    current = None
    in_lib = False
    pending = ""
    fv = None
    in_fv_table = False
    entry = None
    for i, line in enumerate(lines):
        if line == "Module Summary":
            current = ModuleReport()
            modules.append(current)
            in_lib = False
            continue
        if line.startswith("Firmware Device (FD)"):
            current = None
            in_lib = False
        if current is not None:
            if line.startswith("Module Name:"):
                current.name = line.split(":", 1)[1].strip()
            elif line.startswith("Module INF Path:"):
                current.inf = line.split(":", 1)[1].strip().replace("\\", "/")
            elif line.startswith("File GUID:"):
                current.guid = line.split(":", 1)[1].strip().upper()
            elif line == "Library" and i > 0 and lines[i - 1].startswith(">--"):
                in_lib = True
                pending = ""
                continue
            elif in_lib:
                if line.startswith("<--"):
                    in_lib = False
                elif line.startswith("{"):
                    cls = line[1:].split(":", 1)[0].strip()
                    current.libraries.append((pending, cls))
                    pending = ""
                elif line.startswith("---"):
                    pass
                elif line:
                    pending += line.strip()
            continue
        # FD section
        if line.startswith("Fv Name:"):
            fv = line.split(":", 1)[1].strip().split(" ")[0]
            in_fv_table = False
            continue
        if line.startswith("Offset") and "Module" in line:
            in_fv_table = True
            continue
        if in_fv_table:
            if line.startswith("<--"):
                in_fv_table = False
                entry = None
                continue
            if line.startswith("---"):
                continue
            m = re.match(r"^0x[0-9A-Fa-f]+ (\S.*?) \((.*)$", line)
            if m:
                entry = [m.group(1), m.group(2)]
            elif entry is not None:
                entry[1] += line.strip()
            if entry is not None and entry[1].endswith(")"):
                image.append((fv, entry[0], entry[1][:-1]))
                entry = None
    return modules, image


# --------------------------------------------------------------------------
# Proof
# --------------------------------------------------------------------------

def _deps_files(output_base: Path, inf_rel: str) -> list[Path]:
    """The .obj.deps files of a module build directory (any arch)."""
    stem = Path(inf_rel).with_suffix("")
    found = []
    for arch_dir in output_base.iterdir() if output_base.is_dir() else []:
        d = arch_dir / stem / "OUTPUT"
        if d.is_dir():
            found.extend(sorted(d.glob("*.deps")))
    return found


def _deps_paths(files: list[Path]) -> set[str]:
    paths = set()
    for f in files:
        text = f.read_text(errors="replace").replace("\\\n", " ")
        for token in text.split():
            if token.endswith(":"):
                continue
            paths.add(norm(token))
    return paths


def prove(report_path: Path, workspace: Path, output_base: Path | None = None, log=print) -> int:
    workspace = workspace.resolve()
    modules, image = parse_report(report_path.read_text(errors="replace"))
    if not image:
        log(f"error: no FV module list in {report_path} (FLASH report type missing?)")
        return 1
    image_infs = {norm(p): (fv, name) for fv, name, p in image}
    by_inf = {}
    for m in modules:
        by_inf.setdefault(m.inf, []).append(m)
    # Modules that are in the image (match each summary to an FV entry).
    image_modules = []
    for m in modules:
        a = resolve(workspace, m.inf)
        if a is not None and norm(str(a)) in image_infs:
            image_modules.append((m, norm(str(a))))
    failures = 0
    out = []
    for o in OVERRIDES:
        ov = resolve(workspace, o.override)
        orig = resolve_original(workspace, o.original)
        if ov is None:
            out.append(f"FAIL {o.name}: override {o.override} does not exist")
            failures += 1
            continue
        ov_n = norm(str(ov))
        orig_n = norm(str(orig)) if orig else None
        alt_used = []
        if o.kind == "module":
            if orig_n and orig_n in image_infs:
                fv, name = image_infs[orig_n]
                out.append(f"FAIL {o.name}: the ORIGINAL {o.original} is in {fv} as {name}")
                failures += 1
                continue
            if ov_n in image_infs:
                fv, name = image_infs[ov_n]
                summ = [m for m in by_inf.get(o.override, [])]
                if not summ:
                    out.append(f"FAIL {o.name}: {fv} lists {o.override} but no module summary has that INF path")
                    failures += 1
                    continue
                out.append(f"PASS {o.name}: {fv} contains {name} built from {o.override} "
                           f"(FILE_GUID {summ[0].guid}); original {o.original} not in the image")
                continue
            for alt in o.alternatives:
                a = resolve(workspace, alt.inf)
                if a is not None and norm(str(a)) in image_infs:
                    alt_used.append((alt, image_infs[norm(str(a))]))
            if not alt_used:
                out.append(f"FAIL {o.name}: neither {o.override} nor a declared alternative is in the image")
                failures += 1
                continue
        else:
            alts = {}
            for alt in o.alternatives:
                a = resolve(workspace, alt.inf)
                if a is not None:
                    alts[norm(str(a))] = alt
            linkers = []
            bad = []
            alt_linkers = {}
            for m, _ in image_modules:
                for lib, cls in m.libraries:
                    ln = norm(lib)
                    if ln == ov_n:
                        linkers.append(m.name)
                    elif orig_n and ln == orig_n:
                        bad.append(f"{m.name} (links the original)")
                    elif ln in alts:
                        alt_linkers.setdefault(alts[ln], []).append(m.name)
                    elif cls == o.library_class:
                        bad.append(f"{m.name} (links {lib} as {cls})")
            if bad:
                out.append(f"FAIL {o.name}: image modules link something other than the override "
                           f"for {o.library_class}: {', '.join(sorted(set(bad)))}")
                failures += 1
                continue
            if linkers:
                out.append(f"PASS {o.name}: {len(linkers)} image module(s) link {o.override} as "
                           f"{o.library_class} ({', '.join(sorted(set(linkers))[:6])}"
                           f"{', ...' if len(set(linkers)) > 6 else ''}); none links {o.original}")
                if not alt_linkers:
                    continue
            for alt, names in alt_linkers.items():
                alt_used.append((alt, ("library", ", ".join(sorted(set(names))[:4]))))
            if not linkers and not alt_used:
                out.append(f"FAIL {o.name}: no image module links {o.override} or a declared alternative")
                failures += 1
                continue
        # Prove each alternative compiles the files it must.
        for alt, where in alt_used:
            if output_base is None:
                out.append(f"FAIL {o.name}: alternative {alt.inf} used, but no build output to prove it from")
                failures += 1
                continue
            deps = _deps_paths(_deps_files(output_base, alt.inf))
            if not deps:
                out.append(f"FAIL {o.name}: alternative {alt.inf} has no .deps files under {output_base}")
                failures += 1
                continue
            missing = []
            for inc in alt.includes:
                target = resolve(workspace, inc) or (workspace / inc)
                if norm(str(target)) not in deps:
                    missing.append(inc)
            forbidden = sorted(p for p in deps for ex in alt.excludes
                               if ex in p and not p.startswith(norm(str(workspace / OVERRIDE_ROOT)))
                               and not any(p == norm(str(resolve(workspace, i) or workspace / i)) for i in alt.includes))
            if missing or forbidden:
                out.append(f"FAIL {o.name}: alternative {alt.inf} ({alt.reason}) does not compile "
                           f"{missing} or compiles {forbidden}")
                failures += 1
                continue
            out.append(f"PASS {o.name}: alternative {alt.inf} in the image ({where[0]}: {where[1]}); "
                       f"{alt.reason}; its .deps include {', '.join(alt.includes)}")
    header = f"Override proof for {report_path} ({len(OVERRIDES)} overrides, {len(image)} FV entries)"
    log(header)
    for line in out:
        log("  " + line)
    proof = report_path.with_name("OVERRIDE_PROOF.txt")
    proof.write_text(header + "\n" + "\n".join(out) + "\n")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[1] == "prove":
        base = Path(argv[4]) if len(argv) > 4 else None
        return prove(Path(argv[2]), Path(argv[3]), base)
    if len(argv) == 3 and argv[1] == "generate":
        return generate(Path(argv[2]).resolve())
    if len(argv) == 3 and argv[1] == "clean":
        errors = check_submodules_clean(Path(argv[2]).resolve())
        for e in errors:
            print("error: " + e)
        return 1 if errors else 0
    print(__doc__ or "usage: Overrides.py prove|generate|clean ...", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

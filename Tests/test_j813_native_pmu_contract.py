#!/usr/bin/env python3
"""Static contract checks for the J813 native PMUv3 boot profile."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "Platform" / "MacBookAir2026Pkg"
BUILD_SCRIPT = ROOT / "tools" / "build-j813-windows-native.sh"
PATCH_DRIVER = "WindowsPmuCompatDxe"
ARM_EXCEPTION = ROOT / "Silicon" / "ARM" / "TIANO" / "ArmPkg" / "Library" / "ArmExceptionLib"


class J813NativePmuContractTests(unittest.TestCase):
    def test_profile_does_not_build_or_embed_patch_driver(self):
        for path in (PROFILE / "MacBookAir2026.dsc", PROFILE / "MacBookAir2026.fdf"):
            active_lines = [
                line
                for line in path.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")
            ]
            self.assertFalse(
                any(PATCH_DRIVER in line for line in active_lines),
                f"{path.name} still includes {PATCH_DRIVER}",
            )

    def test_manifest_declares_native_el2_contract(self):
        script = BUILD_SCRIPT.read_text(encoding="utf-8")
        self.assertIn('"backend": "m1n1_el2_t8142"', script)
        self.assertIn('"windows_pmu_compat_dxe_embedded": False', script)
        self.assertIn('"microsoft_pe_images_modified": False', script)

    def test_firmware_only_forwards_unknown_sysregs_to_el2(self):
        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in ARM_EXCEPTION.rglob("*")
            if path.suffix in {".c", ".h", ".S"}
        )
        profile = (PROFILE / "MacBookAir2026.dsc").read_text(encoding="utf-8")
        self.assertIn("NTASI_J813_EL2_SYSREG_ASSIST=1", profile)
        self.assertIn("NTASI_EL2_SYSREG_ASSIST_MAGIC", sources)
        self.assertIn("ArmCallHvc", sources)
        self.assertNotIn("NTASI_J813_PMCCNTR_EMULATION", sources)
        self.assertNotIn("emulated MRS", sources)
        self.assertNotIn("ignored PMU MSR", sources)


if __name__ == "__main__":
    unittest.main()

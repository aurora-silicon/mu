#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Static contract for dynamic J414s POST-display ACPI publication."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACPI = (
    ROOT
    / "Silicon/Apple/AppleSiliconPkg/Drivers/AcpiPlatformDxe/AcpiPlatform.c"
)
DSC = ROOT / "Platform/MacBookProEarly2023Pkg/MacBookProEarly2023.dsc"
TABLES = (
    ROOT
    / "Platform/MacBookProEarly2023Pkg/AcpiTables/DeviceAcpiTables.inf"
)
SPEC = ROOT / "Platform/MacBookProEarly2023Pkg/AcpiTables/DISP.asl"


class DisplayPostAcpiContract(unittest.TestCase):
    def setUp(self) -> None:
        self.acpi = ACPI.read_text()
        self.dsc = DSC.read_text()

    def test_static_disp_table_is_not_compiled(self) -> None:
        sources = TABLES.read_text().split("[Packages]", 1)[0]
        self.assertNotRegex(sources, r"(?m)^\s*DISP\.asl\s*$")
        self.assertIn("provenance specification", SPEC.read_text())

    def test_runtime_emitter_is_enabled_for_j414s(self) -> None:
        self.assertIn("DEFINE NTASI_ENABLE_DISPLAY_ACPI_PUBLICATION = 1", self.dsc)
        self.assertIn(
            "-DNTASI_ENABLE_DISPLAY_ACPI_PUBLICATION=$(NTASI_ENABLE_DISPLAY_ACPI_PUBLICATION)",
            self.dsc,
        )
        self.assertIn("NtasiInstallDisplayTable (AcpiTable)", self.acpi)
        self.assertIn('AmlCodeGenNameString ("_HID", "NTAS0070"', self.acpi)
        self.assertIn(
            "#if NTASI_ENABLE_DISPLAY_ACPI_PUBLICATION && (NTASI_GPU_ACPI_HID != 24)",
            self.acpi,
        )

    def test_six_fixed_windows_then_live_framebuffer(self) -> None:
        table = re.search(
            r"mNtasiDisplayWindows\[\].*?=\s*\{(.*?)\n\};",
            self.acpi,
            re.S,
        )
        self.assertIsNotNone(table)
        self.assertEqual(len(re.findall(r"\{\s*0x[0-9A-Fa-f]+ULL", table.group(1))), 6)
        loop = self.acpi.index("for (Index = 0; Index < ARRAY_SIZE (mNtasiDisplayWindows)")
        live = self.acpi.index(
            "AppleAnsAddMemoryResource (CrsNode, FramebufferBase, FramebufferLength)"
        )
        self.assertLess(loop, live)

    def test_framebuffer_comes_from_bootargs_and_is_bounded(self) -> None:
        for needle in (
            "FixedPcdGet64 (PcdBootArgsPointer)",
            "BootArgs->video.base",
            "BootArgs->video.stride",
            "BootArgs->video.height",
            "BootArgs->rv1.mem_size_actual",
            "BootArgs->rv2.mem_size_actual",
            "BootArgs->rv3.mem_size_actual",
            "FramebufferLength = (RawLength + 0x3FFFULL) & ~0x3FFFULL",
        ):
            self.assertIn(needle, self.acpi)
        self.assertNotIn("PcdFrameBufferAddress", self.acpi)
        self.assertNotIn("PcdFrameBufferSize", self.acpi)

    def test_install_failure_is_boot_fatal(self) -> None:
        block = re.search(
            r"Status = NtasiInstallDisplayTable \(AcpiTable\);(.*?)#endif",
            self.acpi,
            re.S,
        )
        self.assertIsNotNone(block)
        self.assertIn("return EFI_ABORTED", block.group(1))

    def test_wddm_is_one_full_device_not_two_consumers(self) -> None:
        self.assertIn("#if NTASI_GPU_ACPI_HID == 24", self.acpi)
        self.assertIn(
            "NTASI_GPU_RES_COUNT + ARRAY_SIZE (mNtasiDisplayWindows) + 1",
            self.acpi,
        )
        gpu = self.acpi[self.acpi.index("NtasiInstallGpuTable (") :]
        self.assertIn("NtasiResolveBootFramebuffer", gpu)
        self.assertIn("mNtasiDisplayWindows[Index].Base", gpu)
        self.assertIn("CrsNode, FramebufferBase, FramebufferLength", gpu)


if __name__ == "__main__":
    unittest.main()

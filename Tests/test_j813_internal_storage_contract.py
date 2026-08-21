"""Host-side contract for J813 firmware-owned internal Apple ANS/NVMe."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DSC = ROOT / "Platform/MacBookAir2026Pkg/MacBookAir2026.dsc"
FDF = ROOT / "Platform/MacBookAir2026Pkg/MacBookAir2026.fdf"
BUILD = ROOT / "Tools/build-j813-windows-native.sh"
MANIFEST = ROOT / "Tools/j813_mu_profile_manifest.py"
PMGR_HEADER = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Include/Drivers/AppleAnsPmgrDomain.h"
)
ANS_DRIVER = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/"
    "AppleNANDStorageDxe.c"
)
NVME_CORE_HEADER = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/Shared/"
    "AppleNvmeCore.h"
)
NVME_CORE = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/Shared/"
    "AppleNvmeCore.c"
)
NVME_CONTROLLER = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Drivers/AppleNANDStorageDxe/Shared/"
    "AppleNvmeControllerCore.c"
)
STORAGE_PROBE = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Application/StorageProbe/StorageProbe.c"
)


def load_manifest_module():
    spec = importlib.util.spec_from_file_location("j813_mu_profile_manifest", MANIFEST)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class J813InternalStorageContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dsc = DSC.read_text(encoding="utf-8")
        self.fdf = FDF.read_text(encoding="utf-8")
        self.build = BUILD.read_text(encoding="utf-8")
        self.pmgr = PMGR_HEADER.read_text(encoding="utf-8")
        self.driver = ANS_DRIVER.read_text(encoding="utf-8")
        self.nvme_core_header = NVME_CORE_HEADER.read_text(encoding="utf-8")
        self.nvme_core = NVME_CORE.read_text(encoding="utf-8")
        self.nvme_controller = NVME_CONTROLLER.read_text(encoding="utf-8")
        self.storage_probe = STORAGE_PROBE.read_text(encoding="utf-8")

    def test_profile_enables_block_io_but_withholds_windows_acpi(self) -> None:
        profile = load_manifest_module().PROFILES["internal-storage"]
        self.assertTrue(profile["bootable"])
        self.assertTrue(profile["aic"])
        self.assertTrue(profile["ans"])
        self.assertTrue(profile["ans_dxe"])
        self.assertTrue(profile["ans_block_io"])
        self.assertTrue(profile["ans_preserve"])
        self.assertFalse(profile["ans_acpi"])

    def test_profile_build_variables_are_explicit_and_fail_closed(self) -> None:
        self.assertIn("internal-storage)", self.build)
        self.assertIn('"BLD_*_NTASI_ENABLE_ANS=$ans_enable"', self.build)
        self.assertIn('"BLD_*_NTASI_ANS_DXE_BRINGUP=$ans_dxe"', self.build)
        self.assertIn(
            '"BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO=$ans_block_io"', self.build
        )
        self.assertIn(
            '"BLD_*_NTASI_ANS_PRESERVE_FOR_OS=$ans_preserve"', self.build
        )
        # ACPI publication is per-profile now that one profile publishes it,
        # but it still has to default closed: only a profile that opts in may
        # set it, and a profile with ANS disabled may never set it.
        self.assertIn('"BLD_*_NTASI_ANS_PUBLISH_ACPI=$ans_acpi"', self.build)
        self.assertIn("ans_acpi=${ans_acpi:-FALSE}", self.build)
        self.assertNotIn("ans_acpi=TRUE\n        mtp_hid_build=FALSE", self.build)

    def test_windows_profile_publishes_acpi_with_the_alias_pair(self) -> None:
        """The one profile that exposes ANS to Windows, and its GSIV contract.

        996/1155 must stay equal to the {published, physical} pair in m1n1's
        hv_aic_aliases_t8142.  Mu publishes the GSIV and m1n1 performs the
        renumbering; neither can discover the other's choice at runtime, so
        the only thing keeping them in step is this assertion.
        """
        profile = load_manifest_module().PROFILES["internal-storage-windows"]
        self.assertTrue(profile["ans"])
        self.assertTrue(profile["ans_acpi"])
        self.assertTrue(profile["ans_dxe"])
        self.assertTrue(profile["ans_block_io"])
        self.assertTrue(profile["ans_preserve"])
        self.assertIn("internal-storage-windows)", self.build)
        self.assertIn("PcdAppleAnsPublishedInterrupt|996", self.dsc)
        self.assertIn("PcdAppleAnsExpectedPhysicalInterrupt|1155", self.dsc)

    def test_sealed_manifest_reports_the_real_acpi_setting(self) -> None:
        """A sealed manifest that lied about this would defeat the boot contract.

        The boot-artifact validator compares the manifest against what m1n1
        will do, so a hardcoded False here would let a publishing build ship
        under a manifest claiming it publishes nothing.
        """
        self.assertIn('"ans_acpi": enabled(ans_acpi),', self.build)
        self.assertIn(
            '"PcdAppleAnsPublishAcpiDevice": enabled(ans_acpi),', self.build
        )

    def test_fdf_embeds_real_ans_driver_only_for_the_storage_profile(self) -> None:
        # The guarded INF moved into AppleSiliconFvMain.fdf.inc, so check the
        # machine still selects the guarded slot rather than the unconditional
        # one. test_fdf_fvmain_expansion.py proves the include still resolves to
        # the same module list this platform had before the hoist.
        self.assertIn("DEFINE PLATFORM_NAND_SLOT = LATE", self.fdf)
        include = (
            Path(__file__).resolve().parents[1]
            / "Silicon/Apple/AppleSiliconPkg/AppleSiliconFvMain.fdf.inc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "!if $(NTASI_ENABLE_ANS) == TRUE\n"
            "  INF AppleSiliconPkg/Drivers/AppleNANDStorageDxe/"
            "AppleNANDStorageDxe.inf\n"
            "!endif",
            include,
        )
        self.assertNotIn("AppleANS2Dxe", self.fdf)

    def test_j813_pmgr_expectations_match_the_official_adt(self) -> None:
        expected = {
            "PcdAppleAnsPmgrResetBase": "0x380700300",
            "PcdAppleAnsPmgrApcieStBase": "0x380700410",
            "PcdAppleAnsPmgrApcieStSysBase": "0x380700520",
            "PcdAppleAnsPmgrApcieSt1SysBase": "0",
        }
        for pcd, value in expected.items():
            self.assertIn(f"{pcd}|{value}", self.dsc)

    def test_reserved_physical_interrupt_is_never_published_directly(self) -> None:
        """1155 itself must never reach Windows as a GSIV.

        It sits in GIC's reserved 1024..4095 range, so a devnode naming it
        comes up problem=12 (CM_PROB_NO_VALID_LOG_CONFIG) with its driver never
        loaded.  It is published under alias 996 instead; see the paired
        assertions in test_windows_profile_publishes_acpi_with_the_alias_pair.
        """
        self.assertIn("interrupt[4] == 1155", self.dsc)
        self.assertNotIn("PcdAppleAnsPublishedInterrupt|1155", self.dsc)
        # The DSC default stays closed; only a profile opts in.
        self.assertIn("DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE", self.dsc)

    def test_live_adt_domain_names_cover_t602x_and_t8142(self) -> None:
        self.assertIn("AppleAnsPmgrSelectDomain", self.pmgr)
        self.assertIn('"ANS2",\n                          "ANS"', self.driver)
        self.assertIn('"APCIE_ST_SYS",\n                    "APCIE_SYS_ST"', self.driver)
        self.assertIn("FourthDomainExpected != 0", self.driver)

    def test_t8142_omits_absent_linear_sq_control(self) -> None:
        self.assertIn("bool linear_sq_ctrl_present;", self.nvme_core_header)
        self.assertIn("ntasi_ans_hw_t8142", self.nvme_core_header)
        self.assertIn(
            ".linear_sq_ctrl_present = false,", self.nvme_core
        )
        self.assertIn(
            ".prp_null_check_ctrl_present = false,", self.nvme_core
        )
        self.assertIn(
            ".max_pend_cmds_ctrl_present = false,", self.nvme_core
        )
        self.assertIn(
            ".secure_io_queue_registers = true,", self.nvme_core
        )
        self.assertIn(
            "if (hw->linear_sq_ctrl_present)", self.nvme_controller
        )
        self.assertIn(
            "#if defined (SILICON_PLATFORM) && (SILICON_PLATFORM == 8142)",
            self.nvme_controller,
        )
        self.assertNotIn("T8142 exposes this control as write-only", self.nvme_controller)
        self.assertIn(
            "#if !defined (SILICON_PLATFORM) || (SILICON_PLATFORM != 8142)",
            self.nvme_controller,
        )

    def test_nvmmu_tcb_matches_the_submitted_opcode(self) -> None:
        self.assertIn("tcb->opcode = sqe->opcode;", self.nvme_core)
        self.assertNotIn("tcb->opcode = 0;", self.nvme_core)

    def test_t8142_registers_secure_io_queues_before_controller_enable(self) -> None:
        self.assertIn("NTASI_ANS_REG_T8142_IOQA", self.nvme_core_header)
        self.assertIn("if (hw->secure_io_queue_registers)", self.nvme_controller)
        ioqa = self.nvme_controller.index("NTASI_ANS_REG_T8142_IOQA")
        iocq = self.nvme_controller.index("NTASI_ANS_REG_T8142_IOCQ_ADDR")
        iosq = self.nvme_controller.index("NTASI_ANS_REG_T8142_IOSQ_ADDR")
        enable = self.nvme_controller.index("ntasi_ans_cc_config() | NTASI_ANS_CC_EN")
        self.assertLess(ioqa, iocq)
        self.assertLess(iocq, iosq)
        self.assertLess(iosq, enable)

    def test_linear_io_uses_tags_after_the_admin_reservation(self) -> None:
        self.assertIn(
            "(uint8_t)controller->hw->admin_queue_depth", self.nvme_controller
        )

    def test_j813_selects_the_t8142_nvme_contract(self) -> None:
        self.assertIn(
            'PropertyContains (RootNode, "compatible", "j813")', self.driver
        )
        self.assertIn(
            "T8142 ? &ntasi_ans_hw_t8142 : &ntasi_ans_hw_t8103", self.driver
        )
        self.assertIn("AppleANS: NVMe MMIO read", self.driver)
        self.assertIn("AppleANS: NVMe MMIO write", self.driver)

    def test_j813_routes_standard_nvme_registers_through_secure_bar(self) -> None:
        self.assertIn(
            "#if defined (SILICON_PLATFORM) && (SILICON_PLATFORM == 8142)",
            self.driver,
        )
        self.assertIn(
            "T8142 = TRUE;",
            self.driver,
        )
        self.assertIn(
            "SecureNvmeBar = T8142;",
            self.driver,
        )
        self.assertIn(
            "dt_node_reg (AnsNode, 9, &NvmeStandardBase, &NvmeStandardSize)",
            self.driver,
        )
        self.assertIn(
            "Offset <= NTASI_ANS_REG_DB_IOCQ", self.driver
        )
        self.assertIn(
            "Device->NvmeStandardBase + NTASI_ANS_REG_CAP", self.driver
        )
        self.assertIn(
            "Offset >= NTASI_ANS_REG_T8142_IOSQ_ADDR", self.driver
        )
        self.assertIn(
            "Offset <= (NTASI_ANS_REG_T8142_IOQA + sizeof (UINT32))", self.driver
        )
        self.assertIn("AppleANS: NVMe CAP=", self.driver)

    def test_internal_storage_omits_interactive_mtp_survey(self) -> None:
        self.assertIn("mtp_hid_build=FALSE", self.build)
        self.assertIn(
            '"BLD_*_MTP_HID_BUILD=$mtp_hid_build"', self.build
        )
        # The guard itself now lives in AppleSiliconFvMain.fdf.inc; the machine
        # opts into it by selecting BUILD_FLAG rather than an unconditional TRUE.
        self.assertIn("DEFINE PLATFORM_ENABLE_MTP_HID = BUILD_FLAG", self.fdf)

    def test_successful_storage_probe_pins_the_internal_partition_map(self) -> None:
        success = self.storage_probe.index("if (!EFI_ERROR (Status))")
        dead_loop = self.storage_probe.index("CpuDeadLoop ();", success)
        fallback = self.storage_probe.index(
            "gEfiBlockIoProtocolGuid", dead_loop
        )
        self.assertLess(success, dead_loop)
        self.assertLess(dead_loop, fallback)
        self.assertIn(
            "READ ONLY | INTERNAL APPLE ANS | PARTITION MAP COMPLETE",
            self.storage_probe,
        )

    def test_storage_probe_can_validate_the_standard_partition_path(self) -> None:
        self.assertIn(
            "PublishStandardBlockIo = PcdGetBool (PcdAppleAnsPublishBlockIo);",
            self.storage_probe,
        )
        self.assertIn("BlockIo == DiagnosticBlockIo", self.storage_probe)
        self.assertIn("LogicalPartitionCount++", self.storage_probe)
        self.assertIn("gEfiSimpleFileSystemProtocolGuid", self.storage_probe)
        self.assertIn(
            "MU BLOCK I/O + PARTITIONDXE READY", self.storage_probe
        )

    def test_storage_dashboard_replaces_bringup_noise_and_fits_one_panel(self) -> None:
        clear = self.storage_probe.index("ProbeDisplayClear ();", 5000)
        banner = self.storage_probe.index("PrintProbeBanner (", clear)
        self.assertLess(clear, banner)
        self.assertNotIn(
            'L"        LBA %lu .. %lu  |  attrs 0x%lx\\r\\n"',
            self.storage_probe,
        )


if __name__ == "__main__":
    unittest.main()

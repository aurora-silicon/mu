#!/usr/bin/env python3
"""Build profiles for the J813 / Mac17,3 M5 MacBook Air."""

PROFILES = {
    "uefi-shell-aic": {
        "bootable": True,
        "aic": True,
        "description": "Native Apple AIC firmware for the first visible UEFI shell gate",
    },
    "internal-storage": {
        "bootable": True,
        "aic": True,
        "ans": True,
        "ans_acpi": False,
        "ans_dxe": True,
        "ans_block_io": True,
        "ans_preserve": True,
        "profile_abi": "ntasi.j813.internal-storage.v1",
        "description": (
            "J813 native-AIC UEFI shell with firmware-owned internal Apple "
            "ANS/NVMe Block I/O; Windows ACPI publication remains withheld "
            "until the 1155 interrupt has a legal GSIV/ALI2 contract."
        ),
    },
    "internal-storage-windows": {
        "bootable": True,
        "aic": True,
        "ans": True,
        "ans_acpi": True,
        "ans_dxe": True,
        "ans_block_io": True,
        "ans_preserve": True,
        "profile_abi": "ntasi.j813.internal-storage-windows.v1",
        "description": (
            "internal-storage plus the \\_SB.ANS0 ACPI device, so Windows can "
            "bind a storage driver to internal Apple ANS/NVMe. The GSIV "
            "contract the profile above waits for now exists, but not as "
            "ALI2: J813 builds no CSRT and never loads the AIC HAL extension, "
            "so the guest lives on m1n1's emulated GICv3 carrier and m1n1 "
            "does the renumbering. ANS publishes GSIV 996 for physical AIC "
            "line 1155, which must stay equal to the {published, physical} "
            "pair in m1n1's hv_aic_aliases_t8142. Kept separate from "
            "internal-storage so the firmware-owned boot path stays available "
            "to A/B against."
        ),
    },
}

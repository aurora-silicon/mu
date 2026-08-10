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
}

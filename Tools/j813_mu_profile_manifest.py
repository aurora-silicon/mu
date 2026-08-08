#!/usr/bin/env python3
"""Build profiles for the J813 / Mac17,3 M5 MacBook Air."""

PROFILES = {
    "uefi-shell-aic": {
        "bootable": True,
        "aic": True,
        "description": "Native Apple AIC firmware for the first visible UEFI shell gate",
    },
}

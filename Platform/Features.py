# @file
# What a firmware build contains, as a set of features you can toggle.
#
# This replaces thirty named profiles. Those were every combination anyone had
# ever wanted, frozen: "ans-gpu-wireless-no-xhc2", "internal-storage-dcpirq",
# "gpu-noacpi-no-wireless". Adding one variable meant adding a name, and the
# names had to be kept identical in the build script, the sealing tool and
# auroradbg. There is no such thing as a profile now -- a build is a device and
# the features that are on.
#
#     Tools/mu-build j414s
#     Tools/mu-build j414s --without wireless
#     Tools/mu-build j414s --with media --without gpu-acpi
#
# The defaults are the configuration that boots. A flag moves one variable.
#
# Copyright (c) Aurora Silicon.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

from __future__ import annotations

#
# Every feature, the -D it becomes, and how its value is spelled. "bool" is
# TRUE/FALSE, "flag" is 1/0 -- the two spellings the .dsc files already use.
#
FEATURES = {
    "ans":                ("BLD_*_NTASI_ENABLE_ANS", "bool"),
    "ans-acpi":           ("BLD_*_NTASI_ANS_PUBLISH_ACPI", "bool"),
    "ans-dxe":            ("BLD_*_NTASI_ANS_DXE_BRINGUP", "bool"),
    "ans-block-io":       ("BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO", "bool"),
    "ans-preserve":       ("BLD_*_NTASI_ANS_PRESERVE_FOR_OS", "bool"),
    "gpu":                ("BLD_*_NTASI_GPU_RESOURCE_PROFILE", "flag"),
    "gpu-acpi":           ("BLD_*_NTASI_ENABLE_GPU_ACPI_PUBLICATION", "flag"),
    "display-interrupts": ("BLD_*_NTASI_ENABLE_DISPLAY_INTERRUPTS", "flag"),
    "wireless":           ("BLD_*_NTASI_ENABLE_WIRELESS_DART_HANDOFF", "flag"),
    "xhc2":               ("BLD_*_NTASI_ENABLE_XHC2", "flag"),
    "mca":                ("BLD_*_NTASI_ENABLE_MCA_PUBLICATION", "flag"),
    "aop":                ("BLD_*_NTASI_ENABLE_AOP_PUBLICATION", "flag"),
    "isp":                ("BLD_*_NTASI_ENABLE_ISP_PUBLICATION", "flag"),
    "battery":            ("BLD_*_NTASI_ENABLE_BATTERY_PUBLICATION", "flag"),
}

# Shorthand that expands to several features. "media" was the only one the
# profiles had, and splitting it apart was deliberate -- the three devices are
# independent -- so it stays a shorthand rather than a define.
GROUPS = {
    "media": ("mca", "aop", "isp"),
    "storage": ("ans", "ans-acpi", "ans-dxe", "ans-block-io", "ans-preserve"),
}

#
# Values that are not on/off. Each is (define, how-to-compute).
#
def _usb3_pipe_mask(on):
    # The left port is the boot port. With XHC2 published, the proven mask is
    # the right port; with it hidden, phase 1 isolates the left.
    return "0x4" if "xhc2" in on else "0x2"


DERIVED = {
    "BLD_*_NTASI_GPU_ACPI_HID": lambda on: "24" if "gpu-acpi" in on else "23",
    "BLD_*_NTASI_USB3_PIPE_SWITCH_PORT_MASK": _usb3_pipe_mask,
    # A routed mux switch is only ever correct on a port m1n1 has brought a
    # tunnel up on, so it is off unless something asks. 0x0 makes the Mu driver
    # a strict no-op.
    "BLD_*_NTASI_USB4_ROUTED_PIPE_SWITCH_PORT_MASK": lambda on: "0x0",
}

#
# One entry per machine. `on` is what a plain `mu-build <device>` produces: the
# configuration known to boot. Everything else is off.
#
DEVICES = {
    "j414s": {
        "platform": "MacBookProEarly2023",
        "description": "MacBook Pro (14/16-inch, M2 Pro, 2023)",
        "fd": "MACBOOKPROEARLY2023_EFI.fd",
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2", "battery"),
    },
    "j813": {
        "platform": "MacBookAir2026",
        "description": "MacBook Air (M5, 2026)",
        "fd": "J813MACBOOKAIR2026_EFI.fd",
        # J813 builds no CSRT and never loads the AIC HAL extension, so the
        # wireless, GPU and USB-host work has no counterpart here yet.
        "on": ("storage", "battery"),
    },
}


class UnknownFeature(ValueError):
    pass


def expand(names) -> set[str]:
    out = set()
    for name in names:
        if name in GROUPS:
            out.update(GROUPS[name])
        elif name in FEATURES:
            out.add(name)
        else:
            known = ", ".join(sorted(set(FEATURES) | set(GROUPS)))
            raise UnknownFeature(f"unknown feature {name!r}; known: {known}")
    return out


def resolve(device: str, with_=(), without=()) -> set[str]:
    """Which features are on for this build."""
    try:
        spec = DEVICES[device]
    except KeyError:
        raise ValueError(
            f"unknown device {device!r}; known: {', '.join(sorted(DEVICES))}") from None
    return (expand(spec["on"]) | expand(with_)) - expand(without)


def defines(device: str, with_=(), without=()) -> list[tuple[str, str]]:
    """(define, value) for every feature, on or off, plus the derived ones."""
    on = resolve(device, with_, without)
    out = []
    for name, (define, kind) in FEATURES.items():
        enabled = name in on
        out.append((define, ("TRUE" if enabled else "FALSE") if kind == "bool"
                    else ("1" if enabled else "0")))
    for define, compute in DERIVED.items():
        out.append((define, compute(on)))
    return sorted(out)


def summary(device: str, with_=(), without=()) -> str:
    on = resolve(device, with_, without)
    off = set(FEATURES) - on
    return (f"{device} ({DEVICES[device]['description']})\n"
            f"  on:  {' '.join(sorted(on)) or '(nothing)'}\n"
            f"  off: {' '.join(sorted(off)) or '(nothing)'}")


def platform(device: str) -> dict:
    """Where this device's package lives and what its firmware is called."""
    try:
        spec = DEVICES[device]
    except KeyError:
        raise ValueError(
            f"unknown device {device!r}; known: {', '.join(sorted(DEVICES))}") from None
    return {
        "pkg": f"{spec['platform']}Pkg",
        "platform": spec["platform"],
        "fd": spec["fd"],
        "description": spec["description"],
    }


def slug(device: str, with_=(), without=()) -> str:
    """A short, stable directory name for one configuration of one device.

    The device alone when nothing is overridden, so the common case reads as
    `build/j414s/<commit>/`. Overrides append in sorted order, so the same
    request always lands in the same place.
    """
    parts = [device]
    parts += [f"+{name}" for name in sorted(expand(with_))]
    parts += [f"-{name}" for name in sorted(expand(without))]
    return "".join(parts)

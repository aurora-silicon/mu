# @file
# The build profiles, in one place.
#
# WHY THIS FILE EXISTS
#
# These thirty profiles were written out twice: as `profile_values` in
# Platform/MacBookProEarly2023Pkg/PlatformBuild.py, which turns them into
# -D NTASI_* defines, and as `PROFILES` in Tools/mu_profile_manifest.py, which
# seals a built image and later re-checks one. Fourteen fields across thirty
# profiles is four hundred and twenty pairs held equal by hand.
#
# They did agree -- the only two differences were `24` against `NTAS0024` for
# the same GPU _HID. But nothing kept them agreeing, and the failure mode is
# quiet: the manifest would attest a build against an expectation the build
# never had.
#
# So the table lives here, the defaulting lives here, and both sides import it.
#
# ADDING A MACHINE
#
# Add a key to PROFILES and, if it seals images, to MANIFEST. The defaults
# cascade and the define mapping follow from that.
#
# Copyright (c) Aurora Silicon.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

J414S = {
    "baseline": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "0", "wireless": "0"},
    "ans": {"ans": "TRUE", "gpu": "0", "wireless": "0", "ans_acpi": "TRUE"},
    # Firmware-owned internal boot: attach to iBoot's live ANS without
    # resetting it, publish read-only Block I/O for the ESP, then keep
    # the live RTKit/SART/reserved-buffer state for AppleNvme's warm
    # adoption path at ExitBootServices.
    "internal-storage": {
        "ans": "TRUE", "gpu": "1", "wireless": "1",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        # One-boot WDDM bring-up selector. Keep the sealed profile
        # name and every storage/handoff bit unchanged; only publish
        # the alternate HID bound by AppleAgxWddm.
        "gpu_acpi_hid": "24",
    },
    # Exact copy of internal-storage plus the five DCP mailbox/DART
    # vectors, which is the only variable. Without them the display
    # side reports no completion source and the WDDM miniport falls
    # back to a full-surface CPU copy per present; with them D589
    # swap-complete can drive real flips. Separate from
    # internal-storage because an unroutable grant costs the whole
    # device, and internal-storage has to keep working.
    "internal-storage-dcpirq": {
        "ans": "TRUE", "gpu": "1", "wireless": "1",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        "gpu_acpi_hid": "24",
        "display_interrupts": "1",
    },
    # Exact copy of internal-storage except NTAS0023 publication is
    # hard-disabled. Keep ANS live handoff, Block I/O, wireless, GPU
    # carveout reservation, and the power sequence unchanged; only
    # the Windows-visible AppleAgxGpu devnode is omitted.
    "internal-storage-gpu-noacpi": {
        "ans": "TRUE", "gpu": "1", "wireless": "1",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        "gpu_acpi": "0",
    },
    # internal-storage-gpu-noacpi with wireless additionally off:
    # same ANS live handoff / Block I/O / GPU carveout reservation,
    # NTAS0023 still withheld, and no DRT0/BCM4388 publication or
    # wireless SID-1 rails. Added 2026-08-14 for driver bring-up
    # boots that must not co-run the wireless stack.
    "internal-storage-gpu-noacpi-no-wireless": {
        "ans": "TRUE", "gpu": "1", "wireless": "0",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        "gpu_acpi": "0",
    },
    # `internal-storage` plus the ROUTED USB4 PIPE opt-in for ATC
    # port 1 (the left-front receptacle, mask bit 1 = 0x2). Everything
    # else is identical, including usb3_pipe_mask, which stays at its
    # 0x4 default -- the right-port direct-USB3 link carries this
    # machine's Ethernet and SSH and must not move.
    #
    # The two masks address the SAME pipehandler register with
    # different values (0x08 direct USB3 vs 0x11 routed), so they must
    # never claim the same port; the manifest asserts that for every
    # profile. Setting the bit does not by itself switch anything:
    # AtcPhyFinishDeferredUsb4Switch additionally requires a
    # powered/out-of-reset PHY and USB4/TBT-crossbarred lanes.
    "internal-storage-usb4": {
        "ans": "TRUE", "gpu": "1", "wireless": "1",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        "usb4_routed_pipe_mask": "0x2",
    },
    # Single-variable control for the BUGCODE_USB3_DRIVER 0x144
    # investigation: byte-for-byte the same FFS set as "ans" (the
    # AppleNANDStorageDxe module is still in the FV) but NTAS2003 is
    # never published, so Windows never builds a devnode for it and
    # its PnP arbiter never allocates resources for it.
    "ans-noacpi": {"ans": "TRUE", "gpu": "0", "wireless": "0", "ans_acpi": "FALSE"},
    # 2026-08-01: the INVERSE of ans-noacpi, and the one combination no
    # profile had.  Mu's AppleNANDStorageDxe boots ANS and then halts it
    # at ExitBootServices, clearing the SART entries it armed -- so
    # Windows inherits a coprocessor that was booted, stopped, and
    # stripped of its DMA grants.  That is why BOOT_STATUS reads OK
    # while the core is halted and 0 of 16 SART entries are armed, and
    # neither Linux nor m1n1 ever clears a SART entry.  ans=FALSE keeps
    # the DXE driver out of the boot entirely so nothing quiesces ANS,
    # while ans_acpi=TRUE still publishes NTAS2003 so the Windows driver
    # loads and can take its WAKE path against a live coprocessor.
    "ans-live": {"ans": "FALSE", "gpu": "1", "wireless": "1",
                 "ans_acpi": "TRUE", "battery": "1"},
    "gpu": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "0"},
    "ans-gpu": {"ans": "TRUE", "gpu": "1", "wireless": "0", "ans_acpi": "TRUE"},
    "ans-gpu-usb3-dual": {"ans": "TRUE", "gpu": "1", "wireless": "0",
                            "ans_acpi": "TRUE", "usb3_pipe_mask": "0x6"},
    "gpu-no-xhc2": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "0", "xhc2": "0"},
    "ans-gpu-no-xhc2": {"ans": "TRUE", "gpu": "1", "wireless": "0", "ans_acpi": "TRUE", "xhc2": "0"},
    "wireless": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "0", "wireless": "1"},
    "gpu-wireless": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "1"},
    "gpu-wireless-usb3-dual": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1",
                                 "wireless": "1", "usb3_pipe_mask": "0x6"},
    "gpu-wireless-no-xhc2": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "1", "xhc2": "0"},
    # 2026-08-01: gpu_acpi was briefly pinned "0" to isolate the GPU
    # driver, on the belief that AppleAgxGpu 0.6.0.0 had retired the
    # GPX PS_MIN precondition and was powering the GPU domain for the
    # first time. That premise was wrong: the package on the disk was
    # 0.5.0.0 -- 0.6.0.0 had never been built, let alone deployed -- so
    # the isolation run tested a driver that still refused D0Entry at
    # stage 10 either way, and it hung rather than bugchecking. The pin
    # is removed; gpu_acpi inherits from "gpu" again.
    "ans-gpu-wireless": {"ans": "TRUE", "gpu": "1", "wireless": "1", "ans_acpi": "TRUE",
                         # Battery (NTAS0053) added 2026-07-31: no GSIV, no memory
                         # window, read-only SMC access via SMCG's device interface.
                         "battery": "1"},
    "ans-gpu-wireless-usb3-dual": {"ans": "TRUE", "gpu": "1", "wireless": "1",
                                     "ans_acpi": "TRUE", "battery": "1",
                                     "usb3_pipe_mask": "0x6"},
    "ans-gpu-wireless-no-xhc2": {"ans": "TRUE", "gpu": "1", "wireless": "1", "ans_acpi": "TRUE",
                                   "battery": "1", "xhc2": "0"},
    # 2026-08-02: ans-gpu-wireless with ANS never published to Windows.
    #
    # ans STAYS TRUE on purpose.  "ANS off" cannot mean ans=FALSE here:
    # that skips Mu's ANS DXE entirely, which is the only thing that
    # quiesces the coprocessor iBoot left running, and it also leaves
    # the ps_ans2 domain unpowered.  A boot on the "ans-live" profile
    # (ans=FALSE, ans_acpi=TRUE) proved what that costs -- AppleNvme
    # touched the SART at 0x34bc50010 with the domain down and took a
    # synchronous external abort (ESR 0x92000010, DFSC 0x10) that
    # killed the boot.
    #
    # ans_acpi=FALSE is the correct lever: the ANS hardware is brought
    # up and quiesced exactly as in the known-good profile, but NTAS2003
    # is never published, so Windows never builds a devnode, never
    # starts AppleNvme, and no driver can touch ANS at all.  Same FFS
    # set as ans-gpu-wireless -- AppleNANDStorageDxe is still in the FV,
    # and neither gpu nor wireless adds a module -- so the count is
    # unchanged at 88.
    "ans-noacpi-gpu-wireless": {"ans": "TRUE", "gpu": "1", "wireless": "1",
                                "ans_acpi": "FALSE", "battery": "1"},
    # 2026-08-01: ans-gpu-wireless MINUS the GPU, added because no
    # existing profile was genuinely GPU-free.  "ans-gpu-wireless" with
    # NTAS0023 publication turned off is NOT that: it still builds with
    # NTASI_GPU_RESOURCE_PROFILE=1, so it carves the GPU
    # reservations out of the memory map while publishing no device to
    # own them.  Windows then boots against a map with holes for a
    # device that does not exist, and dies before the desktop.  Here
    # gpu=0 turns BOTH the resource profile and the ACPI publication
    # off, so nothing GPU-related is programmed at all.
    "ans-wireless": {"ans": "TRUE", "gpu": "0", "wireless": "1",
                     "ans_acpi": "TRUE", "battery": "1"},
    "media": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "0", "wireless": "0", "media": "1"},
    # internal-storage PLUS AOPA (NTAS0081): the internal microphone
    # array, with AOP publication as the ONLY variable against the
    # proven internal-storage baseline.
    #
    # Derived from internal-storage rather than from a minimal base
    # because J414s boots Windows off the internal NVMe -- ANS DXE,
    # ACPI, Block I/O and the live handoff are load-bearing just to
    # reach an OS, and the GPU is mandatory on this target. A minimal
    # aop-only profile was refused by auroradbg's require_safe_profile()
    # before it could be built, correctly: a firmware that cannot reach
    # Windows cannot report what the driver measured.
    #
    # Adds no CSRT byte relative to internal-storage (AIC 631 is below
    # the carrier's 1019 limit and is published identity mapped) and no
    # pmgr_east window, so no Code 12 conflict with KBL0. Must stay in
    # step with the same key in Tools/mu_profile_manifest.py.
    "internal-storage-aop-mic": {
        "ans": "TRUE", "gpu": "1", "wireless": "1",
        "ans_acpi": "TRUE", "ans_dxe": "TRUE",
        "ans_block_io": "TRUE", "ans_preserve": "TRUE",
        "aop": "1",
    },
    # Everything at once. Must stay in step with the same key in
    # Tools/mu_profile_manifest.py PROFILES -- this dict is the
    # one that reaches the compiler.
    "ans-gpu-wireless-media": {"ans": "TRUE", "ans_acpi": "TRUE", "gpu": "1", "wireless": "1", "media": "1"},
    # Single-variable control for NTAS0023, exactly as ans-noacpi is for
    # NTAS2003: the GPU carveouts are still reserved in the GCD and all
    # of the NTASI_GPU_RESOURCE_PROFILE code is still compiled in,
    # but the ACPI device is never published.
    #
    # ADDED 2026-07-31. Tools/mu_profile_manifest.py PROFILES has
    # carried "gpu-noacpi" and "media-gpu" since 3450262, but this dict
    # -- the one that actually reaches the compiler -- did not, so the
    # manifest advertised two profiles the builder rejected with
    # "NTASI_MU_PROFILE must be one of: ...". That is the same
    # dual-source-of-truth split that produced a manifest claiming
    # publication was off while the firmware was built with it on. The
    # two dicts are now key-for-key in step; the test suite pins that.
    "gpu-noacpi": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "0", "gpu_acpi": "0"},
    "gpu-usb3-dual": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1",
                       "wireless": "0", "usb3_pipe_mask": "0x6"},
    "media-gpu": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "1", "wireless": "0", "media": "1"},
    # Battery: baseline plus BAT0 (NTAS0053) and nothing else. A single
    # variable on top of the only configuration currently known to boot
    # and stay up, exactly as "media" is. It is the cheapest experiment
    # in the set: the device claims no memory window and publishes no
    # interrupt, so unlike media it cannot take a resource away from a
    # devnode that already boots, and unlike gpu it reserves nothing.
    "battery": {"ans_acpi": "FALSE", "ans": "FALSE", "gpu": "0", "wireless": "0", "battery": "1"},
}
# Every profile that does not name media leaves it off. Written as a
# default rather than repeated in nine dicts so a profile added later
# cannot silently inherit an enabled media publication by omission.


#
# What the manifest records but the compiler never sees. Kept beside the build
# table rather than inside it, so the comments above stay attached to the flags
# they explain.
#
J414S_MANIFEST = {'baseline': {'profile_abi': 'ntasi.j414s.windows.baseline.v1', 'expected_ffs_count': 87},
 'ans': {'profile_abi': 'ntasi.j414s.windows.ans-readonly.v1', 'expected_ffs_count': 88},
 'internal-storage': {'profile_abi': 'ntasi.j414s.windows.internal-storage-warm-handoff.v6',
                      'expected_ffs_count': 88},
 'internal-storage-dcpirq': {'profile_abi': 'ntasi.j414s.windows.internal-storage-dcpirq.v1',
                             'expected_ffs_count': 88},
 'internal-storage-aop-mic': {'profile_abi': 'ntasi.j414s.windows.internal-storage-aop-mic.v1',
                              'expected_ffs_count': 88},
 'internal-storage-gpu-noacpi': {'profile_abi': 'ntasi.j414s.windows.internal-storage-gpu-no-acpi-control.v1',
                                 'expected_ffs_count': 88},
 'internal-storage-gpu-noacpi-no-wireless': {'profile_abi': 'ntasi.j414s.windows.internal-storage-gpu-no-acpi-no-wireless.v1',
                                             'expected_ffs_count': 87},
 'internal-storage-usb4': {'profile_abi': 'ntasi.j414s.windows.internal-storage-usb4-routed.v1',
                           'expected_ffs_count': 88,
                           'usb4_routed_pipe_switch_port_mask': 2},
 'ans-noacpi': {'profile_abi': 'ntasi.j414s.windows.ans-driver-no-acpi-control.v1',
                'expected_ffs_count': 88},
 'gpu': {'profile_abi': 'ntasi.j414s.windows.gpu-resource-probe.v1', 'expected_ffs_count': 87},
 'ans-gpu': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-combined.v1',
             'expected_ffs_count': 88},
 'ans-gpu-usb3-dual': {'profile_abi': 'aurora.j414s.windows.ans-gpu-usb3-dual.v1',
                       'expected_ffs_count': 88,
                       'usb3_pipe_switch_port_mask': 6},
 'gpu-no-xhc2': {'profile_abi': 'ntasi.j414s.windows.gpu-no-xhc2.v1', 'expected_ffs_count': 87},
 'ans-gpu-no-xhc2': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-no-xhc2.v1',
                     'expected_ffs_count': 88},
 'wireless': {'profile_abi': 'ntasi.j414s.windows.wireless-handoff-v2-runtime-derived.v1',
              'expected_ffs_count': 87},
 'gpu-wireless': {'profile_abi': 'ntasi.j414s.windows.gpu-wireless-combined.v1',
                  'expected_ffs_count': 87},
 'gpu-wireless-usb3-dual': {'profile_abi': 'aurora.j414s.windows.gpu-wireless-usb3-dual.v1',
                            'expected_ffs_count': 87,
                            'usb3_pipe_switch_port_mask': 6},
 'gpu-wireless-no-xhc2': {'profile_abi': 'ntasi.j414s.windows.gpu-wireless-no-xhc2.v1',
                          'expected_ffs_count': 87},
 'ans-wireless': {'profile_abi': 'ntasi.j414s.windows.ans-wireless-battery.v1',
                  'expected_ffs_count': 88},
 'ans-live': {'profile_abi': 'ntasi.j414s.windows.ans-live-battery.v1',
              'expected_ffs_count': 87},
 'ans-gpu-wireless': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-wireless-battery.v1',
                      'expected_ffs_count': 88},
 'ans-gpu-wireless-usb3-dual': {'profile_abi': 'aurora.j414s.windows.ans-gpu-wireless-usb3-dual.v1',
                                'expected_ffs_count': 88,
                                'usb3_pipe_switch_port_mask': 6},
 'ans-gpu-wireless-no-xhc2': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-wireless-no-xhc2.v1',
                              'expected_ffs_count': 88},
 'ans-noacpi-gpu-wireless': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-wireless-no-acpi-control.v1',
                             'expected_ffs_count': 88},
 'ans-gpu-wireless-media': {'profile_abi': 'ntasi.j414s.windows.ans-gpu-wireless-media-combined.v1',
                            'expected_ffs_count': 88},
 'media': {'profile_abi': 'ntasi.j414s.windows.media-publication.v1', 'expected_ffs_count': 87},
 'media-gpu': {'profile_abi': 'ntasi.j414s.windows.media-gpu-combined.v1',
               'expected_ffs_count': 87},
 'gpu-noacpi': {'profile_abi': 'ntasi.j414s.windows.gpu-resource-no-acpi-control.v1',
                'expected_ffs_count': 87},
 'gpu-usb3-dual': {'profile_abi': 'aurora.j414s.windows.gpu-usb3-dual.v1',
                   'expected_ffs_count': 87,
                   'usb3_pipe_switch_port_mask': 6},
 'battery': {'profile_abi': 'ntasi.j414s.windows.battery-publication.v1',
             'expected_ffs_count': 87}}


#
# What a target builds. mu_profile_manifest.py keeps the sealing view of this
# (fd_size, images_verified); this is the half the build script needs.
#
PLATFORMS = {
    "j414s": {
        "pkg": "MacBookProEarly2023Pkg",
        "platform": "MacBookProEarly2023",
        "output": "m2-pro",
        "fd_name": "MACBOOKPROEARLY2023_EFI.fd",
    },
    "j813": {
        "pkg": "MacBookAir2026Pkg",
        "platform": "MacBookAir2026",
        "output": "m5",
        "fd_name": "J813MACBOOKAIR2026_EFI.fd",
    },
}

PROFILES = {
    "j414s": J414S,
}

MANIFEST = {
    "j414s": J414S_MANIFEST,
}

#
# Profile field -> build define. A field absent from here never reaches a build.
#
DEFINES = {
    "ans": "BLD_*_NTASI_ENABLE_ANS",
    "ans_acpi": "BLD_*_NTASI_ANS_PUBLISH_ACPI",
    "ans_dxe": "BLD_*_NTASI_ANS_DXE_BRINGUP",
    "ans_block_io": "BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO",
    "ans_preserve": "BLD_*_NTASI_ANS_PRESERVE_FOR_OS",
    "gpu": "BLD_*_NTASI_GPU_RESOURCE_PROFILE",
    "gpu_acpi": "BLD_*_NTASI_ENABLE_GPU_ACPI_PUBLICATION",
    "gpu_acpi_hid": "BLD_*_NTASI_GPU_ACPI_HID",
    "display_interrupts": "BLD_*_NTASI_ENABLE_DISPLAY_INTERRUPTS",
    "wireless": "BLD_*_NTASI_ENABLE_WIRELESS_DART_HANDOFF",
    "xhc2": "BLD_*_NTASI_ENABLE_XHC2",
    "usb3_pipe_mask": "BLD_*_NTASI_USB3_PIPE_SWITCH_PORT_MASK",
    "usb4_routed_pipe_mask": "BLD_*_NTASI_USB4_ROUTED_PIPE_SWITCH_PORT_MASK",
    "mca": "BLD_*_NTASI_ENABLE_MCA_PUBLICATION",
    "aop": "BLD_*_NTASI_ENABLE_AOP_PUBLICATION",
    "isp": "BLD_*_NTASI_ENABLE_ISP_PUBLICATION",
    "battery": "BLD_*_NTASI_ENABLE_BATTERY_PUBLICATION",
}


def _apply_defaults(profiles):
    for values in profiles.values():
        values.setdefault("xhc2", "1")
        # Phase 1 isolates the left boot-volume port while XHC2 is hidden.
        # Preserve the proven right-port semantics for every profile that
        # still publishes XHC2. The value is also sealed in the manifest.
        values.setdefault(
            "usb3_pipe_mask", "0x4" if values["xhc2"] == "1" else "0x2"
        )
        # Routed (USB4/Thunderbolt) PIPE switch. Defaulted OFF rather than
        # derived from anything: a routed switch is only ever correct on a
        # port m1n1 has actually brought a tunnel up on, and 0x0 makes the
        # Mu driver a strict no-op, so a profile added later cannot inherit
        # a routed mux switch by omission.
        values.setdefault("usb4_routed_pipe_mask", "0x0")
        # Media publication is per-device: three independent compiler flags.
        # "media": "1" is shorthand for all three and is expanded here, so
        # the existing media / media-gpu / ans-gpu-wireless-media profiles
        # keep exactly their previous meaning while aop-mic can select one.
        # Only the three per-device keys reach the compiler; there is no
        # umbrella define, because a stale one is what would silently
        # re-couple the devices after they were split apart.
        _media_shorthand = values.pop("media", "0")
        for _device in ("mca", "aop", "isp"):
            values.setdefault(_device, _media_shorthand)
        values.setdefault("ans_dxe", "FALSE")
        values.setdefault("ans_block_io", "FALSE")
        values.setdefault("ans_preserve", "FALSE")
        # Same rule for the battery devnode: a profile that does not name
        # it still gets it. Battery publication is unconditional in
        # AcpiPlatform.c (no #if gate), so every profile publishes BAT0.
        # Written as a default so a profile added later inherits this.
        values.setdefault("battery", "1")
        # NTAS0023 publication tracks the gpu flag unless a profile says
        # otherwise, mirroring ans_acpi/ans. Defaulted rather than repeated
        # so a profile added later cannot inherit a publication by
        # omission -- and so a future "gpu-noacpi" control can decouple the
        # two by naming gpu_acpi explicitly, exactly as ans-noacpi does.
        values.setdefault("gpu_acpi", values["gpu"])
        # The GPU resources and SSDT shape are shared by the Vulkan KMD
        # (NTAS0023) and WDDM KMD (NTAS0024).  Keep the identity a closed,
        # profile-owned compiler choice: arbitrary strings must never
        # reach AML and a missing selector preserves the established HID.
        values.setdefault("gpu_acpi_hid", "23")
        # Fail closed: only a profile that names the DCP vectors gets them.
        values.setdefault("display_interrupts", "0")
        if values["gpu_acpi_hid"] not in ("23", "24"):
            raise ValueError(
                "gpu_acpi_hid must select NTAS0023 or NTAS0024, got: "
                + repr(values["gpu_acpi_hid"])
            )
        if values["ans_preserve"] == "TRUE" and not (
            values["ans"] == "TRUE"
            and values["ans_acpi"] == "TRUE"
            and values["ans_dxe"] == "TRUE"
            and values["ans_block_io"] == "TRUE"
        ):
            raise ValueError(
                "ANS live handoff requires the driver, ACPI publication, "
                "DXE bring-up, and Block I/O"
            )
    return profiles


def resolve(target, manifest=False):
    """Every profile for a target, defaults applied.

    manifest=True folds in the fields only the sealing tool cares about.
    """
    try:
        profiles = PROFILES[target]
    except KeyError:
        raise KeyError(
            f"unknown target {target!r}; known: {', '.join(sorted(PROFILES))}"
        ) from None
    resolved = _apply_defaults({name: dict(values) for name, values in profiles.items()})
    if manifest:
        for name, extra in MANIFEST.get(target, {}).items():
            resolved[name].update(extra)
    return resolved


def profile(target, name, manifest=False):
    # A machine can exist in PLATFORMS without build profiles -- j813 builds one
    # way and never reads NTASI_MU_PROFILE. Only "baseline" is meaningful there.
    if target not in PLATFORMS:
        raise KeyError(
            f"unknown target {target!r}; known: {', '.join(sorted(PLATFORMS))}")
    if target not in PROFILES:
        if name not in ("", "baseline"):
            raise ValueError(f"{target} has no build profiles; use 'baseline'")
        return {}
    profiles = resolve(target, manifest=manifest)
    if name not in profiles:
        raise ValueError(
            f"NTASI_MU_PROFILE must be one of: {', '.join(sorted(profiles))}"
        )
    return profiles[name]


def build_defines(target, name):
    """(define, value) pairs for this profile, in a stable order."""
    values = profile(target, name)
    return [(DEFINES[field], values[field]) for field in DEFINES if field in values]


#
# The sealing tool tests these fields for truthiness and indexes a dict with
# gpu_acpi_hid, so it needs bools and the NTAS00xx form rather than the build
# side's "TRUE"/"1" strings. "FALSE" is a non-empty string and therefore true,
# which is precisely the kind of disagreement two hand-kept copies produce.
#
_BOOL_FIELDS = (
    "ans", "ans_acpi", "ans_dxe", "ans_block_io", "ans_preserve",
    "gpu", "gpu_acpi", "display_interrupts", "wireless", "xhc2",
    "mca", "aop", "isp", "battery",
)


def _as_bool(value):
    if isinstance(value, bool):
        return value
    text = str(value).strip().upper()
    if text in ("TRUE", "1"):
        return True
    if text in ("FALSE", "0", ""):
        return False
    raise ValueError(f"not a boolean profile value: {value!r}")


def manifest_view(target):
    """The profile table in the shape Tools/mu_profile_manifest.py expects."""
    out = {}
    for name, values in resolve(target, manifest=True).items():
        view = dict(values)
        for field in _BOOL_FIELDS:
            if field in view:
                view[field] = _as_bool(view[field])
        hid = view.get("gpu_acpi_hid")
        if hid is not None and not str(hid).startswith("NTAS"):
            view["gpu_acpi_hid"] = f"NTAS{int(hid):04d}"
        for src, dst in (("usb3_pipe_mask", "usb3_pipe_switch_port_mask"),
                         ("usb4_routed_pipe_mask", "usb4_routed_pipe_switch_port_mask")):
            if src in view:
                view.setdefault(dst, int(str(view[src]), 0))
        # Derived, never authored: the build side deliberately has no umbrella
        # media define, because a stale one would silently re-couple three
        # devices that were split apart. The sealing tool kept a derived copy
        # for its own reporting, so reproduce it here rather than leaving a
        # third expansion of the same rule in that file.
        view["media"] = any(view.get(device) for device in ("mca", "aop", "isp"))
        out[name] = view
    return out


def platform(target):
    """Where a target's build lives. Used by Tools/build-windows-native.sh."""
    try:
        return PLATFORMS[target]
    except KeyError:
        raise KeyError(
            f"unknown target {target!r}; known: {', '.join(sorted(PLATFORMS))}"
        ) from None

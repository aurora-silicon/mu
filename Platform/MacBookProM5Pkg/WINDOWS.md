# J714s M5 Pro Windows profile

The `dev` branch preserves the hardware-tested J714 Windows tree from
`feat/x1n1-j714` plus the subsequent bring-up changes. `main` is a different,
older platform history; this branch does not replace it or other board defaults.

On Linux aarch64, initialize the pinned submodules and run:

```sh
git submodule update --init --recursive
Tools/build-j714-windows.sh
```

The helper uses `Tools/build-linux-native.sh` with the N1 RAM-boot profile.
See that script for toolchain and output overrides. It checks and applies the
already-versioned dependency patches from `Tools/j873-patches`; the pointer-sized
ARM exception stack alignment fix is also required by J714 above 4 GiB.
Dependency gitlinks stay pinned. A clean checkout therefore does not depend on
unpublished edits in a submodule.

Included work:

- Native AIC/FIQ ACPI and handoff, timer/idle kernel profile support, and an
  explicitly reserved monitor CPU. The normal shared platform defaults remain.
- Framebuffer metadata matching x1n1's iBoot BGRA8 format switch. This uses the
  existing framebuffer, not a Windows DCP display driver.
- USB host setup and USB installer boot selection; the RAM-boot wrapper keeps
  installer selection disabled. `J714_USB_INSTALLER` enables that separate path.
- ANS UEFI block support, a basic MTP UEFI keyboard driver and keyboard backlight
  code. Their presence does not qualify an unattended internal-disk installation
  or every UEFI menu/input path.
- SMC, NVMe, N1 and keyboard-backlight ACPI resources; expanded RAM descriptors
  excluding the reserved handoff regions.

The current tested Windows baseline uses 17 guest CPUs and one host monitor.
The N1 Windows transport has passed single-boot scan/association/DHCP/HTTP and a
Bluetooth local-radio query. General reconnect, power transitions, full-memory
stress, all-18-CPU operation and Windows installation remain unfinished. See
`AuroraSilicon:targets/j714s/windows/README.md` for per-driver qualification.

On 2026-10-07 the publication build passed and reproduced the exact 30 MiB Mu
firmware used by the N1 v25 hardware run:

```
J714MACBOOKPROM5_EFI.fd
58610d55607c42ff59037aba20de9e69f4e1279b6c1b3a119d249bd86a059303
```

No Windows image, vendor firmware, private credentials or signing material is
included. Build helpers do not flash or boot a machine. Use the matching
`aurora-silicon/x1n1:windows` transport and the J714 Windows driver profile;
other x1n1 resident contexts are not interchangeable.

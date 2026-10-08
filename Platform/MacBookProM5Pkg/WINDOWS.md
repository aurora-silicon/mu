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
- An opt-in ANS UEFI block frontend, MTP descriptor decoder and keyboard
  backlight code. The original RAM-boot profile does not enable these firmware
  input/storage paths. See the interactive candidate below.
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

## Interactive UEFI candidate — 2026-10-08

`Tools/build-j714-uefi.sh` builds a separate, **host-tested but not hardware-tested**
profile. It does not deploy anything. It enables the text boot menu, USB keyboard
driver, menu-scoped MTP keyboard candidate and firmware keyboard backlight.
The existing Windows RAM-boot helper keeps its existing feature selections.
Windows DCP resources remain disabled in the interactive profile.

The menu discovers readable filesystems afresh on each boot, so loader discovery
does not depend on persistent `BootOrder`. It offers:

| Loader | Menu entry |
| --- | --- |
| `\EFI\Microsoft\Boot\bootmgfw.efi` | Windows Boot Manager |
| `\EFI\Aurora\Recovery\bootaa64.efi` | Recovery, when separately provisioned |
| `\EFI\Aurora\WinPE\bootaa64.efi` | WinPE, when separately provisioned |
| `\EFI\BOOT\BOOTAA64.EFI` on USB | USB boot |
| The same removable path on other media | Local / RAM boot |

The firmware checks file presence; normal UEFI image loading performs validation.
The Recovery path is an EFI application contract, **not** automatic discovery of
Windows' NTFS `Winre.wim`. A usable Windows recovery loader and its BCD still need
provisioning after installation. No synthetic recovery entry, BCD modification,
NTFS writer, disk formatting or partition changes are performed by this menu.

A unique local Windows loader is preferred, then USB, WinPE and other local/RAM
media. Equally preferred volumes require a choice. Recovery never auto-boots.
Esc/F12 (or any key) during the two-second countdown opens the menu; arrows and
Enter select an entry, R refreshes media, and Esc returns to the existing firmware
policy. A returned loader stays in the menu instead of automatically retrying.
An existing `BootNext` retains precedence. Firmware-settings requests enter the
menu directly. Variable storage is still RAM-backed: requests do not gain reset
persistence from this change.

USB host controllers 1 and 2 use the existing host-granted PHY/xHCI paths; USB0
remains the proxy connection. The menu connects already-enumerated keyboard
handles, including later attachment while waiting for input, without ConnectAll.
This is not qualification of arbitrary-port hotplug or of reassigning the proxy
port. The StartImage boundary keeps timer events enabled for this exact built-in
menu, including when no USB controller was prepared, and restores the protected
timer state before starting a disk application such as Windows.

The internal keyboard uses the shared MTP framing/bootstrap/RTKit cores and
descriptor-driven boot-report translation. It feeds Mu's existing HidKeyboardDxe
for keyboard layouts, repeat and SimpleTextInput/Ex. It needs a stopped MTP helper
and matching x1n1 readiness ABI at `0x61f000d8` (`0x4d54504b00000001`); older hosts
return zero and the keyboard candidate does not touch MTP aliases. This cold-helper
precondition, real key delivery and Windows restart of MTP still need hardware
qualification. Trackpad firmware is not loaded. Physical Caps Lock LED output is
not implemented; firmware keyboard illumination uses the separate PWM driver.

Before starting a loader or returning, the menu disconnects its HID producer,
quiesces MTP, clears CPU RUN, drains stale FIFO data and unmaps its buffers. A
failed quiesce/stop/unmap retains DMA pages and refuses ownership transfer. Failed
teardown can therefore require a reboot; this is a candidate acceptance gate,
not a claim of finished Windows handoff. `J714_UEFI_MTP=FALSE` builds the USB-only
menu while leaving the native Windows keyboard driver unchanged.

**Internal SSD boot is not finished.** `J714_UEFI_ANS` remains false in this
profile. The existing read-only Block I/O frontend allocates new queues; native
ANS retains fixed queue addresses. Both the native-owner-to-Mu transfer and the
Mu-to-Windows transfer must reuse the correct reserved queue pages and establish
controller/RTKit ownership. Enabling the existing driver is not a solution to
that missing handoff. The separate Windows NVMe read result does not qualify
firmware SSD boot, writes, or installation.

Run `bash Platform/MacBookProM5Pkg/Tests/run-host-tests.sh` for ASan/UBSan tests
of actual menu discovery/selection/cleanup, MTP INIT/ACK/report translation and
failed-teardown DMA retention, plus the actual StartImage timer boundary.
Shared source provenance is in the MTP driver's `Shared/source-manifest.json`.
No hardware was accessed for this candidate.

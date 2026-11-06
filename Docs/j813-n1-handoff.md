# J813 N1 handoff from Aura to Windows

The N1 bus device is optional. Normal Mu profiles must not expose it. An
explicit N1 build profile may publish `ACPI\AURA8130` only after validating
Aura's `/chosen/aura,windows-n1-v1` record and current hardware state. The
record is an ownership contract from the trusted bootloader, not a signature
or proof of firmware authenticity.

## Failure model (before implementation)

- No opt-in, missing record, wrong byte count, incompatible version, unknown
  flags, wrong chip/board/profile, changed RID/SID or a changed resource must
  withhold the entire SSDT without reading hardware.
- The descriptor is byte encoded. Unaligned ADT properties must be decoded
  safely; malformed size fields cannot authorize out-of-bounds reads.
- Link-down must stop validation before endpoint configuration or BAR reads.
  Root/Control identity and BAR0 must match before the ROM stage is read.
- Any present function with bus mastering enabled, active ROM firmware, a
  protected/live DART stream, unsupported translation geometry or failed MMIO
  read must withhold the device. Mu must never reset or repair these states.
- Six exclusive, fixed resources must be emitted exactly as the Windows bus
  expects, with no translated addresses, seventh resource or generic PCI root.
  A generic PCI root/MCFG must not describe the same controller.
- The normal profile must retain its previous ACPI inventory. The handoff gate
  applies only to the SSDT with the dedicated OEM table ID, not to unrelated
  ACPI tables or boot devices.
- Host model and AML interpreter results establish descriptor/publication
  logic only. The opt-in profile must be tested through Aura -> Mu -> Windows
  on J813 before merge or any hardware-support claim.

## Descriptor version 1

Exactly 128 little-endian bytes:

| Offset | Field |
| --- | --- |
| 0 | u32 magic `0x4831574e` (`NW1H`) |
| 4 | u16 version 1; u16 size 128 |
| 8 | u32 flags 7: exclusive controller, trained link, cold ROM/clean DART |
| 12 | u32 chip `0x8142` |
| 16 | u32 physical board `0x24` |
| 20 | u32 firmware profile `0x32354776` (25G76) |
| 24 | u16 RIDs `0x100,0x101,0x102`; u16 shared SID 0 |
| 32 | Six u64 base/u64 length pairs, in the order below |

| Resource | Base | Length |
| --- | --- | --- |
| ECAM, buses 0–1 | `0x1cb0000000` | `0x200000` |
| APCIE DART | `0x488000000` | `0x20000` |
| Port | `0x488028000` | `0x4000` |
| PHY | `0x48f010000` | `0x1000` |
| PERST GPIO | `0x39a000300` | 4 |
| CPU BAR window | `0xb80000000` | `0x600000` |

Aura must leave Control BAR0 at bus `0x80000000` (CPU `0xb80000000`), memory
decode enabled, all endpoint bus mastering disabled, ROM stage 0 and SID0
disabled with TTBR0 clear. Mu only observes this state. Device firmware remains
the Windows bus driver's responsibility, including its separately provisioned
firmware digest.

## Profiles and ownership

`PcdAppleN1BusHandoff` defaults to `FALSE`, including the existing
`internal-storage-windows` profile. The `internal-storage-windows-n1` profile
sets it to `TRUE`; this enables validation, not unconditional publication.
Missing or rejected records withhold the whole N1 SSDT. No hardware reads are
issued when the profile is disabled, the property is missing, or the descriptor
or physical chip/board identity fails validation.

All census reads use Aura's guarded `N1RD32` HVC, with an exact acknowledgement,
version, status and 32-bit result check. There is no direct `MmioRead32` fallback.
An old resident without the service withholds the N1 table. The service ABI and
the hardware/offline validation limits are in `windows-runtime-forwarding.md`.

The J813 ACPI inventory contains no generic PCI root or MCFG. Its FV excludes
the generic PCI platform, host bridge and bus drivers. The PCI host bridge
library also returns no root bridges with the N1 PCD enabled, keeping generic
enumeration from rewriting the exclusive controller's BARs or DMA state if a
later FV enables that driver. A rejected handoff does not transfer ownership
back to generic PCI enumeration. Future PCI-root/MCFG additions must preserve
this exclusive ownership rule.

## Repeatable offline evidence

From the repository root, with Clang, ACPICA `iasl` and `acpiexec` installed:

```sh
python3 Tests/e2e/n1-handoff.py --output Build/N1-e2e-new
```

The output directory must not already exist. The runner compiles the production
validator with address/undefined-behavior sanitizers, exercises 21 handoff and
register-census scenarios, compiles the real SSDT twice, and evaluates its HID,
status, cache-coherency and resource objects in ACPICA. It independently checks
all six binary QWord resource descriptors, including fixed/exclusive flags,
zero translation, ranges and the final EndTag. Its JSON receipt records source
copies and hashes, commands, per-case MMIO traces, submodule revisions, compiled
AML, and interpreter logs. ACPICA 20260408 may report outstanding interpreter
cache allocations at process exit; the runner checks the evaluated values and
rejects any `AE_*` evaluation failure.

The full J813 firmware candidates can then be built separately:

```sh
sh Tools/build-j813-windows-native.sh internal-storage-windows
sh Tools/build-j813-windows-native.sh internal-storage-windows-n1
```

These host checks do not prove electrical link stability, Windows resource
arbitration, DMA isolation, interrupt delivery, power transitions, device
firmware startup or reset/drain recovery. Hardware qualification must retain
the Aura handoff log, Mu acceptance log, Windows resource inventory showing
exactly one owner, the driver-start/reset trace, and firmware/source hashes.
Test the default profile first, then the opt-in profile with absent/rejected
and valid handoffs. Do not merge the N1 path until the affected hardware has
passed that sequence.

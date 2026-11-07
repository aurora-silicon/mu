# J813 G17 firmware memory preservation

This optional Aura → Mu contract retains the eight live GPU firmware memory
spans for the whole EFI/Windows boot. It defaults off: no
`/chosen/aura,windows-g17-preserve-v1` property means no additional reservation or
verification event. A present invalid property stops the opt-in boot.

This is a memory-preservation admission contract. It does not create a GPU ACPI
device, start either ASC, change interrupts, reset hardware, write GPU firmware,
adopt or mirror UAT roots, or claim DMA is stopped. Preserved pages have no free
or reclaim path. A later native-driver protocol must establish ownership before
it may modify them.

## Wire record

The property is exactly 304 bytes, little endian. No C structure packing is used.
CRC32 is the IEEE reflected polynomial `0xedb88320`, initial/final XOR
`0xffffffff`, over all 304 bytes with bytes 104–107 zeroed. It detects stale or
corrupt transport data; it is not an authenticity mechanism.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | ASCII `AG17` |
| 4 | 2 | Version 1 |
| 6 | 2 | Length 304 |
| 8 | 4 | Chip `0x8142` |
| 12 | 4 | Board `0x24` |
| 16 | 4 | Eight regions |
| 20 | 4 | Flags 1, reservation-only |
| 24, 32 | 8 each | Guest RAM base, exclusive end |
| 40, 48 | 8 each | Installed DRAM base, exclusive end |
| 56 | 32 | `M5-RTKit-3255.160.4.release`, NUL padded |
| 88 | 16 | `Mac17,3`, NUL padded |
| 104 | 4 | CRC32 |
| 108 | 4 | Reserved zero |
| 112 | 192 | Eight 24-byte region entries |

Each region entry is tag:u32, classification:u32, base:u64, size:u64.
The ordered tags are:

| Tag | Live source | Additional admission bound |
| --- | --- | --- |
| 1 | SGX `gfx-handoff-base/size` | Nonzero |
| 2 | SGX `gfx-shared-l2-region-base/size` | Nonzero |
| 3 | SGX `gfx-shared-region-base/size` | At least `0x44000` |
| 4 | SGX `gpu-region-base/size` | Nonzero |
| 5 | SGX `gfx-data-base/size` and GFX ASC DATA | Exactly `0x134000` |
| 6 | SGX `gfx1-data-base/size` and GFX1 ASC DATA | Exactly `0x130000` |
| 7 | GFX ASC TEXT | Exactly `0x4c000` |
| 8 | GFX1 ASC TEXT | Exactly `0x48000` |

The SGX properties are separate 8-byte scalars. Each ASC must have exact
`__TEXT;__DATA` segment names and two 32-byte segment records. Only physical
addresses and sizes establish this revision's exclusions. IOVA, remap and unknown
segment words are not admitted as an ownership contract.

Mu independently compares the record with the copied ADT's chip/board/model and
all physical spans. Firmware-profile admission is Aura's assertion after its
bounded, guarded firmware TEXT checks; Mu does not dereference those pages. It
also binds guest bounds to BootArgs `phys_base` and `mem_size`, and installed DRAM
to `ALIGN_DOWN(phys_base, 4 GiB) + mem_size_actual` for supported BootArgs revisions
1–3. PCD system-memory bounds must agree with BootArgs.

Every span must be nonempty, aligned to 16 KiB, within installed DRAM and the
42-bit physical range, nonwrapping, and disjoint from the other seven. Class 1
means wholly inside guest RAM, class 2 wholly below, class 3 wholly above. A span
crossing a guest boundary is invalid.

## Lifetime and failure behavior

EarlySetup first bounds and validates the source ADT, binds the opt-in record to
that source and BootArgs, and checks the actual fixed copy destinations, runtime
FD, framebuffer, and assembly-derived UEFI workspace. This runs before its two
copies and before the later stack clear. The legacy near-FD workspace fallback
uses the assembly's exact exclusive top of `FdBase+1`. Failed early admission
halts directly. Aura's raw-payload exclusion protects the temporary relocation
stack that runs before this C gate. The generic boot ABI still supplies the
source BootArgs/ADT pointers; this slice does not create a fault-recoverable
physical-address reader in SEC.

Before DXE, the production T8142 memory library preflights all eight ranges
against resource/allocation HOBs and the fixed PEI working region. Class 1 must
be wholly contained in exactly one available system-memory HOB. Reserved holes,
FD allocations, copied boot handoff, appended disk, MTP, and secondary stacks
therefore remain protected by their existing exclusions. Class 2/3 must not
intersect any guest resource or allocation HOB. Only after every check succeeds
does Mu split class-1 system-memory HOBs and create reserved-resource HOBs.

The full validated record is copied into a GUID HOB. Failure never frees earlier
exclusions; PrePi stops if a reservation cannot complete. The DXE ACPI driver
requires a single matching HOB when the marker exists, revalidates against live
metadata and verifies the EFI memory map. All EFI descriptor intervals must be nonempty, aligned, nonwrapping and
pairwise disjoint. Every intersecting EFI descriptor must
be `EfiReservedMemoryType`; class-1 coverage must be complete. Class 2/3 may be
absent from the EFI map. The same verification runs at ReadyToBoot. Failure stops
boot, and neither path writes the firmware memory. No GPU device is published.

## Repeatable offline evidence

Initialize the public pinned submodules, then build the artifact consumer:

```sh
python3 Tools/g17-preservation-e2e/build.py --output Build/G17E2E
Build/G17E2E/mu-g17-consumer guest.adt GUEST_BASE GUEST_SIZE DRAM_SIZE exclusions.json \
  --fd-base MU_IMAGE_BASE --bootargs guest-bootargs.bin --bootargs-sha256 SHA256
```

The consumer compiles the production AppleDTLib, real EarlySetup, BootArgs
adapter, admission, PEI HOB reservations and DXE/ReadyToBoot verifier. Host substitutes provide HOB
storage, the two physical CopyMem destinations, and EFI services; no physical
address is dereferenced. The actual serialized BootArgs artifact retains its
revision, guest/DRAM extents, ADT size and video data. Only devtree/virt_base are
translated into owned host memory, and the output reports this translation and
the orchestrator-supplied original artifact hash. CLI extents must match the
artifact. The ADT file must exactly match the advertised serialized length. The receipt binds
the executable, source files, compiler command and recursively discovered
compiler inputs by SHA-256. The output contains region classifications, actual
resulting resource HOBs and EFI descriptors. `--allocated BASE SIZE` and
`--uefi BASE SIZE` inject reservation collisions; `--map-fault` and
`--ready-map-fault` expose retained pages as BootServicesData at the respective
verification checkpoint. `--map-duplicate-gap` checks that duplicate reserved
half-spans cannot hide a missing half. `--boot-dest` and `--adt-dest` inject early
copy overlaps. Use a fresh output path per case; rejected runs do not write a
success artifact.

The Aura repository's G17 artifact E2E supplies the real host-produced guest ADT
and BootArgs. The existing `Tests/e2e/memory-peim.py` remains the boot/MTP regression
suite; its fixtures do not contain the G17 marker and its narrow service shim
delegates new G17 coverage to the full artifact consumer.

A native J813 FD build uses:

```sh
.venv-native/bin/stuart_update -c Platform/MacBookAir2026Pkg/PlatformBuild.py
AURORADBG_MU_OUTPUT_ROOT="$PWD/Build/G17Native" \
AURORADBG_LLD_LINK=/opt/homebrew/bin/lld-link \
Tools/build-j813-windows-native.sh internal-storage-windows
```

These artifacts establish offline path/build coverage. They do not establish
hardware GPU startup or Windows graphics-driver support.

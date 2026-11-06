# J813 active MTP memory ownership

## Failure model recorded before implementation

The J813-v1 handoff maps guest staging IPA `0x1800000` to physical
`0x10020000000` for 1 MiB. A 1 MiB RTKit DMA grant pool follows it. The host
profile reservation constrains Aura allocation and image loading but does not
publish an EFI memory reservation. T8142 MemoryPeim omitted the carveout present
in T602X. With the smaller modern host heap, Windows could allocate a boot-driver
image in this range; a legitimate MTP staging copy then overwrote it. The older
large heap placed the range below guest RAM and masked the omission.

Both regions must be excluded before DXE allocates memory and remain reserved
in the EFI map supplied to Windows. Failure cases include malformed policy,
partial RAM overlap, arithmetic overflow, overlap with the firmware or initial
UEFI workspace, and a conflicting existing resource hole. A handoff wholly
outside advertised guest RAM cannot be allocated and needs no overlapping HOB.
The full 2 MiB must be protected, including the RTKit pool, without losing either
adjacent RAM range.

The executable production MemoryPeim replay was extended before implementation
to reproduce the missing carveout. It checks policy admission, resource types,
full-span exclusion, adjacent ranges, total accounting and conflicts. Firmware
services are substituted; the reservation algorithm is production code.

## Admission and EFI propagation

This is the existing J813-v1 contract, not a generic Aura reservation ABI.
Exactly little-endian u32 zero in `/chosen/aura,windows-mtp-disabled` admits the
prepared Aura handoff. An absent property or missing `/chosen` admits the legacy
handoff, matching AcpiPlatform's J813MTP admission policy. Exactly u32 one skips
the reservation. Other present lengths or values stop memory initialization.

The legacy-absence case was added after reviewing actual ACPI admission. Its
missing reservation was reproduced before the helper was revised. The first
`672599c` firmware and its receipts remain preserved; the corrected build uses
a separate output directory.

The range is carved from system-memory resource HOBs as
`EFI_RESOURCE_MEMORY_RESERVED` before PrePi loads DXE. A failing MemoryPeim
status reaches PrePi's existing error stop. Pinned MU_BASECORE maps this resource
to `EfiGcdMemoryTypeReserved` in `MdeModulePkg/Core/Dxe/Gcd/Gcd.c`, and
`CoreGetMemoryMap` emits `EfiReservedMemoryType` in `Core/Dxe/Mem/Page.c`.
It is not boot-services memory and is not reclaimed at ExitBootServices.

## Validation and current hardware evidence

The integration replay's relocatable cases retain ASan and UBSan. Literal-address
MTP cases use non-replacing host virtual-memory allocation and UBSan: ASan's
shadow reservation occupies the 1 TiB ABI address. Failure to obtain that address
fails the test. Local receipts include:

- `Build/mtp-reservation-red03`: missing active reservation reproduced.
- `Build/mtp-reservation-legacy-red01`: missing legacy reservation reproduced.
- `Build/mtp-reservation-independent-02`: all 27 final cases independently passed.
- `Build/mtp-reservation-v2-artifacts/summary.json`: base and N1 firmware builds,
  source snapshots and hashes. Base FD SHA-256 is
  `27d2936cd4b4dcf285598b1c22e842036bf529ea1dd90e93204afe3348cb4254`.

The supervised fixed-Mu run completed 600 seconds with MTP enabled. Both
AppleANS and AcpiPlatform `GetMemoryMap` snapshots report all 512 pages in
`[0x10020000000, 0x10020200000)` as Reserved, with no conflicting descriptor.
Their preceding GCD snapshots also report Reserved, and MTP0 remains published.
The final local Aura map receipt is
`build/windows-hardware/mtp-memory-map/fixed-01-final.json`.
These are DXE snapshots, not a captured final ExitBootServices memory map.

The 450-second capture showed the Windows lock screen. The user confirmed that
the physical panel displayed it and that the built-in keyboard and trackpad
both worked through Aura → Mu → Windows. At 600 seconds, the shadow framebuffer
and native scanout captures both showed the Windows desktop.

All eight complete image reads totaled 136,314,880 bytes. The two terminal
shadow rereads matched the initial terminal shadow hash. The final drain
reported a zero guest-CPU mask, all 32 started flags clear, the expected deliberate
abort status, a responding proxy and a zero parent exit status. This qualifies
the bounded J813 run and its memory-ownership correction; it does not establish
long-duration stability or support on other platforms.

An optional stopped-guest follow-up found an intact `mup.sys` header and CodeView
record. Fourteen mapped image pages checked outside the MTP range; the walk
then stopped at a page with its access flag clear. That partial check is not a
claim that every image page was examined. The complete run, EFI map snapshots
and framebuffer/termination receipts are the primary qualification evidence.

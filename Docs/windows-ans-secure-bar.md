# T8142 ANS secure register resource

## Failure model recorded before the change

Windows attempt 07 reached bugcheck `0x7b`, with the unavailable boot path
`\ArcName\multi(0)disk(0)rdisk(0)partition(7)`. The current Windows AppleNvme
source requires a fourth memory resource for T8142's standard NVMe registers;
the existing Mu ACPI publisher emits only ASC, ANS, and SART resources. That
driver explicitly refuses T8142 without resource 3. The installed driver
version has not yet been verified, so correcting this mismatch is necessary
for the current driver but does not by itself establish the whole bugcheck's
cause.

The working Mu storage driver resolves `/arm-io/ans` ADT `reg[9]` separately
from the ANS aperture at `reg[3]`. The captured J813 serial log records
`standard=4C5CC0000/10000 secure=1`, while its ACPI publication lacks that range.
The fix must use the same ADT resource, not derive or hardcode its address.

Required failures and compatibility boundaries:

- Preserve memory indices 0=ASC, 1=ANS, 2=SART; append standard NVMe at 3.
- Keep the established three-resource ABI on older chips.
- T8142 must not publish a partial controller when reg[9] is missing, zero,
  smaller than 64 KiB, not page aligned, wraps the address space, or overlaps
  the other controller resources.
- Reject a T8142/SART profile mismatch.
- Do not reintroduce PMGR words as exclusive memory resources; they remain
  `_DSD` properties.
- Preserve the existing interrupt alias, `_HID`, `_STA`, and `_CCA`.
- Do not change the live RTKit/SART handoff or free its reserved DMA buffers.

The earlier `quiesce failed (-8)` means a queue-delete command completed with
an error status. It is not the controller-disable timeout (`-4`). The existing
preserve-for-OS path deliberately keeps RTKit, SART and reserved queue buffers
alive; the Windows start path disables the inherited controller before
installing its own queues. That behavior remains outside this resource fix.

## Validation boundary

The host integration replay compiles the complete production ANS ACPI
publication function against recorded ADT resource fixtures and modeled AML
services. It records the actual resource-constructor calls, checks ordering
and failure behavior, and compiles the resulting fixture ASL with iASL. The
AML service backend is modeled; final runtime ACPI publication and Windows
driver startup still require the hardware boot. Both Windows firmware profiles
are built and their source and artifact hashes retained separately.

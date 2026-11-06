# Aura MTP handoff admission

## Failure model recorded before implementation

Windows attempt 08 loaded AppleMtpHid and faulted while zeroing its telemetry
page at guest IPA `0x18ff000`. That page is the tail of the MTP staging window.
The J813 MTP SSDT requires a preboot owner to prepare the device, map staging
IPA/DVA `0x1800000` to physical `0x10020000000` for one MiB, and retain its RTKit
buffer pool in the next MiB. Publishing MTP0 without that handoff exposes a
device whose declared memory does not exist in the guest mapping.

The narrowly scoped admission rule must handle these failures:

- Every Aura Windows profile initially supplies `/chosen/` property
  `aura,windows-mtp-disabled` as exactly little-endian u32 value 1.
- Value 1 withholds the entire J813MTP SSDT, so no MTP0 device is enumerated.
- Only after successful MTP preparation does Aura change that value to 0.
  Value 0 admits the unchanged SSDT and its established four-resource ABI.
- An invalid value or present property of the wrong size must withhold MTP.
- An absent property preserves existing legacy m1n1 behavior. This is not a
  readiness signal for Aura: Aura must always emit the property.
- Other ACPI tables and platform SSDTs must keep their existing admission.
- Admission performs no MTP/DART/MMIO probe and cannot establish readiness
  independently. The host owns preparation, reservation and final mapping.

The integration replay compiles the real MTP ASL, executes the production
publication admission branch with that AML header and controlled ADT property
values, and preserves source hashes, AML and outcomes. It reproduces the old
publication on disabled handoff before the gate is added. It does not prove
device training, DMA ownership or physical Windows startup.

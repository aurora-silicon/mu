# J813 Windows bring-up without USB ownership

## Failure model, before implementation

- Suppressing guest ADT nodes alone must not leave an enabled static ACPI XHC0.
  Windows could otherwise reset a controller still owned by the Aura resident.
- The legacy m1n1 path must keep its existing XHC0/XHC1 status when the explicit
  policy property is absent.
- Malformed policy bytes must not silently fall back to enabled USB.
- A failed AML parse, missing policy object, failed update or serialization must
  abort DSDT publication rather than publish the original enabled device.
- The policy edit must only affect the J813 DSDT's dedicated USB status object.
  It must not edit resource addresses or unrelated namespace objects.
- Both the default Windows and N1 profiles must compile; ACPICA must evaluate
  the compiled DSDT with XHC0/XHC1 statuses 15/0 by default and 0/0 when disabled.
- Compilation and ACPICA evaluation are offline evidence. A live Aura-to-Mu
  receipt is still required to verify property transport and Windows enumeration.

## Contract

Aura's no-USB launch profile removes every `/arm-io/usb-drdN` and
`/arm-io/dart-usbN` node, plus aliases pointing to those removed nodes, before
serializing the guest ADT. It also writes `/chosen/aura,windows-usb-disabled`
as exactly four little-endian bytes with value 1. Mu's USB DART and DWC3 drivers
skip absent nodes. The no-USB path does not transfer any controller ownership.

Before installing the J813 DSDT, AcpiPlatformDxe validates this property and
updates `\_SB.AUSD` to 1 using AmlLib. XHC0's `_STA` then returns zero; XHC1
already returns zero. The property-absent path leaves `AUSD` zero and preserves
the legacy XHC0 status. The property applies to the J813 DSDT only.

This is a bring-up baseline for an appended RAM WinPE image. Real USB boot,
input or hotplug still requires Aura to quiesce each non-proxy controller,
disable its DMA, and hand it to Mu reset/clamped with the PHY policy Mu expects.

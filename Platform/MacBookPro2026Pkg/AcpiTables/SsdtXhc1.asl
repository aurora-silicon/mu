// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, D4 (J804_WINDOWS_USB builds only): usb-drd1, the
// left-rear USB-C port, as a generic xHCI controller for Windows' usbxhci
// (windows-m6/D4-USB-DESIGN.md section 8).
//
// The dwc3 runs in USB2 host mode as aura's j804_usb_prep leaves it; the
// carrier passes exactly the xHCI register window through (two 16 KiB
// stage-2 pages; the dwc3 globals at +0xc100 stay denied), aliases AIC 1692
// to the level SPI J804_GSIV_XHC1, and DMA goes through the carrier's
// coherent Gen3 DART identity map of guest RAM, so _CCA is 1. No power
// methods (_PSx, _PRx, _PRW, _DSW, _S0W): Windows must not power the
// controller off, and with no _S0W it is not wake-capable in S0.
//
// This is a separate table, not part of the DSDT: J804XhciAcpiDxe installs
// it only when the launcher's /chosen/aura,windows-usb-v1 handshake is
// present and matches J804Windows.h. Without it Windows sees no XHC1 and
// never touches the xHCI window. usb-drd0 (the proxy link) is never
// described.
//
#include <J804Windows.h>

DefinitionBlock ("", "SSDT", 2, "AURORA", "J804XHC1", 1)
{
    Scope (\_SB)
    {
        Device (XHC1)
        {
            Name (_HID, "PNP0D10")
            Name (_UID, One)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    J804_XHC1_BASE, 0x40A437FFF, 0, J804_XHC_SIZE)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {J804_GSIV_XHC1}
            })
        }
    }
}

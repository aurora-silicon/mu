// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, I0 (J804_WINDOWS_INPUT builds only): the carrier's
// keyboard and pointer mailbox (windows-m6/INPUT-DESIGN.md section 1.6) for
// Windows' m6input.sys HID minidriver.
//
// One 16 KiB page that the carrier hooks and never maps (every access is
// trapped and emulated) and one purely virtual level SPI with no AIC line
// behind it. The device does no DMA. No power methods (_PSx, _PRx, _PRW,
// _DSW, _S0W) and no _DSD: the carrier owns the device's state, and with no
// _S0W it is not wake-capable in S0.
//
// This is a separate table, not part of the DSDT: J804InputAcpiDxe installs
// it only when the launcher's /chosen/aura,windows-input-v1 handshake is
// present and matches J804Windows.h. Without it Windows sees no INP0 and
// never touches the page.
//
#include <J804Windows.h>

DefinitionBlock ("", "SSDT", 2, "AURORA", "J804INP0", 1)
{
    Scope (\_SB)
    {
        Device (INP0)
        {
            Name (_HID, J804_INPUT_HID)
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    J804_INPUT_BASE, 0xF50003FFF, 0, J804_INPUT_SIZE)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {J804_GSIV_INPUT}
            })
        }
    }
}

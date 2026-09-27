// SPDX-License-Identifier: BSD-2-Clause-Patent
// RAM disk, GOP, twelve CPUs, and prepared USB/NVMe/SMC controllers.
DefinitionBlock ("", "DSDT", 2, "AURORA", "J873WIN", 1)
{
    Scope (\_SB)
    {
        Device (CPU6)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Name (_STA, 0x0F)
        }
        Device (CPU7)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
            Name (_STA, 0x0F)
        }
        Device (CP00)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
            Name (_STA, 0x0F)
        }
        Device (CP01)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
            Name (_STA, 0x0F)
        }
        Device (CP02)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
            Name (_STA, 0x0F)
        }
        Device (CP03)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Name (_STA, 0x0F)
        }
        Device (CP04)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
            Name (_STA, 0x0F)
        }
        Device (CP05)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
            Name (_STA, 0x0F)
        }
        Device (CP08)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 8)
            Name (_STA, 0x0F)
        }
        Device (CP09)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 9)
            Name (_STA, 0x0F)
        }
        Device (CP10)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 10)
            Name (_STA, 0x0F)
        }
        Device (CP11)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 11)
            Name (_STA, 0x0F)
        }
        Device (XHC1)
        {
            Name (_HID, "PNP0D15")
            Name (_UID, 1)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x40A430000, 0x40A43BFFF, 0, 0xC000)
                // J873 Windows09 aliases physical AIC IRQ 1692.
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {995}
            })
        }
        Device (XHC2)
        {
            Name (_HID, "PNP0D15")
            Name (_UID, 2)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x382430000, 0x38243BFFF, 0, 0xC000)
                // m1n1 J873 Windows carrier aliases physical AIC IRQ 1763.
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {997}
            })
        }
        Device (SMC0)
        {
            Name (_HID, "NTAS2032")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            // Read-only sensor driver polls the separate ASCWrap-v8 mailbox.
            // Resource order is ASC, firmware SRAM, mailbox page.
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x30C600000, 0x30C673FFF, 0, 0x74000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x30DE00000, 0x30DF1FFFF, 0, 0x120000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x30C710000, 0x30C713FFF, 0, 0x4000)
            })
        }
        Device (ANS0)
        {
            Name (_HID, "NTAS2004")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x321600000, 0x321673FFF, 0, 0x74000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x325CC0000, 0x325D1FFFF, 0, 0x60000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x325C50000, 0x325C5BFFF, 0, 0xC000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x316010000, 0x316013FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x300700000, 0x300703FFF, 0, 0x4000)
                // J873 Windows carrier aliases physical AIC IRQ 1349.
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {996}
            })
        }
    }
}

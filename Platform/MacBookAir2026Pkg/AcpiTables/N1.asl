// SPDX-License-Identifier: MIT
// Installed only by the validated Aura N1 handoff gate in AcpiPlatformDxe.
DefinitionBlock ("N1.aml", "SSDT", 2, "AURORA", "AURA813N", 1)
{
    Scope (\_SB)
    {
        Device (N1PC)
        {
            Name (_HID, "AURA8130")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x1CB0000000, 0x1CB01FFFFF, 0, 0x200000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x488000000, 0x48801FFFF, 0, 0x20000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x488028000, 0x48802BFFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x48F010000, 0x48F010FFF, 0, 0x1000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x39A000300, 0x39A000303, 0, 4)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0xB80000000, 0xB805FFFFF, 0, 0x600000)
            })
        }
    }
}

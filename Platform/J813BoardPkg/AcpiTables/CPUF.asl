// SPDX-License-Identifier: MIT
// J813-v1 manual DVFS only. AcpiPlatform requires explicit Aura ownership.
// Addresses: public Linux t8142 cluster-cpufreq resources. No PEP/CPPC claim.
DefinitionBlock ("CPUF.aml", "SSDT", 2, "AURORA", "J813CPUF", 1)
{
    Scope (\_SB)
    {
        Device (CFPM)
        {
            Name (_HID, "AURA8142")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x210E20000, 0x210E20FFF, 0, 0x1000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x211E20000, 0x211E20FFF, 0, 0x1000)
            })
        }
    }
}

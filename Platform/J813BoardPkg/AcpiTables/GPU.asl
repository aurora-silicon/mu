// SPDX-License-Identifier: MIT
// J813 G17 GPU for the AppleAgxWddm G17 boot probe (AgxwStartMode 'G17B').
//
// Installed only by AcpiPlatformDxe when PcdAppleG17GpuProbe is TRUE, the
// machine is chip 0x8142 / board 0x24, and the six iBoot regions described
// by /arm-io/sgx validate (sizes, alignment, disjointness, outside system
// memory). It then patches the six placeholder descriptors
// (minimum 0xA17000n000000000) with those regions. No interrupt is declared:
// the probe polls both GPU mailboxes.
//
// _CRS order is the driver's contract (integration/agx-wddm-g17-boot/README.md):
//   0 SGX, 1 GFX ASCWrap v6, 2 GFX1 ASCWrap v6,
//   3 gfx-handoff, 4 gpu-region (TTBAT), 5 gfx-shared-region (page tables),
//   6 gfx-shared-l2-region, 7 gfx-data, 8 gfx1-data.
DefinitionBlock ("GPU.aml", "SSDT", 2, "AURORA", "AURA817G", 1)
{
    Scope (\_SB)
    {
        Device (GPU0)
        {
            Name (_HID, "AURA8170")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x580000000, 0x580FFFFFF, 0, 0x1000000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x582600000, 0x582687FFF, 0, 0x88000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x582E00000, 0x582E87FFF, 0, 0x88000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170000000000000, 0xA170000000003FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170001000000000, 0xA170001000003FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170002000000000, 0xA170002000003FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170003000000000, 0xA170003000003FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170004000000000, 0xA170004000003FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0, 0xA170005000000000, 0xA170005000003FFF, 0, 0x4000)
            })
        }
    }
}

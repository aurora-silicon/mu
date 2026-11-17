// Unified candidate only: selecting it excludes the legacy GPU/DCP SSDTs.
// First nine resources preserve GPU.asl order; seven DCP resources follow.
// SPDX-License-Identifier: MIT
// J813 unified GPU/panel resource candidate. Published only when an image
// built with PcdAppleJ813UnifiedDisplay sees the host's /chosen
// aura,windows-unified-display-v1 marker (default off), exact J813 identity,
// dcp/disp0 presence and all six non-OS-RAM GPU regions validate. Selecting
// it excludes legacy AURA8170 and AURA8180 even if this table fails admission.
// Driver publication remains closed until primary/GPU-display leases/cadence
// exist. No interrupt or framebuffer resource is invented here.
//
// _CRS order is the driver's contract (integration/agx-wddm-g17-boot/README.md):
//   0 SGX, 1 GFX ASCWrap v6, 2 GFX1 ASCWrap v6,
//   3 gfx-handoff, 4 gpu-region (TTBAT), 5 gfx-shared-region (page tables),
//   6 gfx-shared-l2-region, 7 gfx-data, 8 gfx1-data.
DefinitionBlock ("GPUF.aml", "SSDT", 2, "AURORA", "AURA817F", 1)
{
    Scope (\_SB)
    {
        Device (GPU0)
        {
            Name (_HID, "AURA8171")
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
                // disp-0
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x284000000, 0x284B1BFFF, 0, 0xB1C000)
                // disp-1
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x286240000, 0x28624FFFF, 0, 0x10000)
                // disp0 DART (scanout streams 0 and 16)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x286300000, 0x28630BFFF, 0, 0xC000)
                // DCP DART (stream 23)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x286340000, 0x28635FFFF, 0, 0x20000)
                // disp-2
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x2863AC000, 0x2863AFFFF, 0, 0x4000)
                // disp-3 (ASC, mailbox, iop-vbar)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x286800000, 0x286FFFFFF, 0, 0x800000)
                // disp-4 (pmgr bandwidth scratch and doorbell)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x382700000, 0x382813FFF, 0, 0x114000)
            })
        }
    }
}

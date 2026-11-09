// SPDX-License-Identifier: MIT
// J813 internal display coprocessor (DCP) for the standalone AppleDisplay
// driver (AuroraSilicon drivers/AppleDisplay, J813-H17G-PLAN.md).
//
// Installed only by AcpiPlatformDxe when PcdAppleJ813DisplayDevice is TRUE,
// the machine is chip 0x8142 / board 0x24, and the ADT has /arm-io/dcp and
// /arm-io/disp0 (AuraJ813DisplayDevice.c). The ranges are fixed SoC MMIO
// from the J813 device tree in aurora-silicon/linux e50ba38
// (t8142-j813-display.dtsi, read from this machine's ADT).
//
// _CRS is the driver's contract (AppleDisplayJ813Core.h), seven windows that
// do not overlap:
//   disp-0, disp-1, disp0 DART, DCP DART, disp-2, disp-3, disp-4.
// disp-3 contains the DCP ASC (+0x600000), its mailbox (+0x608000) and
// iop-vbar (+0x50000); disp-4 contains the pmgr bandwidth scratch (+0x20000)
// and doorbell (+0xa4000). They are not published a second time.
//
// No interrupt: Aura routes no DCP line to Windows, so the driver polls.
// No framebuffer: the driver takes the POST display from dxgkrnl, as on
// J414s (MacBookProEarly2023Pkg DISP.asl explains why it is not a resource).
DefinitionBlock ("DISP.aml", "SSDT", 2, "AURORA", "AURA818D", 1)
{
    Scope (\_SB)
    {
        Device (DCP0)
        {
            Name (_HID, "AURA8180")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
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

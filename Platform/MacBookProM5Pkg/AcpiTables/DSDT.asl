// SPDX-License-Identifier: BSD-2-Clause-Patent
// QEMU T6050 fixture: UIDs match the native-AIC MADT and ADT CPU ordering.
DefinitionBlock ("", "DSDT", 2, "AURORA", "J714WIN", 1)
{
    Scope (\_SB)
    {
#if J714_N1
        // Persistent owner for N1 firmware/DART storage across PCI personality
        // replacement. It owns this alias independently from ANS and PCI BARs.
        Device (N1DM)
        {
            Name (_HID, "AURO0100")
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x61F04000, 0x4000)
            })
            OperationRegion (N1RG, SystemMemory, 0x61F04000, 0x4000)
            Field (N1RG, QWordAcc, NoLock, Preserve)
            {
                Offset (0x60), NRST, 64,
                Offset (0x78), NARM, 64,
                Offset (0x80), NABI, 64,
                Offset (0x1018), NDOO, 64
            }
        }
        // Shared N1 reset dependency. Windows removes affected PCI stacks
        // before invoking this bounded native preboot port transition.
        PowerResource (N1PR, 0, 0)
        {
            Method (_STA, 0, NotSerialized) { Return (One) }
            Method (_ON, 0, NotSerialized) { }
            Method (_OFF, 0, NotSerialized) { }
            Method (_RST, 0, Serialized)
            {
                If (LNotEqual (\_SB.N1DM.NABI, 0x4E31525300000001)) { Return (One) }
                If (LNotEqual (\_SB.N1DM.NARM, One)) { Return (One) }
                Store (0x4E31525300000002, \_SB.N1DM.NDOO)
                Return (\_SB.N1DM.NRST)
            }
        }
        // BAR windows use native CPU addresses through stage-2. The
        // non-prefetchable bus aperture is translated above the USB PMGR aliases.
        // ECAM is
        // the Linux-qualified buses 0..1 window; each PCI function keeps its
        // own requester ID and future DART lease.
        Device (PCI0)
        {
            Name (_HID, EISAID ("PNP0A08"))
            Name (_CID, EISAID ("PNP0A03"))
            Name (_UID, Zero)
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_CCA, One)
            Name (_CBA, 0x62000000)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0, 0, 1, 0, 2)
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x80000000, 0x83FFFFFF, 0xB00000000, 0x4000000)
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Prefetchable, ReadWrite, 0,
                    0xBC0000000, 0xBC07FFFFF, 0, 0x800000)
            })
            Device (RP00)
            {
                Name (_ADR, Zero)
                Device (N1C0)
                {
                    Name (_ADR, Zero)
                    Name (_DEP, Package () { \_SB.N1DM })
                    Name (_PRR, Package () { \_SB.N1PR })
                }
            }
        }
#endif
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
        Device (CP06)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Name (_STA, 0x0F)
        }
        Device (CP07)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
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
        Device (CP12)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 12)
            Name (_STA, 0x0F)
        }
        Device (CP13)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 13)
            Name (_STA, 0x0F)
        }
        Device (CP14)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 14)
            Name (_STA, 0x0F)
        }
        Device (CP15)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 15)
            Name (_STA, 0x0F)
        }
        Device (CP16)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 16)
            Name (_STA, 0x0F)
        }
        Device (CP17)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 17)
            Name (_STA, 0x0F)
        }

        // Apple MTP keyboard/trackpad transport (x1n1 guest windows); see
        // windows-native-20261005/mtp-hid-port/DSDT-MTP-proposed.asl.
        Device (MTPH)
        {
            Name (_HID, "NTAS0054")
            Name (_UID, 0)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                // DockChannel parent IRQ block (mask @0, flags @4)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000062C14000, 0x0000000062C17FFF, 0x0000000000000000, 0x0000000000004000,,,)
                // DockChannel AP config (TX/RX threshold)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000062C30000, 0x0000000062C33FFF, 0x0000000000000000, 0x0000000000004000,,,)
                // DockChannel AP data (TX/RX FIFO registers)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000062C34000, 0x0000000062C37FFF, 0x0000000000000000, 0x0000000000004000,,,)
                // MTP ASC CPU-control block
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000062C00000, 0x0000000062C03FFF, 0x0000000000000000, 0x0000000000004000,,,)
                // MTP ASC mailbox (A2I/I2A control @0x110/0x114, FIFOs @0x800..)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000062C08000, 0x0000000062C0BFFF, 0x0000000000000000, 0x0000000000004000,,,)
                // MTP firmware SRAM (__TEXT+__DATA segments), identity IPA
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000294C00000, 0x0000000294CC3FFF, 0x0000000000000000, 0x00000000000C4000,,,)
                // DockChannel interrupt: AIC line 660, level high
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive,,,) { 660 }
            })
        }

        // Monitor-serviced Samsung/Apple UART emulation; AppleSerial uses
        // its existing RX polling path, so no physical UART IRQ is claimed.
#if J714_USB3
#if J714_USB_HOSTS
        OperationRegion (UHRD, SystemMemory, 0x61F00058, 8)
        Field (UHRD, QWordAcc, NoLock, Preserve) { UHRM, 64 }
        OperationRegion (UPA1, SystemMemory, 0x8902400C, 0x14)
        Field (UPA1, DWordAcc, NoLock, Preserve)
        { UMU1, 32, Offset (0x10), UAO1, 32 }
        Device (XHC1)
        {
            Name (_HID, "PNP0D15")
            Name (_UID, 1)
            Name (_CCA, One)
            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (And (UHRM, 2), Zero)) { Return (Zero) }
                If (LAnd (LEqual (And (UMU1, 0x73), 0x10),
                          LEqual (And (UAO1, 0x11), One))) { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x89000000, 0x8900FEFF, 0, 0xFF00,,,)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {1003}
            })
        }
#endif
        // Right-port native USB3. Core + PIPE grant and DMA are prepared by
        // the explicit x1n1/Mu profile; failed bringup leaves RESET_N clear.
        OperationRegion (UPAR, SystemMemory, 0x8A02400C, 0x14)
        Field (UPAR, DWordAcc, NoLock, Preserve)
        { UMUX, 32, Offset (0x10), UAON, 32 }
        Device (XHC2)
        {
            Name (_HID, "PNP0D15")
            Name (_UID, 2)
            Name (_CCA, One)
            Method (_STA, 0, NotSerialized)
            {
#if J714_USB_HOSTS
                If (LEqual (And (UHRM, 4), Zero)) { Return (Zero) }
#endif
                If (LAnd (LEqual (And (UMUX, 0x73), 0x10),
                          LEqual (And (UAON, 0x11), One))) { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x8A000000, 0x8A00FEFF, 0, 0xFF00,,,)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {1002}
            })
        }
#endif

        Device (COM0)
        {
            Name (_HID, "APPL8900")
            Name (_UID, 0)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x60000000, 0x60000FFF, 0, 0x1000,,,)
            })
        }

#if J714_SMC
        // One read-only v6 owner; status is set only after host ADT validation.
        OperationRegion (SMRD, SystemMemory, 0x61F00048, 8)
        Field (SMRD, QWordAcc, NoLock, Preserve) { SRDY, 64 }
        Device (SMC0)
        {
            Name (_HID, "NTAS3032")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (SRDY, One)) { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x60600000, 0x60603FFF, 0, 0x4000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x28DE00000, 0x28DF3FFFF, 0, 0x140000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x60608000, 0x6060BFFF, 0, 0x4000,,,)
            })
        }
        // Shared battery-class frontend. The mailbox remains owned solely
        // by SMC0; BAT0 obtains key reads through its device interface.
        Device (BAT0)
        {
            Name (_HID, "NTAS0053")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (SRDY, One)) { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate () {})
        }
#endif

#if J714_KBL
        OperationRegion (KBRD, SystemMemory, 0x61F00050, 8)
        Field (KBRD, QWordAcc, NoLock, Preserve) { KRDY, 64 }
        Device (KBL0)
        {
            Name (_HID, "NTAS3051")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (LEqual (KRDY, One)) { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x62610000, 0x62613FFF, 0, 0x4000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0, 0x62614000, 0x62617FFF, 0, 0x4000,,,)
            })
        }
#endif

#if J714_NVME
        // Explicit M5 resident-SPTM storage qualification profile. Host setup
        // validates live ADT resources, the selected interrupt and warm ANS.
        OperationRegion (ASRD, SystemMemory, 0x61F00020, 0x10)
        Field (ASRD, QWordAcc, NoLock, Preserve) { ARDY, 64, AIRQ, 64 }
        Device (ANS0)
        {
            Name (_HID, "NTAS2005")
            Name (_UID, Zero)
            Name (_CCA, One)
            Method (_STA, 0, NotSerialized)
            {
                If (LAnd (LEqual (ARDY, One), LEqual (AIRQ, 2338)))
                { Return (0x0F) }
                Return (Zero)
            }
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x60200000, 0x60203FFF, 0, 0x4000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x60208000, 0x6020BFFF, 0, 0x4000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x60300000, 0x6032FFFF, 0, 0x30000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x60400000, 0x6042FFFF, 0, 0x30000,,,)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x61F00000, 0x61F03FFF, 0, 0x4000,,,)
                // Live ADT physical IRQ 2338 is translated by the CSRT ALI2
                // entry to GSIV 1001, inside ACPI.sys's 0..1023 input range.
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive,,,) {1001}
            })
        }
#endif
    }
}

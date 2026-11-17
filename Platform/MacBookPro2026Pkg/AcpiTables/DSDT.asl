// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 (MacBook Pro 14-inch, M6) windows profile DSDT, for Mu running at EL1
// under the EL2 carrier (aura WINDOWS=1 personality on T8152).
//
// Described: twelve CPUs in three ACPI0010 clusters, the SMC (read-only
// sensor contract, NTAS2032) and the internal SSD controller (NTAS2004,
// present but disabled unless the build sets J804_WINDOWS_ANS), plus COM0
// (J804_WINDOWS_VUART) and the carrier's AHCI disk AHC0 (J804_WINDOWS_AHCI).
//
// Deliberately NOT described, whatever a driver might want: speaker amps,
// i2c1, i2c3, AOP, ADMAC, the hpm USB-PD port controllers, GPIO pin 0xca
// (193), the USB-C xHCI controllers, and anything that reaches the QSPI
// NVRAM (spi controller and NOR). usb-drd1's xHCI (D4, J804_WINDOWS_USB) is
// not in this table: it is the separate SSDT SsdtXhc1.asl, which
// J804XhciAcpiDxe installs only when the launcher's USB handshake is present.
//
// Values shared with the carrier come from J804Windows.h.
//
#include <J804Windows.h>

#ifndef J804_WINDOWS_ANS
#define J804_WINDOWS_ANS 0
#endif
#ifndef J804_WINDOWS_SMC
#define J804_WINDOWS_SMC 1
#endif
#ifndef J804_WINDOWS_VUART
#define J804_WINDOWS_VUART 0
#endif
#ifndef J804_WINDOWS_AHCI
#define J804_WINDOWS_AHCI 0
#endif

DefinitionBlock ("", "DSDT", 2, "AURORA", "J804WIN", 1)
{
    Scope (\_SB)
    {
        //
        // E cluster: ADT cpu0..cpu5, MPIDR 0x0..0x5.
        //
        Device (CLU0)
        {
            Name (_HID, "ACPI0010")
            Name (_UID, 0x100)
            Name (_STA, 0x0F)
            Device (CPU0)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 0)
                Name (_STA, 0x0F)
            }
            Device (CPU1)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 1)
                Name (_STA, 0x0F)
            }
            Device (CPU2)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 2)
                Name (_STA, 0x0F)
            }
            Device (CPU3)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 3)
                Name (_STA, 0x0F)
            }
            Device (CPU4)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 4)
                Name (_STA, 0x0F)
            }
            Device (CPU5)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 5)
                Name (_STA, 0x0F)
            }
        }

        //
        // P cluster: ADT cpu6 (the boot CPU) and cpu7, MPIDR 0x10100/0x10101.
        //
        Device (CLU1)
        {
            Name (_HID, "ACPI0010")
            Name (_UID, 0x101)
            Name (_STA, 0x0F)
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
        }

        //
        // M cluster: ADT cpu8..cpu11, MPIDR 0x40102..0x40105.
        //
        Device (CLU2)
        {
            Name (_HID, "ACPI0010")
            Name (_UID, 0x102)
            Name (_STA, 0x0F)
            Device (CPU8)
            {
                Name (_HID, "ACPI0007")
                Name (_UID, 8)
                Name (_STA, 0x0F)
            }
            Device (CPU9)
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
        }

        //
        // SMC (ADT smc, iop,ascwrap-v8; same layout as J873, checked against
        // our J804 DTS). Polled, no interrupt. Resource order is ASC, SMC
        // SRAM, mailbox page (the driver adds the post-office offset 0x80,
        // giving 0x30C710080).
        //
        Device (SMC0)
        {
            Name (_HID, "NTAS2032")
            Name (_UID, Zero)
            Name (_CCA, One)
#if J804_WINDOWS_SMC
            Name (_STA, 0x0F)
#else
            Name (_STA, Zero)
#endif
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

        //
        // Internal SSD: ANS2 (ADT ans). Resource order as the J873 profile:
        // ASC, NVMe+NVMMU, SART v4, ANS mailbox page, PMGR page.
        //
        // Mailbox: the Linux-proven post-office window 3 at 0x316020300
        // (our t8152-j804.dts ans_mbox, IRQs 302/262; NVMe works with it
        // under Linux on J804). The page below is 0x316020000 and the driver
        // adds +0x300. This is NOT the J873 profile's 0x316010000 page and is
        // NOT taken from the live ADT: /arm-io/ans reg[2] maps
        // 0x316000000 + 0x1c000, which does not cover 0x316020300, and the
        // ADT post-office rule would give 0x316010300 instead (AUDIT-0.8 7.2,
        // open question; unverified under Windows).
        //
        // NVMe interrupt: published GSIV 996, which the carrier must alias to
        // physical AIC 1349 (ADT ans interrupts[3]).
        //
        // Emitted only when the build sets J804_WINDOWS_ANS: M2 and M3 run
        // without any internal-storage driver, and the default DSDT names
        // none of the pages below.
        //
        // The last two resources are SHARED pages and are NOT a stage-2
        // mapping request (J804Windows.h, J804_ANS_*): the PMGR page holds
        // ps_spi_p (+0xd40) and ps_qspi (+0x1140) next to ps_ans (+0x8a0),
        // and the mailbox page holds the DCP mailbox at +0x500 (and other
        // coprocessors' post-office windows). The carrier must never map
        // either page; it traps them and passes through only ps_ans and the
        // 0x80-byte mailbox window at +0x300.
        //
#if J804_WINDOWS_ANS
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
                    0x316020000, 0x316023FFF, 0, 0x4000)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x300700000, 0x300703FFF, 0, 0x4000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {J804_GSIV_ANS}
            })
        }
#endif

#if J804_WINDOWS_VUART
        //
        // The carrier's virtual UART at the uart0 address (ADT uart0,
        // uart-1,samsung, 0x331200000). Only built when the carrier offers
        // it; the guest never reaches the physical block.
        //
        Device (COM0)
        {
            Name (_HID, "APPL8900")
            Name (_UID, Zero)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x331200000, 0x331203FFF, 0, 0x4000)
            })
        }
#endif

#if J804_WINDOWS_AHCI
        //
        // D1: the carrier-emulated AHCI 1.3.1 HBA (one SATA port, a DRAM
        // disk; windows-m6/D1-AHCI-DESIGN.md). _CLS 01/06/01 gives Windows
        // the compatible ID ACPI\CLS_0001&SUBCLS_0006&PI_01, which mshdc.inf
        // binds to the inbox storahci. The _HID is project-local. The ABAR
        // page is hooked by the carrier (never mapped) and the interrupt is
        // a purely virtual level SPI with no AIC line behind it. DMA is
        // cache coherent: the carrier copies through its cached map of
        // guest RAM. Only built with J804_WINDOWS_AHCI. _STA is a constant:
        // the launcher must refuse to start an AHCI FD (raw marker at FD
        // offset 0x110) without an attached disk, or Windows would bind
        // storahci to an ABAR the carrier does not hook. J804AhciDxe uses the
        // /chosen handshake to decide the same for UEFI.
        //
        Device (AHC0)
        {
            Name (_HID, "NTAS2010")
            Name (_UID, Zero)
            Name (_CLS, Package (0x03) { 0x01, 0x06, 0x01 })
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    J804_AHCI_BASE, 0xF40003FFF, 0, J804_AHCI_SIZE)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {J804_GSIV_AHCI}
            })
        }
#endif
    }
}

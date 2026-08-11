/**
 * Copyright (c) 2023, amarioguy (AppleWOA authors).
 *
 * Module Name:
 *     DSDT.asl
 *
 * Abstract:
 *     Differentiated System Description Table. This source file implements the DSDT table
 *     for the MacBook Air (M5, 2026) platform.
 *
 * Environment:
 *     UEFI firmware/runtime services.
 *
 * License:
 *     SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 *
 **/
#include <IndustryStandard/Acpi65.h>

 DefinitionBlock("DSDT.aml", "DSDT", 0x02, "Apple", "J813", 0x8142) {
    Scope(\_SB) {
        //
        // Cluster low power states. On T8101/T8103, there are only 2 clusters, the P and E core clusters,
        // so should be easier to track
        //
        Name (CLPI, Package() {
            0, // Version
            0, // Level Index
            1, // Count
            Package() { // Power Gating state for Cluster
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No Parent State
            0x00000000, // Integer Entry method (currently NULL, TODO actually add an entry method)
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "ClusterRetention"
            },
        })

        //
        // Per processor low power states.
        //
        Name(PLPI, Package() {
            0, // Version
            0, // Level Index
            2, // Count
            Package() { // WFI for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            0, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No parent state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "WFI",
            },
            Package() { // Power Gating state for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            1, // Parent node can be in any state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00000000,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "CorePwrDn"
            },
        })

        /*
        Device (XHC0) {
            Name (_HID, "PNP0D15")
            Name (_UID, One)
            Name (_CCA, One)

            Method (_CRS, 0, Serialized) {
                Name (RBUF, ResourceTemplate () {
                    QWordMemory (
                        ResourceConsumer,     // ResourceUsage
                        PosDecode,            // Decode
                        MinFixed,             // IsMinFixed
                        MaxFixed,             // IsMaxFixed
                        NonCacheable,         // Cacheable
                        ReadWrite,            // ReadAndWrite
                        0,                    // AddressGranularity - GRA
                        0x382280000,          // AddressMinimum - MIN
                        0x38237FFFF,          // AddressMaximum - MAX
                        0,                    // AddressTranslation - TRA
                        0x100000              // RangeLength - LEN
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                        777
                    }
                })
                Return (RBUF)
            }

            Method (_STA) {
                Return (0xF)
            }
        }
        */

        Device (XHC1) {
            Name (_HID, "PNP0D15")
            Name (_UID, One)
            Name (_CCA, One)

            Method (_CRS, 0, Serialized) {
                Name (RBUF, ResourceTemplate () {
                    QWordMemory (
                        ResourceConsumer,     // ResourceUsage
                        PosDecode,            // Decode
                        MinFixed,             // IsMinFixed
                        MaxFixed,             // IsMaxFixed
                        NonCacheable,         // Cacheable
                        ReadWrite,            // ReadAndWrite
                        0,                    // AddressGranularity - GRA
                        0x502280000,          // AddressMinimum - MIN
                        0x50237FFFF,          // AddressMaximum - MAX
                        0,                    // AddressTranslation - TRA
                        0x100000              // RangeLength - LEN
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                        857
                    }
                })
                Return (RBUF)
            }

            //
            // Reported absent on this platform.
            //
            // The address above is an M1 one -- MacBookProLate2020Pkg,
            // MacMini2020Pkg and MacBookProLate2025Pkg all carry the identical
            // 0x502280000, and nothing here was ever read off J813.  There is
            // no evidence T8142 has an xHCI controller there, and the measured
            // result of claiming otherwise is a synchronous external abort:
            // Windows enumerated XHC1, mapped it, read a register on cpu8 and
            // took ESR 0x92000410 with DFSC 0x10.  The region is mapped
            // passthrough in stage 2, so the abort came from the hardware
            // itself -- nothing is decoding at that address.
            //
            // Its interrupt could not be delivered either: GSIV 857 would have
            // to arrive through AIC, and no AIC-to-GIC bridge exists yet.
            //
            // XHC0 immediately above is commented out for the same reason.
            // Restore this once a real T8142 xHCI base is read from the ADT and
            // its interrupt can actually be routed.  WinPE boots from a ramdisk
            // and does not need USB to reach the installer.
            //
            Method (_STA) {
                Return (0x0)
            }
        }

        Device(COM0) {
            Name(_HID, "APPL8900") // naming it APPL8900 since the Samsung based UART was used since the 8900
            Name(_UID, Zero)
            Name (_CRS, ResourceTemplate () {
                QWordMemory (
                ResourceProducer,     // ResourceUsage
                PosDecode,            // Decode
                MinFixed,             // IsMinFixed
                MaxFixed,             // IsMaxFixed
                NonCacheable,         // Cacheable
                ReadWrite,            // ReadAndWrite
                0x0000000000000000,   // AddressGranularity - GRA
                // FixedPcdGet64(PcdAppleUartBase),   // AddressMinimum - MIN
                // (FixedPcdGet64(PcdAppleUartBase) + 0xFFFF),   // AddressMaximum - MAX
                //
                // J813/T8142 uart0.  Was 0x235200000, which is T8103's UART --
                // an M1 address inherited with the rest of this table, backed
                // by nothing on this SoC.  XHC1 above shows what that costs
                // when a driver actually reads it: a synchronous external
                // abort.  This value matches PcdAppleUartBase in
                // T8142FamilyPkg.dsc.inc, which is what the commented-out
                // FixedPcdGet64 above was reaching for before it was hardcoded.
                //
                // It is also the address m1n1 traps as its VUART, so a guest
                // access lands in emulation rather than on the bare device.
                //
                0x3a5200000,   // AddressMinimum - MIN
                0x3a5200fff,   // AddressMaximum - MAX
                0x0000000000000000,   // AddressTranslation - TRA
                0x0000000000001000    // RangeLength - LEN
                )
                Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) { 1097 }
            })
            Method (_STA) {
                Return (0xF)
            }
        }

        //
        // T8101/T8103 *only* have 1 CPU die, ever, so everything in this node will comprise
        // most of the SoC.
        //
        Device(SOC) {
            Name(_HID, "ACPI0010") // all "processor containers" must have this HID
            Name(_UID, Zero) // unique identifier of the container

            //
            // E-core cluster, typically bootstrap core is here
            //
            //
            // Processor objects for T8142/M5: 6 E-cores (UID 0..5) then
            // 4 P-cores (UID 6..9).
            //
            // This block previously described M1's topology -- CLU0 holding
            // UIDs 0..3 and CLU1 holding 4..7 -- because it was inherited from
            // the M1 platform packages along with the rest of this table.  On a
            // ten-core part that is not merely inaccurate: UIDs 8 and 9 had no
            // ACPI0007 device at all, and UIDs 4 and 5 are E-cores described
            // inside the P-core container.
            //
            // Windows builds its per-processor power management state from the
            // MADT and expects to find a matching processor object for each
            // enabled entry.  The measured consequence of the missing ones was
            // a reproducible bugcheck 0xA -- IRQL_NOT_LESS_OR_EQUAL, a NULL
            // dereference at DISPATCH_LEVEL -- at ntoskrnl RVA 0x2C3D8C, inside
            // an internal Power Manager routine that sits immediately after
            // PoCpuIdledSinceLastCallImprecise in the export table and calls a
            // per-processor handler through a function pointer.
            //
            // UIDs here match MADT_Static.aslc and PPTT.aslc, which already use
            // E = 0..5 and P = 6..9.
            //
            // CPU0 reports _STA 0xF like every other core.  It briefly
            // reported 0 to match a MADT entry that marked E-core 0 not
            // enabled while m1n1 could not start it; both were restored
            // together once cpu0 came up.  They must always agree.
            //
            Device(CLU0) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x1) // unique identifier of the container

                Device(CPU0) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 0)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU1) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 1)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU2) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 2)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU3) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 3)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU4) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 4)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU5) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 5)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }

            //
            // P-core cluster.
            //
            Device(CLU1) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x2) // unique identifier of the container

                Device(CPU6) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 6)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU7) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 7)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU8) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 8)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU9) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 9)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }
        }
    }
 }
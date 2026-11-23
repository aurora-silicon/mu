/** @file
  J813 (Mac17,3 / T8142 / M5) internal keyboard backlight (FPWM0) ACPI device.

  The keyboard backlight on the MacBook Air M5 (J813) is channel 0 of SoC
  fixed-PWM block fpwm0 (/arm-io/fpwm0, child kbd-backlight).  AppleKbdBacklight
  consumes two memory windows using its generic exact-register contract:

    0: fpwm0 register block, 0x3A5040000 + 0x4000 (CTRL +0x00, OFF +0x18,
       ON +0x1C).
    1: PMGR leaf power-state register PS_FPWM0, 0x380700330 + 0x0004 (4-byte
       exact register; parent domains SIO and AFISOCNI2 are virtual on T8142).

  Provenance:
    DeviceTree.j813ap.bin (IPSW 25G76):
      /arm-io/ranges[0] = 0x210000000
      /arm-io/fpwm0 reg = <0x195040000 0x4000> -> 0x3A5040000
      /arm-io/fpwm0 clock-gates = <0x52> -> PMGR device FPWM0
      /arm-io/pmgr reg[0] = 0x170700000 -> 0x380700000, reg_off = 0x330
        -> PS_FPWM0 = 0x380700330
    Verified live on J813 hardware by AuroraBios (AuroraMtpInput.c).

  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("KBL.aml", "SSDT", 0x02, "Apple", "J813KBL", 0x00000001)
{
    Scope (\_SB)
    {
        Device (KBL0)
        {
            Name (_HID, "NTAS0051")
            Name (_UID, Zero)
            Name (_CCA, One)

            Name (_CRS, ResourceTemplate ()
            {
                // Window 0: fpwm0 register block
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x00000003A5040000,
                    0x00000003A5043FFF,
                    0x0000000000000000,
                    0x0000000000004000
                    )
                // Window 1: PMGR exact PS_FPWM0 register (4 bytes)
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000380700330,
                    0x0000000380700333,
                    0x0000000000000000,
                    0x0000000000000004
                    )
            })

            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "ntasp,kbl-pwm-clock-hz", 24000000 },
                    Package () { "ntasp,kbl-pwm-period-ns", 40000 },
                    Package () { "ntasp,kbl-max-brightness", 255 },
                    Package () { "ntasp,preboot-owner", "none" },
                    Package () { "ntasp,preboot-handoff-required", Zero }
                }
            })

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
    }
}

// SPDX-License-Identifier: BSD-2-Clause-Patent
// T6050 / J714s DSDT. First x1n1 milestone: a single boot CPU (matches the
// single-GICC MADT). Device controllers (USB/NVMe/SMC) are intentionally omitted
// until a UEFI shell, then WinPE, boots under x1n1; their J714s MMIO/IRQ mapping
// through the x1n1 carrier has not been established and inventing it is out of
// scope. Add cores CP01.. and controllers once bring-up progresses.
DefinitionBlock ("", "DSDT", 2, "AURORA", "J714WIN", 1)
{
    Scope (\_SB)
    {
        Device (CP00)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
            Name (_STA, 0x0F)
        }
    }
}

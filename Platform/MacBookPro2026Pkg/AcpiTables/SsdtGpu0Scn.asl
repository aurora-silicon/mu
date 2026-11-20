// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 windows profile, G6-S (J804_WINDOWS_SCANOUT builds only): GPU0 with the
// carrier's scanout service. The same table as SsdtGpu0.asl (same OEM table
// ID, same sentinels, resources 1-4 unchanged) plus resource 5, the scanout
// page, and resource 6, its level SPI, so the one WDDM adapter owns its
// vblank interrupt. J804GpuAcpiDxe installs this variant instead of the plain
// one only when /chosen/aura,windows-scanout-v1 validates as well.
//
// This is a TEMPLATE. Nothing per machine is compiled in: every address is a
// unique 64-bit sentinel that J804GpuAcpiDxe replaces from the carrier's
// read-only handoff page (J804GpuHandoff.h J804GpuPatchSsdt; the values below
// are J804_GPU_SENTINEL_MIN/MAX/LEN (k) and J804_GPU_SENTINEL_IPA). The
// driver installs it only when the launcher's /chosen/aura,windows-gpu-v1
// names a page that validates and PrePi reserved the pool; otherwise Windows
// sees no GPU0. It is not part of the DSDT for that reason.
//
// _CRS, in this order (windows-m6/G0-CARRIER-CONTRACT.md 8):
//   1. the sgx window (handoff classes 1..5 as one range; the carrier hooks
//      or denies by class inside it: the foreign pmgr-child/error-handler
//      holes, the CPU_CONTROL and iop-vbar pages are never the guest's);
//   2. the post-office page holding both GFX mailbox windows (only that 16 KiB
//      page, not the rest of the bank, which other coprocessors share);
//   3. the GPU pool: guest RAM that PrePi took out of Mu's RAM and the UEFI
//      memory map reports as EfiReservedMemoryType; Cacheable is a request,
//      whether the driver maps it cached is still open;
//   4. the carrier's handoff page, read-only.
// Not listed: the UAT, __DATA and __TEXT ranges, nor the iop-vbar pages or
// the rest of the post-office bank as ranges of their own.
//
// Interrupt: only the scanout service's virtual SPI (vblank, flip done, flip
// error); the GPU's own lines are still not published (completion is polled). No power methods (_PSx, _PRx, _PRW, _S0W):
// the carrier owns the GPU's power, Windows must never power it.
//
// _DSD (device properties UUID): the handoff page's IPA and size, so the
// driver finds the page without the ADT.
//
#include <J804Windows.h>

DefinitionBlock ("", "SSDT", 2, "AURORA", "J804GPU0", 1)
{
    Scope (\_SB)
    {
        Device (GPU0)
        {
            Name (_HID, J804_GPU0_HID)
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate ()
            {
                // 1: sgx window
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x5AA5010000000000, 0x5AA5015A0FFFFFFF, 0, 0x0000005A10000000)
                // 2: GFX post-office page
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    0x5AA5020000000000, 0x5AA5025A1FFFFFFF, 0, 0x0000005A20000000)
                // 3: GPU pool
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadWrite, 0,
                    0x5AA5030000000000, 0x5AA5035A2FFFFFFF, 0, 0x0000005A30000000)
                // 4: handoff page
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    Cacheable, ReadOnly, 0,
                    0x5AA5040000000000, 0x5AA5045A3FFFFFFF, 0, 0x0000005A40000000)
                // 5: scanout service page (hooked by the carrier, never RAM)
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    J804_SCANOUT_BASE, 0xF60003FFF, 0, J804_SCANOUT_SIZE)
                // 6: its level SPI (vblank, flip done, flip error)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {J804_GSIV_SCANOUT}
            })
            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "aura,handoff-ipa", 0x5AA5A55A5AA5A55A },
                    Package () { "aura,handoff-size", 0x4000 },
                    Package () { "aura,handoff-version", 1 },
                    Package () { "aura,scanout-ipa", J804_SCANOUT_BASE },
                    Package () { "aura,scanout-size", J804_SCANOUT_SIZE },
                    Package () { "aura,scanout-gsiv", J804_GSIV_SCANOUT },
                    Package () { "aura,scanout-version", J804_SCANOUT_VERSION }
                }
            })
        }
    }
}

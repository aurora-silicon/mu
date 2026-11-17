# J813 unified GPU+panel device: host marker and merged-image failure model

Scope: the quiet line (7ad68eb) plus the unified resource table from 8c088cf
(GPUF.asl, SSDT OEM table id AURA817F, `\_SB.GPU0` with `_HID` AURA8171, 16
QWordMemory ranges, no interrupt, no `_CID`). The image boots in the legacy
selection (AURA8170 + AURA8180, exactly as boot 147) unless the host asks for
the unified selection with the marker below. Written before the code that
implements it.

## Failure model (written before the code)

U1. Default boot changes the published tables. With the marker absent, the
    installed sequence must equal the quiet image's byte for byte: AURA817F
    withheld, AURA8170 and AURA8180 published exactly as before (same order,
    same patched G17 bytes, same checksums), nothing added, nothing missing.
U2. Free or Instance errors in either direction of the new decision. AURA817F
    is withheld on every default boot, and AURA8170/AURA8180 are withheld on
    every unified boot. Each withheld table must be freed exactly once, with
    CurrentTable cleared and Instance advanced, through the existing combined
    withhold branch of the device-table loop. A separate withhold path missing
    either step brings back the boot-145 double free (or a re-read of the same
    instance into freed pool, or a loop that never advances) on every boot of
    that direction. The G17 helper's GCD map buffer must be freed once per
    call. Installed tables must be freed once after installation.
U3. Marker parsing accepts a malformed value. Only exactly 4 bytes holding
    little-endian 1 select the unified device. Zero, 2 or 8 bytes, value 0, 2,
    0xFFFFFFFF, or a marker elsewhere than `/chosen` select legacy. A malformed
    marker must never ASSERT.
U4. The decision changes between tables. DISP (AURA818D) is decided before
    GPU (AURA817G) and GPUF (AURA817F) in storage order. All three must read
    one latched selection; a re-read that disagrees would publish two owners
    of the display windows or none.
U5. Published on the wrong machine. Chip-id other than 0x8142, board-id other
    than 0x24, missing chip/board properties, or an ADT without `/arm-io/dcp`
    and `/arm-io/disp0`: AURA8171 is withheld. A marker on an image built
    without the unified gate is ignored.
U6. Ranges overlap RAM or other devices. The six iBoot regions (ranges 3-8)
    must be 16 KiB aligned, inside the 42-bit DRAM window, outside UEFI system
    memory, disjoint from each other; the ten fixed windows must not overlap
    them or any range another published device owns in the same boot (ANS,
    MTP0, N1 AURA8130, CPUF AURA8142, SMCG).
U7. Ranges 3-6 larger than 16 MiB. The unified driver refuses gfx-handoff,
    gpu-region, gfx-shared-region and gfx-shared-l2-region above 16 MiB
    (agxw_j813_resources.c, `r < 4`). Firmware must withhold AURA8171 with an
    error line instead of publishing a device its driver will refuse. DATA
    sizes stay exact (0x134000, 0x130000). Legacy AURA8170 admission is
    unchanged (key-off bytes must not move).
U8. Two GPU0 devices. AURA8170 and AURA8171 both declare `\_SB.GPU0`; at most
    one may be installed. Selecting unified withholds both legacy tables even
    when AURA8171 itself then fails admission (no fallback second owner).
U9. Patch order. AppleG17GpuTableAdmitted patches the six placeholders in
    place; it must run before the USB policy, checksum and install, so the
    installed AURA8171 carries the patched ranges and a valid checksum.
U10. Log noise on every default boot. An absent marker is the normal case and
    must not print a "Missing DeviceTree prop" warning; one DEBUG_INIT line
    states the selection.

## The marker (host to Mu contract)

- Location: property `aura,windows-unified-display-v1` in `/chosen` of the ADT
  the host hands to the guest (the copy Mu reads at `PcdAdtPointer`).
- Format: exactly 4 bytes, little-endian u32, value 1 selects the unified
  device. Absent selects legacy. Any other length or value selects legacy and
  prints one DEBUG_WARN line; it never asserts.
- Host side (Aura launcher, not part of this change): written in `setup_adt`
  next to `aura,windows-cpufreq-v1`, only for a J813 target (chip 0x8142,
  board 0x24) whose profile asks for it; any inherited copy is removed first
  so a key-off ADT is byte-identical to today's. Proposed profile key:
  `"unified_display": "j813-v1"` (absent or `"disabled"` means off).
- Mu side: AcpiPlatformDxe reads it once, the first time an admission helper
  needs the selection, through `dt_get ("/chosen")` and `dt_prop` (no warning
  when absent), and latches the result for every later table. The read only
  counts in images built with `PcdAppleJ813UnifiedDisplay` TRUE (the
  internal-storage-windows-n1-gpu-disp profile); other images ignore it.
- Selected: AURA8170 and AURA8180 withheld; AURA8171 published only when the
  machine is J813 with dcp/disp0 in the ADT and all six iBoot regions pass the
  checks above (including the 16 MiB ceiling on ranges 3-6). Every withhold
  prints one DEBUG_ERROR line naming the reason.

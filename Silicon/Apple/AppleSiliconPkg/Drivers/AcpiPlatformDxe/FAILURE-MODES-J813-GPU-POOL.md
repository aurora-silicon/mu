# J813 G17 local-memory pool: failure modes (written before the code)

Written 2026-11-21 21:56 EST (`date`), before any test or code, on branch
codex/j813-mu-gpu-pool cut from 094f1e5. Design: option A of
j813/work/one-tree/rules/SHARED-SURFACES.md, sections 0-4. Desk only: nothing
here has booted.

## The rule under test

- **What.** One physically contiguous range of 1 GiB of guest RAM, reserved by
  Mu for the G17 driver's local memory segment (segment 2) and published to it
  as one more QWordMemory resource of AURA8171. Nothing else Windows sees
  changes.
- **Gate.** The pool exists only when all of these hold; otherwise nothing
  about it exists (no reservation, no HOB, no descriptor, no log line):
  - the image is built with `PcdAppleJ813GpuLocalPool` (new, default FALSE)
    and `PcdAppleJ813UnifiedDisplay`;
  - the host's `/chosen/aura,windows-unified-display-v1` is exactly a 4-byte
    little-endian 1 (the rule AuraJ813UnifiedDisplay.c already applies);
  - `/chosen` chip-id is 0x8142 and board-id is 0x24.
  The default image (gate FALSE) is unchanged in every profile, key-on
  included, so this branch can merge before the driver's segment 2 exists.
- **Where it comes from.** MemoryPeim (T8142 MemoryInitPeiLib) carves it out
  of guest RAM before DXE, as `EFI_RESOURCE_MEMORY_RESERVED`, the same way the
  MTP handoff is carved (Docs/windows-mtp-memory.md). MU_BASECORE 11a8cedb
  turns that HOB into `EfiGcdMemoryTypeReserved` (Gcd.c:2687-2688) and
  `CoreGetMemoryMap` reports it as `EfiReservedMemoryType` (Page.c:2049-2070).
  It is therefore outside UEFI system memory: the rule AuraG17GpuDevice.c:5-14
  applies to the six iBoot GPU regions applies to the pool unchanged.
- **Placement.** The highest window inside one system-memory resource HOB
  (after every existing hole is punched) that is aligned on both ends and
  overlaps neither the initial UEFI region, nor the FD, nor an ADT
  `/chosen/carveout-memory-map` region-id-2/-4 range.
- **Size and alignment.** Exactly 1 GiB (ruling 2). Base and top aligned to
  32 MiB. The minimum that works is 64 KiB: VidMm places allocations at
  64 KiB-aligned segment offsets, the GPU walks 16 KiB pages, and AArch64 UEFI
  keeps reserved memory on 64 KiB granules (AcpiPlatform.c:542-548). 32 MiB is
  chosen because it is the level-2 block size of a 16 KiB-granule table, both
  the GPU's and Aura's stage 2 (hv_vm.c VADDR_L2_OFFSET_BITS = 25), and a
  multiple of the CPU's 2 MiB large page. The cost is at most 32 MiB of RAM
  between the pool and the next hole, which stays ordinary system memory.
- **Handoff to DXE.** One GUID HOB (`AURA_J813_GPU_POOL`, new header
  IndustryStandard/AuraJ813GpuPool.h) with signature, version, size, base,
  size and the guest-RAM window MemoryPeim used.
- **Publication.** A second unified template, GPUP.asl (OEM table ID
  "AURA817P"), is GPUF.asl's 16 descriptors in the same order and bytes plus a
  17th: the pool, as `QWordMemory (ResourceConsumer, PosDecode, MinFixed,
  MaxFixed, WriteCombining, ReadWrite)`. The gate picks the template: gate on,
  GPUF is withheld and GPUP is the only AURA8171 candidate; gate off, GPUP is
  withheld and GPUF behaves as at 094f1e5. GPUP admits only when every GPUF
  rule passes and the pool HOB validates.
- **Attribute.** WriteCombining, the design's first CPU mapping (S8).
  Microsoft: cached CPU-accessible allocations cannot be locked in a memory
  segment, so a cacheable flag would describe a mapping nobody makes.

## Failure modes

Each line names what would go wrong, then how it is prevented (P) and how the
host test shows it (T). "Test" is Tests/e2e/j813-gpu-pool.py unless stated.

**G1. The range overlaps memory Windows is given.**
- Ways: carved from the wrong HOB; a split HOB keeps covering part of it; it
  overlaps the initial UEFI region (DXE core allocates there first), the FD
  or an allocation HOB; the arithmetic wraps; a HOB that is not system memory
  is "reserved" (no-op) and the GPU then owns RAM Windows also owns.
- P: carve only inside one system-memory HOB; exclude the UEFI region and FD
  explicitly; DXE re-reads the GCD and refuses unless every byte is GCD
  Reserved and none is SystemMemory, MoreReliable or Persistent.
- T: no system-memory HOB overlaps the pool; exactly one reserved HOB equals
  it; RAM accounting balances (system + reserved = guest RAM, system shrinks
  by exactly 1 GiB against 094f1e5); DXE withholds AURA8171 for a pool HOB
  over system memory or over a GCD range that is not wholly Reserved.

**G2. The range overlaps the iBoot GPU regions or another reserved or device
range.**
- Ways: overlaps one of the six sgx regions (they sit outside system memory,
  so the system-memory rule alone would not catch a pool placed over them);
  overlaps MTP, the copied boot handoff, a CPU carveout, the appended
  ramdisk, the FD, the UEFI region or a TZ carveout; overlaps a fixed MMIO
  window of any published device.
- P: PEI holes and the forbidden list above; DXE compares the pool with the
  six regions before patching.
- T: overlap checked against every QWordMemory of every installed table and
  every reserved HOB; a region fixture moved inside the pool withholds
  AURA8171; a TZ carveout over the top window moves the pool below it.

**G3. The range lies outside Aura's stage-2 identity map.**
- Ways: outside the guest RAM Aura hands Mu (`phys_base`, `mem_size`); over a
  TZ carveout Aura unmaps; above the 42-bit physical limit or below DRAM.
- What source settles (public j813-aura-public 3bcc90be;
  internal aura-internal 98599af2):
  - guest RAM is `[heap_top, ba.phys_base + ba.mem_size)`:
    proxyclient/aura/hv/__init__.py:2082, 2094, 2171-2172 (internal 1906,
    1918, 1995-1996);
  - it lies inside RAM-HIGH `[heap_top, ram_base + mem_size_actual)`, mapped
    with TraceMode.OFF (2143-2144; internal 1967-1968), which pt_update maps
    identity, `map_hw(mzone.start, mzone.start, ...)` (387-388; internal
    363-364) with PTE_VALID hardware entries (198-221; internal 174-196);
  - holes: TZ carveouts only (unmap_carveouts, 2040-2050; internal
    1864-1874), from the MCC TZ windows or iBoot's ADT list (src/mcc.c:390,
    321, 295-305; internal mcc.c:253, TZ windows only), each inside DRAM;
  - the C side adds no alias inside guest RAM: the Mu ADT alias sits at IPA
    0x5300000000 (windows/hv.c:1239, windows_compat.h:19), the framebuffer
    shadow and SPRR tables are Aura heap, below guest RAM.
- Not settled by source: where the TZ carveouts are on J813 (runtime MCC/ADT
  values), and whether the MCC windows are wider than the ADT's region ids.
- **Boot 179 record** (desk, 2026-11-21 22:16 EST from `date`; the
  coordinator read boot 179's console.log lines 144-161, I did not). Aura's
  stage-2 map over DRAM, as printed:
  - `PT[10000000000:10003094000] -> HW:RAM-LOW`
  - `PT[100129d4000:10012ad4000] -> RESERVED HVXFER-WIN` (below guest RAM)
  - `PT[1001a9c0000:103dc694000] -> HW:RAM-HIGH`
  - `PT[103dc694000:103de6b4000] -> *UNMAPPED*` (hole A, 0x2020000 bytes)
  - `PT[103de6b4000:103e6bf8000] -> HW:RAM-HIGH`
  - `PT[103e6bf8000:103ffbf8000] -> *UNMAPPED*` (hole B, 0x19000000 bytes)
  - `PT[103ffbf8000:10400000000] -> HW:RAM-HIGH`
- **ADT or wider: not known.** The test's carveouts are synthetic
  (Tests/e2e/j813-gpu-pool.py:74-75: 0x103E0000000+0x4000000 and
  0x103F4000000+0x2000000, or one moved into the top window); no J813 ADT
  value was used, and neither matches holes A or B. The lines the log quotes
  cannot say where a hole came from. The Aura lines that would:
  - src/mcc.c:357 `MMU: Unmapping region-id-N at start..end` (the ADT list in
    use, so the hole is exactly the region id);
  - src/mcc.c:452 `MMU: Unmapping TZn region at start..end` (the MCC
    windows in use; mcc.c:368-388 only requires each region id inside one
    window, so the hole may be wider);
  - src/mcc.c:445, the notice that the MCC windows missed the ADT list;
  - proxyclient/aura/hv/__init__.py:2048 `  Unmap [base..last]` gives each
    unmapped range, not its source.
  None of these changes the pool's placement; see the margins below.
- **Boot 179's window: [0x10394000000, 0x103D4000000)**, the same as boot
  147's. It comes from the production MemoryPeim (the j813-gpu-pool.py PEI
  host at d0f7d4a): RAM base 0x1001a9c0000 (RAM-HIGH's start, Aura's
  heap_top), top 128 MiB as the UEFI region, the FD at 0x1001ba50000+0x1e00000,
  holes A and B listed as the ADT's region-id-2/-4, and again with no list.
  - Both ends are 32 MiB aligned; size 0x40000000.
  - The window lies inside the first RAM-HIGH segment, which ends at
    0x103dc694000.
  - Clearance from hole A: 0x8694000 bytes (134.58 MiB) between the window's
    top and the hole's start.
  - Clearance from hole B: 0x12bf8000 bytes (299.97 MiB).
  - The log does not print boot 179's guest-RAM top (`ba.phys_base +
    ba.mem_size`); hv/__init__.py:2106 `Physical memory: phys_base ..
    mem_top` would, as would Mu's own `Top of system RAM`
    (MemoryInitPeiLib.c:898). Every top in [0x103DC000000, 0x103DE000000)
    gives this same window, so 0x103dc600000 (boot 147) and 0x103dc694000
    (the start of hole A, where RAM-HIGH's first segment ends) do too. The
    window's top is always at least 128 MiB below the guest top, so any
    guest top up to 0x103E4694000 keeps the pool below hole A; hole B is
    above that bound.
- P: the pool must lie inside the guest-RAM window MemoryPeim was given and
  outside region-id-2/-4 when the ADT lists them; DXE checks DRAM and 42-bit
  bounds.
- T: the pool is inside the identity set computed from the Aura formula over
  the fixture's boot arguments and carveouts.

**G4. Not enough contiguous free RAM.**
- Ways: no aligned 1 GiB window; a partial reservation; a smaller pool
  published; PEI stops the boot.
- P: no window means no reservation and no HOB, and MemoryPeim still returns
  success; gate-on DXE then withholds AURA8171 (no fallback to the 16-resource
  template, which the gate-on driver does not expect; the legacy pair stays
  withheld as for any unified failure). Basic display remains.
- T: a small-RAM case reserves nothing, builds no HOB and installs no GPU
  table, and every other HOB equals 094f1e5's.

**G5. Misalignment.**
- Ways: base or top not 32 MiB aligned; size not exactly 1 GiB; Min, Max and
  Length of the descriptor disagree with the HOB.
- P: PEI aligns both ends; DXE refuses a HOB that is not aligned or not
  exactly 1 GiB.
- T: descriptor Min/Max/Length equal the reserved HOB; injected misaligned or
  wrongly sized HOBs withhold AURA8171.

**G6. The range published to a device other than AURA8171.**
- Ways: a placeholder or patch lands in GPU.asl (AURA8170), DISP.asl
  (AURA8180) or another table; the pool is published by the legacy path.
- P: only GPUP carries the pool placeholder; only the unified branch patches
  it.
- T: across every installed table, exactly one descriptor overlaps the pool
  and it is in the AURA8171 table.

**G7. The key-off profile changes.**
- Ways: PEI reserves or logs on a key-off boot; a new table is installed; the
  ADT is read differently; GPUF's bytes change.
- P: the gate is read first and nothing runs when it is off.
- T: for the key-off cases (marker absent, 0, short, long, 2; image without
  the unified PCD) with the pool PCD on, every resource HOB, allocation HOB,
  GUID HOB and installed table is byte-identical to 094f1e5's. The same holds
  for key-on with the pool PCD off. Tests/e2e/acpi-table-loop.py and
  memory-peim.py keep passing.

**G8. The range published twice, or published when the GPU device is
withheld.**
- Ways: GPUF and GPUP both installed (two \_SB.GPU0, a namespace collision);
  two pool descriptors in one \_CRS; two pool HOBs; a placeholder left
  unpatched (0xA170006000000000 published); a pool descriptor in a table that
  was otherwise withheld.
- P: the gate selects exactly one template; PatchDescriptor requires its
  placeholder exactly once; DXE refuses a second pool HOB.
- T: at most one AURA8171 table installed and never both templates; exactly
  17 descriptors in it; no placeholder value in any installed table; with dcp
  absent, wrong chip or a bad region, no table carries the pool.

**G9. PEI and DXE disagree about the key.**
- Ways: PEI reserves for a marker shape DXE rejects (1 GiB lost on a boot that
  publishes the legacy pair); DXE publishes a pool PEI never reserved.
- P: the same exact rule in both; DXE trusts only a HOB whose range is GCD
  Reserved.
- T: every marker variant runs through both stages on the same ADT.

**G10. Pool reserved but its device withheld** (dcp absent, a region refused,
not J813 after PEI's check, table malformed).
- Accepted: the 1 GiB stays reserved and unpublished for that boot. There is
  no free-back path; freeing firmware-visible memory on a failure path is what
  the unified design forbids (Tests/e2e/j813-unified-display-failures.md item
  5), and reserved-page free-back deadlooped before (AcpiPlatform.c:1405-1441).
- T: the case is run; the reservation is present and no table carries it.

**G11. ReserveMemoryRegion takes a UINT32 size.** A future size of 4 GiB or
more would be truncated. P: a STATIC_ASSERT on the pool size.

## Not Mu's, recorded for the kernel side

- **K1.** UEFI's own page tables map all of DRAM write-back during boot
  services (MemoryInitPeiLib BuildVirtualMemoryMap), the pool included, and
  the memory map entry carries Attribute 0 like MTP's. Clean and invalidate
  the pool before the first write-combined use.
- **K2.** Mu does not clear the pool. Its contents at driver start are
  undefined.
- **K3.** A gate-on image publishes 17 resources on AURA8171; a driver that
  requires exactly 16 refuses the device. Flip the gate only with a driver
  that accepts the 17th.
- **K4.** A later DXE claim of a fixed address inside guest RAM would now fail
  if it fell in the pool. None exists in the non-banned Mu sources read
  (AllocateAddress/AddMemorySpace sites: AppleNANDStorageDxe, the T602X GPU
  path). The final memory map is read on SS-1.

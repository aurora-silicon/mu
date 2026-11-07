# G17 reservation-only handoff failure model

This model precedes implementation. The producer is public Aura; this consumer
uses public ADT and BootArgs metadata only. The opt-in record admits J813 / T8142
and M5-RTKit-3255.160.4.release exclusively. It is not permission to start, reset,
halt, overwrite, adopt UAT roots, or expose the GPU to Windows.

Failures the artifact E2E must exercise:

- Missing marker preserves the existing boot. A present malformed, truncated,
  oversized, CRC-invalid, unknown-version/flag/tag or nonzero-reserved record is
  rejected before any reservation callback.
- A stale/wrong chip, board, model, firmware profile, BootArgs revision, guest
  span, installed DRAM span, SGX range, or ASC TEXT/DATA physical segment layout is
  rejected. Firmware identity is Aura's guarded TEXT admission assertion; Mu
  does not read or write the firmware pages.
  ASC IOVA/remap/unknown words are not admitted as an ownership contract by this
  reservation-only revision; physical spans alone determine exclusions.
- Zero, unaligned, overflowing, over-42-bit, out-of-DRAM, overlapping or
  partially-in-guest ranges are rejected. Classifications must be computed from
  the current guest bounds, not trusted. Exact DATA/TEXT sizes and sufficient
  shared-region room for both UAT roots are mandatory.
- Class-1 spans must be wholly unallocated system RAM before any split. Existing
  resource/allocation HOB collisions (FD, PEI stack/heap, boot handoff, appended
  ramdisk, MTP, secondary stacks) are fatal. All preflight checks precede edits.
  Failure after a split is fatal and never releases any successful exclusion.
- PEI reservations alone are too late for EarlySetup's fixed BootArgs/ADT copies
  and the assembly stack clear. Before either, validate the original ADT and
  BootArgs, then reject GPU overlap with the actual copy destinations, runtime
  FD, framebuffer, and exact assembly-derived UEFI workspace (including the
  legacy near-FD fallback top of FdBase+1). An EarlySetup FALSE return is ignored
  by existing assembly, so rejected admission must halt directly. Source ADT
  must be bounded and checked before traversal. Aura separately guards the raw
  payload/temporary relocation stack, which execute before the C gate.
- Class-2/3 spans are outside guest RAM and must not intersect any system-memory
  or allocation HOB. They are recorded, not inserted as usable guest memory.
- Validated metadata crosses PEI/DXE in a bounded GUID HOB; absent, duplicated,
  malformed or mismatched handoff state is fatal when the opt-in marker exists.
- DXE verifies every covered EFI descriptor is reserved; class-1 coverage must
  be complete, class-2/3 may be absent but must never be allocatable. A failed
  verification stops the opt-in boot rather than exposing unsafe memory.
  Descriptor intervals must also be nonempty, page-aligned, nonwrapping and
  pairwise disjoint. In particular, duplicate reserved half-span descriptors
  plus an omitted other half must not counterfeit complete byte coverage.
- Exclusions have whole-boot lifetime, remain EfiReservedMemoryType at boot
  handoff, and have no release path. Validation does not establish DMA quiescence
  or permit freeing the original firmware allocations after a timeout.

Evidence is an offline host-produced record through the same portable admission
and reservation policy used by PEI, plus production firmware compilation. It is
not hardware or Windows driver qualification. Receipts bind input, source and
output SHA-256 and include repeatable commands.

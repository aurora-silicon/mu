# J813 copied boot handoff memory

## Failure model before implementation

EarlySetup copies boot arguments and the ADT to fixed physical addresses. Aura
includes those addresses in the guest RAM range. Without a resource reservation,
DXE can allocate over the copies while AppleDTLib and later DXE drivers still use
them. The firmware image, an appended disk, or the initial UEFI stack/heap could
also overlap that fixed window. A zero, oversized, wrapped, or out-of-RAM ADT
length must not create a misleading reservation. Splitting resource HOBs must
preserve both adjacent RAM ranges, including windows touching either boundary.
An existing hole or allocation must cause failure rather than silent success.

The isolated integration test compiles and executes the production T8142
MemoryPeim source with modeled HOB services and a bounded host RAM arena. It
checks the resulting resource and allocation map, not a second implementation
of the reservation algorithm. Only firmware-only services (MMU installation,
PCDs, ADT CPU lookup, and allocation) are substituted. It must demonstrate the
missing reservation on the unmodified source first. No MMIO or target hardware
is accessed. The receipt records source hashes, commands, and case results.

This test does not prove early target execution or the real DXE allocator. The
physical boot still needs to validate that the fixed copy destination is free
before EarlySetup writes it; a later MemoryPeim rejection cannot undo an earlier
overlap. The observed Aura guest base is far below this window. Moving the fixed
copy into a dynamically allocated, explicitly handed-off region remains a
separate design change.

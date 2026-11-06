# J813 firmware forwarding contracts

## Failure model before implementation

The N1 census must not execute unguarded guest MMIO. An absent service, echoed
request, wrong acknowledgement/version, nonzero status, or value wider than
32 bits must withhold the device. There is no direct-read fallback. Descriptor
or identity rejection must still issue zero HVCs. Aura owns the address whitelist,
hardware fault recovery, successful-producer requirement, and boot-CPU check.

The firmware exception assist must handle only synchronous undefined AArch64
system-register instructions. Other exceptions, malformed/aligned/wrapped PCs,
non-system instructions, rejected HVCs, and unexpected acknowledgements must
leave the exception context unchanged and use the existing local handler.
Successful reads modify only their destination register and advance ELR once;
writes modify no saved general register. Register 31 means zero/discard, never
the saved stack pointer. The helper must not recurse into itself while servicing
an exception. A failed instruction fetch still belongs to the ordinary fatal
exception path; this mechanism is not a general memory-probing service.

The pinned TIANO exception entry supports a J813-specific stack choice. The
overlay must use its existing SP_EL1 synchronous entry and allocated EL0 stack,
not revive a static image-data exception stack or implement new assembly save
rules. This choice is selected only in the J813 library override. Upstream
source changes and hidden dirty modifications must be detected before builds.

Host integration tests will execute the production forwarding code with a
modeled HVC boundary, recording requests, context changes and failed replies.
The N1 census tests will run through that actual adapter. Full firmware builds
must show the explicit exception-library override and compiled HVC calls. These
checks do not establish target exception return, interrupt delivery, or Windows
boot. Those need separately recorded J813 hardware runs.

## Public inputs and overlay

The exception entry and ordinary callback/fatal dispatch come from the pinned
public `Silicon/ARM/TIANO` commit
`7b3db1e6fe68d5d456512a88c217ec1dcde31e3b`. The NTSRG ABI is read from the committed
Aura Windows backend, not from the dirty original Mu checkout. The new C wrapper
includes pinned source directly. EDK2's `Trim --source-code` removes assembly
inside preprocessor includes, so the assembly file contains an exact copy from
the public pinned Git object, with five prefix lines selecting its existing
J813 stack behavior. The build guard verifies that its body remains identical.
This does not copy or modify the dirty original checkout's implementation.

The immutable public ABI source is `aurora-silicon/m1n1` commit
`69534d4899e20108d2c7a085565a7097509fa17f`, `src/hv_exc.c` (SHA-256
`4b08e239f0dc20ce7bd09cdc7e4dff61c244b66b000f578ead6e7694b3510a4e`),
whose dispatcher returns `handled` in x0 and the result in x1. The build script
runs `Tools/check-j813-exception-base.py` to reject modifications to the three
public TIANO files included by this overlay. Other platforms continue selecting
the ordinary exception library.

NTSRG uses HVC #0: x0=`0x4e54535247`, x1=instruction, x2=source register value.
An exact x0=1 reply accepts the operation; x1 supplies a read result. Any other
x0 falls through to the pinned firmware handler. The pre-existing ABI has no
distinct response magic, so an exact 1 is the full acknowledgement it supplies.

Repeat the offline checks with new output directories:

```sh
python3 Tests/e2e/runtime-forwarding.py --output Build/runtime-forwarding-new
python3 Tests/e2e/n1-handoff.py --output Build/n1-forwarding-new
```

The first campaign covers 50 saved-context, rejection, and reentrancy scenarios.
The second sends all 21 descriptor/census scenarios through the production HVC
adapter before validating the real compiled SSDT. Each preserves source copies,
commands, logs, and hashes. The full J813 base and N1 builds provide the separate
compiled firmware evidence; neither campaign executes AArch64 exception return.

N1 census uses HVC #0: x0=`0x4e3152443332`, x1=physical address, x2=1, x3=0.
Success requires x0=`0x4e3141434b31`, x1=0, x2<=`UINT32_MAX`, x3=1. No reply is
accepted on the basis of a plausible register value alone.

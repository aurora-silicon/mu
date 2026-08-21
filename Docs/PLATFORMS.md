# Adding a machine, and where its facts live

## The question

Every per-machine address in this tree could come from the ADT instead. iBoot
hands us a complete device tree for the machine actually booting: register
windows, interrupt lines, DART bases, power-domain paths. So why is
`AcpiPlatformDxe/AcpiPlatform.c` 4,921 lines with 31 hardcoded 64-bit MMIO
literals in it?

Mostly because nobody decided. The literals came from the ADT in the first
place: `verify-j414s-av-adt.py` in the drivers repo read them off a live J414s
and someone pasted the output into C. They are a copy with no link back to
their source, sitting in `Silicon/Apple/AppleSiliconPkg` — the package every
machine compiles.

## The answer

Read from the ADT what the ADT already states as fact about this machine.
Author what we decided about how Windows should see it.

That line is not a compromise between two positions. It falls out of what ACPI
is for. Take the media device description, which is the densest example:

```c
{
  "MCA0", "NTAS0080", "J414MCA",          // authored
  mNtasiMcaWindows,     9 entries,        // ADT fact
  mNtasiMcaInterrupts,  5 entries,        // authored
  mNtasiMcaProperties,  n entries         // authored
}
```

The windows are where the hardware is. The ADT knows; `AppleDTLib` already has
`dt_node_reg(node, index, &base, &size)` to ask.

The interrupts look like they should be ADT facts and are not. MCA0's published
GSIVs are 40, 41, 42, 43, 45. Its physical AIC lines are 1211, 1213, 1218,
1221, 1231 — all above the GIC carrier's 1019 limit and illegal as GSIVs, so
the CSRT's ALI2 tail translates them. That renumbering is ours. It has to match
m1n1's alias table and the CSRT, and it exists nowhere in the ADT.

Neither are the rest. `"NTAS0080"` is a `_HID` we invented. `ntasp,mca-cluster-count`
is a `_DSD` property name our driver reads. `_STA`, `_PS0` and power resources
are AML methods, which is code, not data — you cannot synthesise them from a
device tree without an AML compiler at runtime.

So: **windows from the ADT, everything else authored.** That is 31 literals, and
it is the whole of what dynamic buys.

## Why not go further

Three reasons, in order of how much they cost.

**The failure mode inverts.** A missing static table entry is a build error. A
missing ADT property is a device that silently does not appear, on a machine
you may not have in front of you. For a device Windows needs in order to boot,
that is the difference between a broken build and a brick.

**ACPI is a contract, not a description.** Windows drivers bind to `_HID`, read
`_DSD` by name, and acknowledge specific GSIVs. None of that is a fact about
the silicon. Generating it from a device tree means inventing the contract at
runtime, on the machine, where you cannot see it.

**Property names are not stable.** The ADT is Apple's, and it changes between
iBoot versions. A static table that disagrees with the machine is a bug you can
find by reading. A dynamic lookup that returns nothing is a bug you find by
bisecting boots.

## The rule for the windows we do move

The literal does not go away. It becomes the pin.

```c
{ 0x39B600000ULL, 0x10000ULL, "/arm-io/mca0", 0 },
```

Resolve the path. If it resolves and matches, nothing changed. If it resolves
and disagrees, say so loudly and keep the literal, because a disagreement means
the path-to-window mapping is wrong and the literal is the one that was checked
on hardware. If there is no literal — a new machine — the ADT supplies it.

That gives a new machine no literals to transcribe, keeps J414s behaviour
identical, and turns a silent transcription error into a message.

## What adding a machine costs today

| | Lines | Where it goes |
| --- | --- | --- |
| `PlatformBuild.py` | 38 | boilerplate over `PlatformBuildCommon` |
| `.dsc` / `.fdf` / `.dec` | ~700 | mostly FV layout, still copied |
| `DSDT.asl` + tables | 240–680 | authored, and rightly so |
| Build profiles | 0 | `Platform/Profiles.py`, if it needs any |
| SoC family package | ~1,300 | once per SoC, not per machine |

The build script is solved. The profiles are solved. The `.dsc`/`.fdf` pair is
the next copy-paste to attack, and the ACPI tables are the part that should
stay hand-written, because that is the part that is a decision.

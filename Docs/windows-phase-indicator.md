# Windows ExitBootServices phase indicator

## Failure model recorded before the fix

The J813 diagnostic-03 hardware capture stops in `DebugAssert` in
`MuVarPolicyFoundationDxe`, at image RVA `0x1994`. Its entire 6,840-byte `.text`
matches the built module. The saved frame identifies `SetPhaseIndicator` line
71, called directly by `SetExitBootServicesIndicator`; the saved status is
`0x8000000000000002` (`EFI_INVALID_PARAMETER`). The request creates `EBS`, with
attributes `BS | RT` (6) and a one-byte true value.

The selected public `UefiRuntimeLib` changes its runtime flag from an
ExitBootServices event at `TPL_NOTIFY`. The phase driver's event at
`TPL_CALLBACK` executes later. `Variable.c` rejects creation of a volatile
variable after that transition. Suppressing the assertion, allowing volatile
runtime writes, changing the indicator to nonvolatile, or removing the phase
policy would conceal or weaken the actual contract.

Other failure cases the fix must preserve or address:

- Missing EndOfDxe or ReadyToBoot must still create the earlier indicators.
- A failed first ExitBootServices memory-map key and a later retry must not
  create a second write to the locked indicator.
- Duplicate phase notifications must remain harmless.
- Allocation or variable-service failures must still reach the assertion.
- Both namespaces must retain their exact size, attributes, and lock-on-create
  policies. The indicators must disappear on reboot.
- Non-Windows profiles and the pinned Common/MU submodule must remain unchanged.

## Event boundary

For the explicit `NTASI_AURA_WINDOWS` profile, a root-owned component includes
the pinned public phase driver while redirecting only its ExitBootServices
event to `gEfiEventBeforeExitBootServicesGuid`. DXE signals that group once,
before validating/terminating the memory map and before any runtime transition.
The indicator means the irreversible attempt to hand control to the OS has
begun. If the map key is stale, it remains set: security phase locks are
monotonic, and retrying the handoff must not reopen them. No policy or assertion
is removed. Existing profiles keep the original component.

## Validation boundary

The host replay compiles the complete production phase driver and its proposed
wrapper, drives the registered callbacks, and enforces the relevant volatile
variable and lock-on-create rules at a modeled firmware-service boundary. It
also executes the runtime-flag callback extracted verbatim from the pinned
public runtime library. This reproduces event-ordering failures and validates
retry and missed-phase behavior; it is not a replacement for a firmware boot.
The executable, inputs, compiler command, logs and hashes are retained in a
repeatable receipt. A final Windows hardware boot must establish progress past
this assertion.

Run the replay from the repository root using fresh output directories:

```sh
python3 Tests/e2e/phase-indicator.py --expect-unfixed --output Build/phase-before
python3 Tests/e2e/phase-indicator.py --output Build/phase-after
```

`Tools/check-j813-phase-policy.py` verifies the driver, INF and public header
against Common/MU commit `ba613eea328e801d9582059a8e0af414097a7fa8` before both
Windows profiles build. The build report must select exactly
`AppleSiliconPkg/Drivers/AuraVarPolicyFoundationDxe/AuraVarPolicyFoundationDxe.inf`;
the generated module defines the BeforeExitBootServices GUID. The original
component remains selected when `NTASI_AURA_WINDOWS` is false or undefined.

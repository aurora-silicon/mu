# Building Mu

AuroraSilicon uses native incremental builds. Docker is not part of the build
workflow.

Preferred entry point:

```sh
abg boot prepare [--no-ans] [--no-wireless] [--no-xhc2] [--usb3-left]
```

ABG selects a safe profile, verifies prerequisites, rebuilds only when the
source fingerprint changed, and verifies the sealed artifact. Add
`--force-build` only to discard that cache decision.

Direct firmware build:

```sh
Tools/build-windows-native.sh j414s <profile>
```

The authoritative profile list is `PROFILES` in
`Tools/mu_profile_manifest.py`. Native output defaults to the main
AuroraSilicon repository:

```text
../AuroraSilicon/build/m2-pro/<profile>/<source-commit>/
```

Override it with `AURORADBG_MU_OUTPUT_ROOT`. Required host tools are Python
3.11 or 3.12, Homebrew LLVM/lld, `iasl`, `nasm`, and `make`.

First time in a fresh checkout:

```sh
Tools/setup-build.sh [reference-checkout]
```

That pins every submodule, including nested ones, to the commit this tree
records, and seeds the BaseTools external dependency. Both matter and both fail
obscurely if skipped: `--reference` alone leaves submodules on the reference's
HEAD, which surfaces as BaseTools failing to find
`brotli/c/common/constants.h`, and a fresh tree with no extdep makes stuart
refuse the host with `Verify support for detected host: Host(os='MacOs',
arch='ARM', bit='64')`.

With a sibling mu checkout to seed from it needs no network and copies
clone-on-write, so it costs almost no disk.

Verify an artifact without rebuilding:

```sh
.venv-native/bin/python Tools/mu_profile_manifest.py verify \
  --manifest <artifact-directory>/manifest.json \
  --source-root .
```

Only DEBUG/CLANGPDB firmware is currently supported. Machines are entries in
`TARGETS` at the top of `Tools/mu_profile_manifest.py`, which is where the
platform build directory, the FD name and its expected size live; adding a Mac
is a line there, not a copy of the tool.

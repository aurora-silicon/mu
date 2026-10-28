# Pinned J873 dependency changes

`../build-j873-native.sh` invokes `../apply-j873-patches.py` before building.
Initialize the repository's pinned submodules first. The helper refuses a
different revision or conflicting changes and accepts an already applied patch.
The submodule gitlinks remain at their published commits.

- Common/MU: run the Windows ExitBootServices phase-indicator callback at
  TPL_NOTIFY, before VariableRuntimeDxe transitions. Keep failure assertions.
- Silicon/ARM/TIANO: use a UINTN alignment mask for an EL0 exception stack
  above 4 GiB when compiling with the LLP64 CLANGPDB target.

These are the dependency changes used during J873 Mu/Windows PE bring-up.
Generated dependency caches and private Windows images are not source inputs.

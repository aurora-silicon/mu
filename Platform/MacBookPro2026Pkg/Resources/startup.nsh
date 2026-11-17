@echo -off
echo J804_MU_FIRMWARE_READY
echo J804_WIN_PROFILE windows (ACPI only at EL1 under the carrier)
J804BootProbe.efi
ver
map -r
echo J804_WIN_STARTUP_DONE resetting in 30 s (EL1: PSCI to the carrier; EL2: WD1)
stall 30000000
reset

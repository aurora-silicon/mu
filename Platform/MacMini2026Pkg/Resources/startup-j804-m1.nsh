@echo -off
echo J804_MU_FIRMWARE_READY
J873BootProbe.efi
ver
map -r
echo J804_M1_STARTUP_DONE no reset in M1 hold power to exit

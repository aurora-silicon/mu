# Host test of the appended-ramdisk ABI (AppendedRamdisk.h) against a built image.
# usage: make && ./appended_ramdisk_test hdr.bin WINPE.img
# The images are built from Microsoft media: pass their paths, never commit them.

MU ?= ../../..
CFLAGS := -std=gnu11 -O2 -Wall -Wno-unused-function -Ishim -I$(MU)/Silicon/Apple/AppleSiliconPkg/Include
appended_ramdisk_test: appended_ramdisk_test.c $(MU)/Silicon/Apple/AppleSiliconPkg/Include/AppendedRamdisk.h
	cc $(CFLAGS) -o $@ appended_ramdisk_test.c
clean:
	rm -f appended_ramdisk_test

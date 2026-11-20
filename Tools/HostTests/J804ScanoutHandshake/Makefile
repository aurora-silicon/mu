# Host test of the G6-S scanout handshake parser (J804ScanoutHandshake.h), the
# code J804GpuAcpiDxe runs on /chosen/aura,windows-scanout-v1 in a
# J804_WINDOWS_SCANOUT build. No inputs, no private data.
# usage: make && ./scanout_handshake_test

MU ?= ../../..
INC := $(MU)/Platform/MacBookPro2026Pkg/Include
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ishim -I$(INC)
scanout_handshake_test: scanout_handshake_test.c $(INC)/J804ScanoutHandshake.h $(INC)/J804Windows.h
	cc $(CFLAGS) -o $@ scanout_handshake_test.c
clean:
	rm -f scanout_handshake_test

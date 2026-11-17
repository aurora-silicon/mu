# Host test of the D1 AHCI handshake parser (J804AhciHandshake.h), the code
# J804AhciDxe runs on /chosen/aura,windows-ahci-v1. No inputs, no private data.
# usage: make && ./ahci_handshake_test

MU ?= ../../..
INC := $(MU)/Platform/MacBookPro2026Pkg/Include
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ishim -I$(INC)
ahci_handshake_test: ahci_handshake_test.c $(INC)/J804AhciHandshake.h $(INC)/J804Windows.h
	cc $(CFLAGS) -o $@ ahci_handshake_test.c
clean:
	rm -f ahci_handshake_test

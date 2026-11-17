# Host test of the I0 input handshake parser (J804InputHandshake.h), the code
# J804InputAcpiDxe runs on /chosen/aura,windows-input-v1. No inputs, no
# private data.
# usage: make && ./input_handshake_test

MU ?= ../../..
INC := $(MU)/Platform/MacBookPro2026Pkg/Include
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ishim -I$(INC)
input_handshake_test: input_handshake_test.c $(INC)/J804InputHandshake.h $(INC)/J804Windows.h
	cc $(CFLAGS) -o $@ input_handshake_test.c
clean:
	rm -f input_handshake_test

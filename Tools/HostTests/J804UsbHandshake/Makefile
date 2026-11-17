# Host test of the D4 USB handshake parser (J804UsbHandshake.h), the code
# J804XhciAcpiDxe runs on /chosen/aura,windows-usb-v1. No inputs, no private
# data.
# usage: make && ./usb_handshake_test

MU ?= ../../..
INC := $(MU)/Platform/MacBookPro2026Pkg/Include
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ishim -I$(INC)
usb_handshake_test: usb_handshake_test.c $(INC)/J804UsbHandshake.h $(INC)/J804Windows.h
	cc $(CFLAGS) -o $@ usb_handshake_test.c
clean:
	rm -f usb_handshake_test

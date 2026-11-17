# Host test of the G0 GPU handshake and handoff-page parser and the GPU0 SSDT
# patcher (AppleSiliconPkg J804GpuHandoff.h), the code PrePi and
# J804GpuAcpiDxe run. Synthetic pages only, no private data. zlib's crc32 is
# the reference CRC.
# usage: make && ./gpu_handoff_test [SsdtGpu0.aml patched-out.aml]

MU ?= ../../..
INC := $(MU)/Silicon/Apple/AppleSiliconPkg/Include
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -Ishim -I$(INC)
gpu_handoff_test: gpu_handoff_test.c $(INC)/J804GpuHandoff.h
	cc $(CFLAGS) -o $@ gpu_handoff_test.c -lz
clean:
	rm -rf gpu_handoff_test gpu_handoff_test.dSYM

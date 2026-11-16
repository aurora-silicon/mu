# Host test of the J804 M1 reservation code (MemoryInitPeiLib J804_M1 block).
# usage: make && ./j804m1_reserve_test <path to a saved J804 ADT dump>
# The ADT dump is private data: pass its path, never copy or commit it.

MU ?= ../../..
LIB := $(MU)/Silicon/Apple/T8152FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c
CFLAGS := -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
  -Ishim -I$(MU)/Silicon/Apple/AppleSiliconPkg/Include
j804m1_reserve_test: j804m1_reserve_test.c gen/j804_block.c dtlib.c $(MU)/Silicon/Apple/AppleSiliconPkg/Include/J804M1.h
	cc $(CFLAGS) -o $@ j804m1_reserve_test.c dtlib.c
gen/j804_block.c: $(LIB)
	mkdir -p gen
	python3 -c 'import sys; s=open(sys.argv[1]).read(); a=s.index("#if defined(J804_M1)\n//\n// J804 M1 reservations"); b=s.index("#endif\n", a)+7; open(sys.argv[2],"w").write(s[a:b])' $(LIB) $@
	grep -q J804M1ReserveFromAdt $@

dtlib.c: $(MU)/Silicon/Apple/AppleSiliconPkg/Library/AppleDTLib/AppleDTLib.c
	printf '#include <Base.h>\n#define __asm__(x)\n#include "%s"\n' "$<" > $@
clean:
	rm -rf gen dtlib.c j804m1_reserve_test

# J813 sequential firmware read batching: design and failure model

Baseline f60538a3, frozen Mu802a4f8a, allocates a4096-byte bounce buffer and issues one synchronous READ per logical block. J813's current namespace uses4096-byte LBAs. Existing boot117/118 timer-ready to ExitBootServices intervals are29.367/29.314 seconds, but contain no attributable read-byte/time totals. A reduction in command count is an offline result; a boot-time or SSD-speed improvement requires a later controlled hardware comparison after the separate CPU119 A/B.

The first candidate is deliberately limited to8192-byte sequential READ chunks on T8142. Use the existing reserved/BootServices allocation tracker: its16384-byte alignment already rounds both4096 and8192 requests to the same16384-byte physical allocation. Admit only the first8192 bytes to the block layer. SART grants for RTKit are unchanged; no new grant is introduced for the bounce buffer. Explicitly enable batching after namespace identification only for the exact T8142 descriptor. Legacy/default callers retain one logical block per command. Use PRP1 for the first4096-byte page and a direct PRP2 for the second page only when needed. No PRP list, new queue concurrency, write batching, controller register change, cache-policy change, timeout relaxation or disk-write test is introduced. T8142 uses the existing LINEAR_NVMMU path and secure queue registration; its TCB must echo the actual SQE NLB and both PRPs.

Batch activation validates identified media, power-of-two block size no larger than a page, bounded/aligned8192-byte DMA capacity, address arithmetic, and an integral number of blocks. Read validation remains before submission. A synchronous chunk is copied to the arbitrary-alignment caller buffer only after its completion succeeds. Earlier successful chunks may be visible on an error, as before; the failing chunk and all following caller bytes remain untouched. No claim is made about partial media progress inside a failed multi-LBA command. Failed or timed-out commands are not retried by this change. Write/flush paths and preservation/reset policy remain unchanged.

Pre-code E2E failure cases:

1. Baseline real block/controller path submits five READs for five4KiB blocks; candidate must submit three with byte-identical data and canaries.
2. Single block, two blocks, odd tail, arbitrary caller alignment, and reads crossing low32-bit LBA boundaries produce exact NLB/LBA/byte data.
3.512-byte media uses at most16blocks per8192-byte command; default legacy mode remains one block regardless of buffer capacity.
4. PRP2 is zero for≤4096bytes and exactly the owned second physical page for larger chunks. No third page or fabricated pointer/address equivalence is admitted.
5. TCB opcode/direction/tag/NLB/PRP1/PRP2 agree with SQE before the real controller doorbell; write/read barrier ordering remains observable.
6. Caller range overflow, undersized output, NULL output, uninitialized namespace, invalid sector size, insufficient bounce capacity, unaligned/overflowing DMA address, invalid batch size and activation before identify all refuse without a READ. Re-identification clears the old batching admission until explicitly admitted again.
7. Zero-block reads preserve existing success behavior without DMA.
8. Completion-status error, wrong tag, TCB invalidation error and missing completion stop without copying the failed chunk or issuing later commands; completed prefix remains exact. Timeouts retain original bounded poll count.
9. Repeated reads wrap the real completion ring/phase without losing data. Successful startup/identify/read/shutdown disable and queue-delete path remains intact.
10. Native firmware build must include the intended T8142-only allocation/activation and preserve other PCDs/ACPI/boot guards. Allocation failure follows existing fail path; reserved lifetime and release tracking remain inherited.

The host E2E compiles the actual Mu block, NVMe and controller sources, uses synthetic MMIO/DMA/media only at the hardware boundary, records every command, checks byte artifacts and errors, and runs with address/undefined sanitizers. Preserve the baseline failure and targeted mutations that must fail the same E2E. Source/compiler/artifact hashes and exact commands make it repeatable. No hardware execution, firmware deployment or measured speed gain is claimed by this work.

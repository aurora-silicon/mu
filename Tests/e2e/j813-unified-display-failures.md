# J813 unified GPU/display ownership (pre-code)

Scope: isolated integration based on G17 target lifecycle a112176 and AppleDisplay
3d45fef (0.8.7 backend V2). Frozen render, shared, standalone DOD and firmware
candidates remain unchanged. No hardware/registration/deployment is authorized
for this implementation. FULL publication remains closed until all producer
contracts below have implementations and evidence.

## Owning contracts and exact firmware prerequisite

Current Mu 45087b59 publishes GPU0/AURA8170 with three fixed T8142 GPU MMIO
windows and six validated, non-system-RAM iBoot regions. DCP0/AURA8180 separately
owns the seven fixed J813 windows. GPU.asl and DISP.asl have no interrupts and
no framebuffer descriptor. DXGK owns POST framebuffer acquisition. The driver
must call GetDeviceInformation once and pass a normalized resource snapshot to
its embedded consumers; querying it twice already failed on the owning Windows
build.

The opt-in unified firmware selection must publish one new GPU0/AURA8171 SSDT
with the exact nine GPU descriptors followed by all seven DCP descriptors. It
must suppress BOTH legacy SSDTs whenever unified selection is requested, even
if admission of the new SSDT fails. It requires exact chip8142/board24, GPU
region validation, dcp/disp0 ADT presence. Default remains the legacy selection.
No duplicate MMIO ownership, new IRQ, fabricated framebuffer address, or implied
firmware quiescence. Failure leaves BasicDisplay's POST path intact. New HID
prevents existing render-only INF from binding accidentally.

Current G17 admission in AppleAgxWddmG17.c requires exactly9 ranges. Old FULL
agxw_resources.c instead validates T6020 windows and a six-window DCP backend.
Neither may accept the new resource shape. A separate exact J813 unified parser
must normalize16 memory ranges, refuse all IRQ/port/unknown grants and check
cache flags, overflow, duplicate/overlapping ranges and OS-RAM exclusion for the
six reserved GPU regions before any mapping or arm consumption. Standalone
legacy parsing must retain its existing shape and behavior.

## Failure cases before implementation

1. Wrong mode/HID, legacy9 or display7 alone, reordered/duplicate/foreign fixed
   window, missing/extra descriptor, forbidden interrupt, malformed count or
   cache flags: refuse before MMIO and before consuming any one-shot arm.
2. Reserved GPU range wraps, is misaligned, too small, overlaps another grant,
   lies outside42-bit DRAM or intersects system RAM: refuse. DATA exact sizes
   and existing handoff/TTBAT/table minimums remain owning constraints.
3. Unified firmware selected but GPU or DCP validation/table install fails:
   never publish a second owner as fallback; preserve POST display.
4. Embedded J813 owner must consume only its validated7-window snapshot; it
   must not call GetDeviceInformation again or fall into J414/T6020 code.
5. Embedded Start fails after GPU/DCP firmware begins DMA: retain all backing
   and mappings, close admission and require reboot. Do not roll back by freeing
   firmware-visible memory. No restart implied by Stop or driver unload.
6. PRIMARY/shared resource input is not automatically a scanout lease. Current
   G17 restrict_aperture refuses PRIMARY; .24 only enables representable direct
   non-primary sharing. Keep that guard until standard allocation/open/paging
   ownership routes actual OS backing to both GPU and DCP.
7. A primary lease must retain allocation+open+backing generation, verify exact
   native2560x1664/10240 geometry and1040 16KiB pages, stabilize paging/cache and
   forbid CPU/GPU writers while DCP owns it. No private-PFN escape allocation.
8. G17 target/all-queue retirement proves internal job storage reusable, not
   client allocation release. Need per-allocation writer references/fences and
   paging ownership. D590 proves old DCP scanout retirement only; both proofs
   are required before reuse/free/unmap and after device/process loss.
9. Existing legacy display worker can force a never-written primary after a
   pass budget. This is not usable G17 ownership evidence; unified code cannot
   inherit that path as a substitute for a real completed writer.
10. Physical scanout may exceed16MiB. Every1040-page panel surface uses a
    separate32MiB L2 window; at most6 external windows remain after DOD's2.
    Partial Map/Unmap or invalidation failure retains its token/backing.
11. H17G_D590_POLLED is exact present/old-surface retirement, not VSync. No
    timestamp, frame period, IRQ or periodic-VSync capability may be fabricated.
    The old FULL timer/caps negotiation must remain distinct and closed.
12. Accidental capability exposure: no Display class/default INF/UMD opt-in,
    NumberOfSources/Children, FULL publication or primary guard relaxation from
    merely compiling the new backend or recognizing the16-resource shape.
13. Backend and imported dependencies must be source-bound to0.8.7V2; no
    divergent copies or silently changed standalone behavior. Compiled ARM64
    object/link and actual resource admission workflow must be repeatable.

## Evidence boundary and next functional work

Use existing G17 boot/admission and J813 session/DART workflows; add only the
actual resource/owner routing boundary and its failures. Produce source/build
and input/output receipts. Firmware-model D590 and GPU retirement remain models.
A hardware-testable resource/owner diagnostic is separate from accelerated
DWM. Complete production enablement additionally requires real standard primary
producer ownership, cache/paging transfer and all process/device-loss cleanup,
then a truthful DWM cadence contract. No enum switch can close these gaps.

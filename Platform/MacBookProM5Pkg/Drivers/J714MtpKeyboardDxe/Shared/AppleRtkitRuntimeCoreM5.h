/* SPDX-License-Identifier: MIT */
/*
 * AppleMtpHidM5 fork of AppleRtkitRuntimeCore.h.  The only
 * change is the optional map_firmware_buffer op: Linux rtkit-helper
 * (drivers/soc/apple/rtkit-helper.c) answers a BUFFER_REQUEST that already
 * carries an address by mapping the coprocessor SRAM window and sending NO
 * reply (drivers/soc/apple/rtkit.c apple_rtkit_common_rx_get_buffer,
 * is_mapped path).  The original core rejects such requests.
 */
#ifndef NTASI_APPLE_RTKIT_RUNTIME_CORE_M5_H
#define NTASI_APPLE_RTKIT_RUNTIME_CORE_M5_H

#include "AppleAscCore.h"
#include "AppleRtkitCore.h"

#include <stddef.h>

#define NTASI_RTKIT_SYSTEM_ENDPOINT_LIMIT 0x20u

enum ntasi_rtkit_power_state {
    NTASI_RTKIT_POWER_OFF = 0x00,
    NTASI_RTKIT_POWER_SLEEP = 0x01,
    NTASI_RTKIT_POWER_QUIESCED = 0x10,
    NTASI_RTKIT_POWER_ON = 0x20,
    NTASI_RTKIT_POWER_INIT = 0x220,
};

/*
 * Position inside ntasi_rtkit_runtime_boot().
 *
 * WHY THIS EXISTS.  ntasi_rtkit_runtime_boot() returns
 * NTASI_RTKIT_RUNTIME_ERR_TIMEOUT (-22) from THREE different places -- the
 * HELLO wait, the endpoint-map wait, and the IOP power-state wait -- and all
 * three leave iop_power and ap_power at the POWER_OFF the function itself
 * assigned on entry.  A caller that only sees -22 therefore cannot tell "the
 * coprocessor never said anything at all" (dead IOP) from "it completed the
 * handshake and then refused to power on" (live IOP, different fault), and
 * those two have nothing in common.  On J414s/T6020 that ambiguity was the
 * whole diagnosis.
 *
 * APPENDED-NEVER-INSERTED: the numeric values are published in AppleNvme's
 * REG_BINARY start record and decoded at fixed offsets by
 * ABG diagnostics.
 */
enum ntasi_rtkit_boot_step {
    NTASI_RTKIT_BOOT_STEP_IDLE = 0,
    NTASI_RTKIT_BOOT_STEP_CPU_START = 1,
    NTASI_RTKIT_BOOT_STEP_SEND_INIT = 2,
    NTASI_RTKIT_BOOT_STEP_WAIT_HELLO = 3,
    NTASI_RTKIT_BOOT_STEP_SEND_HELLO_ACK = 4,
    NTASI_RTKIT_BOOT_STEP_WAIT_EPMAP = 5,
    NTASI_RTKIT_BOOT_STEP_REPLY_EPMAP = 6,
    NTASI_RTKIT_BOOT_STEP_START_ENDPOINTS = 7,
    NTASI_RTKIT_BOOT_STEP_WAIT_IOP_POWER = 8,
    NTASI_RTKIT_BOOT_STEP_SEND_AP_POWER = 9,
    NTASI_RTKIT_BOOT_STEP_DONE = 10,
    /* Reached only from ntasi_rtkit_runtime_sleep(). */
    NTASI_RTKIT_BOOT_STEP_SLEEP = 11,
    /*
     * The AP power-state ACK wait, which Linux performs and m1n1's untimed
     * rtkit_boot() skips.  See the wait in ntasi_rtkit_runtime_boot(): it is
     * BOUNDED AND NON-FATAL, so this step is never the reported step of a
     * failed boot -- it exists so `ap_power` in telemetry is a measurement
     * rather than the POWER_OFF the function assigned on entry.
     */
    NTASI_RTKIT_BOOT_STEP_WAIT_AP_POWER = 12,
    /*
     * The stale-mailbox drain that opens a COLD boot, standing in for the
     * apple_mbox_stop() / apple_mbox_start() pair inside apple_rtkit_reinit()
     * (drivers/soc/apple/rtkit.c:739-768).  Bounded and never fatal.
     */
    NTASI_RTKIT_BOOT_STEP_DRAIN = 13,
};

/*
 * WHICH OF THE REFERENCES' BRING-UP SHAPES ntasi_rtkit_runtime_boot() PERFORMS.
 *
 * The three differ in exactly two decisions -- whether CPU_CONTROL.RUN is
 * written, and whether SET_IOP_PWR_STATE(INIT) is transmitted before the
 * coprocessor has said anything -- and they are NOT interchangeable:
 *
 *   M1N1  src/rtkit.c:496-509.  asc_cpu_start() unconditionally, then
 *         SET_IOP_PWR_STATE(INIT) unconditionally ("can be sent
 *         unconditionally to wake up a possibly sleeping IOP").  One shape for
 *         both cases.  This is what this runtime has always done, so it stays
 *         value 0: a caller that never sets the field keeps its behaviour.
 *
 *   COLD  Linux drivers/nvme/host/apple.c:1103-1118 followed by
 *         apple_rtkit_boot() (drivers/soc/apple/rtkit.c:817-840).  The core is
 *         reset, then released with a plain writel(RUN), and then NOTHING is
 *         transmitted: the coprocessor opens the conversation with HELLO and
 *         the AP only ever replies.  Anything the AP puts in the A2I FIFO
 *         before HELLO is, on this path, out of protocol.
 *
 *   WAKE  Linux apple_rtkit_wake() (rtkit.c:920-942).  CPU_CONTROL IS NEVER
 *         TOUCHED -- the coprocessor is already executing -- and
 *         SET_IOP_PWR_STATE(INIT) is what wakes it.  Writing the run bit here
 *         is at best redundant and at worst perturbs a live core.
 */
enum ntasi_rtkit_boot_mode {
    NTASI_RTKIT_BOOT_MODE_M1N1 = 0,
    NTASI_RTKIT_BOOT_MODE_COLD = 1,
    NTASI_RTKIT_BOOT_MODE_WAKE = 2,
};

enum ntasi_rtkit_runtime_result {
    NTASI_RTKIT_RUNTIME_OK = 0,
    NTASI_RTKIT_RUNTIME_NO_MESSAGE = 1,
    NTASI_RTKIT_RUNTIME_APP_MESSAGE = 2,
    NTASI_RTKIT_RUNTIME_ERR_ARGUMENT = -20,
    NTASI_RTKIT_RUNTIME_ERR_TRANSPORT = -21,
    NTASI_RTKIT_RUNTIME_ERR_TIMEOUT = -22,
    NTASI_RTKIT_RUNTIME_ERR_PROTOCOL = -23,
    NTASI_RTKIT_RUNTIME_ERR_VERSION = -24,
    NTASI_RTKIT_RUNTIME_ERR_BUFFER = -25,
    NTASI_RTKIT_RUNTIME_ERR_CRASHED = -26,
    /*
     * The caller's ready_to_release() gate refused. Distinct from TIMEOUT
     * because nothing was ever sent: the coprocessor was never released, so
     * no message could have been exchanged and no boot step past CPU_START
     * can be meaningful.
     */
    NTASI_RTKIT_RUNTIME_ERR_NOT_READY = -27,
    NTASI_RTKIT_RUNTIME_ERR_STATE = -28,
};

struct ntasi_rtkit_shared_buffer {
    void *cpu_address;
    uint64_t device_address;
    size_t size;
};

struct ntasi_rtkit_runtime_ops {
    int (*allocate_shared)(void *opaque, uint8_t endpoint, size_t size,
                           struct ntasi_rtkit_shared_buffer *buffer);
    void (*release_shared)(void *opaque, uint8_t endpoint,
                           struct ntasi_rtkit_shared_buffer *buffer);
    void (*crashed)(void *opaque,
                    const struct ntasi_rtkit_shared_buffer *crashlog);
    /*
     * OPTIONAL last-instant gate, called immediately before the run bit is
     * written and after every other preparation.  Return 0 to release,
     * non-zero to abort with NTASI_RTKIT_RUNTIME_ERR_NOT_READY.
     *
     * WHY IT HAS TO BE HERE AND NOWHERE ELSE.  Measured on J414s 2026-08-01:
     * the ps_ans2 power domain read PS_ACTUAL == PS_TARGET == ACTIVE at the
     * end of the reset deassert, read PS_ACTUAL 0x3 at the instant the run bit
     * was released, and read ACTIVE again by the time the failure telemetry
     * swept all four domains.  The domain reaches ACTIVE on its own timeline;
     * the release simply happened inside the transition.  A readiness check
     * performed anywhere earlier answers a question about a different moment,
     * which is exactly what the earlier settle was doing.
     *
     * Neither reference needs this: on their paths the domain is already
     * settled long before they release.  Ours is coming out of a reset this
     * driver just performed, so the transition is still in flight.
     */
    int (*ready_to_release)(void *opaque);
    /*
     * OPTIONAL post-release observation, called immediately after the
     * CPU_CONTROL run-bit write and its readback, and BEFORE the CPU_STATUS
     * watch.  It cannot fail the boot -- it exists purely so a caller can
     * sample device state at the one moment that is otherwise unreachable:
     * after the core has been let go but before anything else has run.
     *
     * WHY THIS MOMENT.  ready_to_release() proves the power domain was settled
     * BEFORE the write; only a sample taken AFTER it can say whether the write
     * itself perturbed the domain.  Those are different questions and a single
     * sample cannot answer both.
     *
     * NULL for callers that do not need it.  Mu initialises this ops struct
     * with four positional members, so the field stays NULL there.
     */
    void (*after_release)(void *opaque);
    /*
     * OPTIONAL (M5 fork).  A BUFFER_REQUEST whose payload already names a
     * device address is firmware-owned memory (the MTP SRAM segments on
     * J714s).  Fill cpu_address/device_address/size for that window and
     * return 0; the runtime then sends no reply, as Linux does.  NULL keeps
     * the original fail-closed behaviour.
     */
    int (*map_firmware_buffer)(void *opaque, uint8_t endpoint,
                               uint64_t device_address, size_t size,
                               struct ntasi_rtkit_shared_buffer *buffer);
};

struct ntasi_rtkit_runtime {
    struct ntasi_asc_transport *asc;
    struct ntasi_rtkit_runtime_ops ops;
    void *opaque;
    uint32_t poll_limit;
    enum ntasi_rtkit_power_state iop_power;
    enum ntasi_rtkit_power_state ap_power;
    uint32_t system_endpoints;
    /*
     * Bitmap of advertised-but-optional endpoints whose start handshake
     * failed.  Non-fatal by design (see the `optional` list in
     * ntasi_rtkit_runtime_boot), but recorded rather than discarded so a
     * skipped endpoint is observable instead of invisible.
     */
    uint32_t optional_endpoints_failed;
    struct ntasi_rtkit_shared_buffer crashlog;
    struct ntasi_rtkit_shared_buffer syslog;
    struct ntasi_rtkit_shared_buffer ioreport;
    struct ntasi_rtkit_shared_buffer oslog;
    bool booted;
    bool crashed;

    /*
     * Boot-progress trace.  Diagnostic only -- nothing in the protocol reads
     * these back -- but they are what turns a bare -22 into a named step.
     * Reset by ntasi_rtkit_runtime_init() and by every
     * ntasi_rtkit_runtime_boot() entry, so they always describe the CURRENT
     * attempt rather than an accumulation across retries.
     */
    uint32_t boot_step;           /* enum ntasi_rtkit_boot_step */
    /* Polls consumed by the bounded wait that was running last.  Equal to
     * poll_limit exactly when that wait is the one that timed out. */
    uint32_t poll_attempts;
    uint32_t epmap_rounds;
    uint32_t messages_received;
    uint32_t messages_sent;
    /* HELLO's advertised range, min in [15:0] and max in [31:16], as parsed.
     * Zero means no HELLO was ever seen. */
    uint32_t hello_version;
    uint64_t last_rx_payload;
    uint64_t last_tx_payload;
    uint32_t last_rx_endpoint;
    uint32_t last_tx_endpoint;
    /*
     * Did the coprocessor acknowledge SET_AP_PWR_STATE(ON)?  Linux waits for
     * this and verifies it (drivers/soc/apple/rtkit.c:771-792, reached from
     * apple_rtkit_boot() at rtkit.c:838); m1n1's untimed rtkit_boot() does not
     * (src/rtkit.c:823-826, "Preserve the historical asynchronous return").
     * We wait like Linux but, like m1n1, do not fail the boot on it -- so a
     * missing ACK is reported instead of either being invisible or costing a
     * controller that would otherwise have worked.
     */
    bool ap_power_acked;
    uint32_t ap_ack_poll_attempts;
    /*
     * Management messages whose type is none of HELLO, EPMAP, or either
     * power-state ACK.
     * Linux warns and continues (rtkit.c:253-258) and so do we: failing the
     * boot on an unrecognised management type would let one unmodelled
     * firmware message abort a handshake that is otherwise complete.
     */
    uint32_t unexpected_mgmt_messages;
    uint32_t last_unexpected_mgmt_type;

    /*
     * HOW THE CORE WAS RELEASED, and what it did next.
     *
     * Set by the caller BEFORE ntasi_rtkit_runtime_boot(): false keeps m1n1's
     * read-modify-write (src/asc.c:70), true uses Linux's plain
     * writel(RUN) (drivers/nvme/host/apple.c:1115).  Callers that operate on
     * a coprocessor in a known-good state should leave it false; a cold path
     * recovering an unknown one should set it, because an RMW preserves any
     * inherited bit that holds the core back and a plain write does not.
     */
    bool cpu_start_exclusive;
    /*
     * enum ntasi_rtkit_boot_mode, set by the caller BEFORE
     * ntasi_rtkit_runtime_boot().  Zero (M1N1) is the historical behaviour, so
     * callers that never assign it are unaffected.
     *
     * WHY THIS IS NOT A DETAIL.  A caller on the COLD path that transmits
     * SET_IOP_PWR_STATE(INIT) is speaking before the coprocessor has said
     * HELLO, which Linux never does on that path; a caller on the WAKE path
     * that writes CPU_CONTROL is touching the run bit of a live core, which
     * Linux never does either.  Both were previously unrepresentable: one
     * function did both things on every path.
     */
    uint32_t boot_mode;
    /*
     * THE STALE-MAILBOX DRAIN, and what it found.
     *
     * apple_rtkit_reinit() stops the mailbox, flushes the workqueue and starts
     * it again (rtkit.c:741-768) so the first message the new handshake sees
     * cannot be one the previous stage left behind.  A polled transport has no
     * workqueue to flush, so the equivalent is to read I2A dry while the core
     * is still held in reset.  stale_messages_drained is ALSO the evidence for
     * "the coprocessor was already running and talking": a cold path that
     * drains a non-zero count was handed a live IOP.
     */
    uint32_t stale_messages_drained;
    uint32_t stale_first_endpoint;
    uint64_t stale_first_payload;
    /*
     * The release, measured rather than assumed.  A core that accepts the run
     * bit and never runs is indistinguishable from one whose CPU_CONTROL write
     * never stuck unless the readback is published next to the value written.
     * cpu_status_first_change / cpu_status_change_polls additionally separate
     * "never moved at all" from "moved and then stopped", which is the last
     * ambiguity left in a silent coprocessor.
     */
    uint32_t cpu_control_before_start;
    uint32_t cpu_control_written;
    uint32_t cpu_control_readback;
    uint32_t cpu_status_before_start;
    uint32_t cpu_status_after_start;
    uint32_t cpu_status_first_change;
    uint32_t cpu_status_change_polls;
    /*
     * THE FULL TRAJECTORY, not just the first step.
     *
     * cpu_status_first_change stops at the first transition, and on J414s that
     * is always the STOPPED bit clearing at the release -- so the watch
     * returned immediately and never observed what the core did next.  The
     * failure-time sample then showed IDLE had ALSO cleared, with nothing in
     * between to say when or why.  A core that moves twice is executing
     * something; a core that moves once and stops is not, and the difference
     * was invisible.
     *
     * cpu_status_trace[i] is the i'th DISTINCT value observed after the
     * release and cpu_status_trace_polls[i] the poll index at which it first
     * appeared.  cpu_status_transitions counts every transition even after the
     * slots are full, so "more happened than we recorded" is still visible.
     */
    uint32_t cpu_status_trace[4];
    uint32_t cpu_status_trace_polls[4];
    uint32_t cpu_status_transitions;
};

/* Bounded watch on CPU_STATUS after the run bit is released, in service-op
 * iterations. The caller's service op is what sets the wall-clock cost; with
 * AppleNvme's 1 us stall this is 1 ms, far longer than any core needs to
 * fetch its first instruction. */
#define NTASI_RTKIT_CPU_STATUS_WATCH_POLLS 1000u

/* Distinct post-release CPU_STATUS values retained. Four covers the observed
 * STOPPED-clear then IDLE-clear pair with headroom, and bounds the struct. */
#define NTASI_RTKIT_CPU_STATUS_TRACE_SLOTS 4u

/* Messages the cold-path drain will discard before giving up. The I2A FIFO is
 * 16 entries deep on every ASC this project has seen (m1n1
 * proxyclient/m1n1/hw/asc.py, FIFOCNT is a 4-bit field), so this empties a
 * completely full mailbox and no more. */
#define NTASI_RTKIT_STALE_DRAIN_LIMIT 16u

int ntasi_rtkit_runtime_init(struct ntasi_rtkit_runtime *runtime,
                             struct ntasi_asc_transport *asc,
                             const struct ntasi_rtkit_runtime_ops *ops,
                             void *opaque,
                             uint32_t poll_limit);

int ntasi_rtkit_runtime_boot(struct ntasi_rtkit_runtime *runtime);

/* Starts or sends to an application endpoint after a successful boot. */
int ntasi_rtkit_runtime_start_endpoint(
    struct ntasi_rtkit_runtime *runtime, uint8_t endpoint);
int ntasi_rtkit_runtime_send_application(
    struct ntasi_rtkit_runtime *runtime, uint8_t endpoint, uint64_t payload);

/* Handles at most one inbound message. Application messages are returned. */
int ntasi_rtkit_runtime_service(struct ntasi_rtkit_runtime *runtime,
                                struct ntasi_asc_message *application_message);

int ntasi_rtkit_runtime_sleep(struct ntasi_rtkit_runtime *runtime);
int ntasi_rtkit_runtime_quiesce(struct ntasi_rtkit_runtime *runtime);
void ntasi_rtkit_runtime_release_buffers(struct ntasi_rtkit_runtime *runtime);

#endif

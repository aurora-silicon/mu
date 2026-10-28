/* SPDX-License-Identifier: MIT */
#ifndef NTASI_APPLE_ASC_CORE_H
#define NTASI_APPLE_ASC_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* CPU-control offsets are relative to the coprocessor register base. */
#define NTASI_ASC_CPU_CONTROL 0x0044u
#define NTASI_ASC_CPU_CONTROL_START (1u << 4)

/*
 * CPU STATUS -- the only register that says whether the coprocessor is
 * EXECUTING, as opposed to whether software asked it to.
 *
 * CPU_CONTROL.START is a request: this driver sets it in
 * ntasi_rtkit_runtime_boot() and it reads back 0x10 whether or not the core
 * ever fetched an instruction.  On J414s/T6020 that ambiguity was the entire
 * diagnosis -- a stage-12 RTKit timeout with CPU_CONTROL == 0x10 was read as
 * "the coprocessor is running but the mailbox is broken" when the mailbox
 * FIFO counters said the opposite (A2I FIFOCNT=1 unread, I2A never written:
 * the IOP had not executed a single instruction).
 *
 * Offsets and field names from m1n1 proxyclient/m1n1/hw/asc.py:14-22
 * (R_CPU_CONTROL.RUN = 4, R_CPU_STATUS at 0x0048 with RUNNING/STOPPED/IDLE).
 * Read-only; nothing in the boot path writes it.
 */
#define NTASI_ASC_CPU_STATUS 0x0048u
#define NTASI_ASC_CPU_STATUS_RUNNING (1u << 0)
#define NTASI_ASC_CPU_STATUS_STOPPED (1u << 1)
#define NTASI_ASC_CPU_STATUS_IDLE    (1u << 5)

/* Mailbox offsets are relative to the separately discovered mailbox base. */
#define NTASI_ASC_MBOX_A2I_CONTROL 0x110u
#define NTASI_ASC_MBOX_I2A_CONTROL 0x114u
#define NTASI_ASC_MBOX_A2I_SEND0   0x800u
#define NTASI_ASC_MBOX_A2I_SEND1   0x808u
#define NTASI_ASC_MBOX_I2A_RECV0   0x830u
#define NTASI_ASC_MBOX_I2A_RECV1   0x838u
#define NTASI_T8015_MBOX_A2I_CONTROL 0x108u
#define NTASI_T8015_MBOX_I2A_CONTROL 0x10cu

#define NTASI_ASC_MBOX_CONTROL_FULL  (1u << 16)
#define NTASI_ASC_MBOX_CONTROL_EMPTY (1u << 17)

enum ntasi_asc_result {
    NTASI_ASC_OK = 0,
    NTASI_ASC_NO_MESSAGE = 1,
    NTASI_ASC_ERR_ARGUMENT = -1,
    NTASI_ASC_ERR_SEND_TIMEOUT = -2,
    NTASI_ASC_ERR_STOP_TIMEOUT = -3,
};

struct ntasi_asc_message {
    uint64_t payload;
    uint32_t endpoint;
};

struct ntasi_asc_hw {
    uint32_t a2i_control;
    uint32_t i2a_control;
    uint32_t a2i_send0;
    uint32_t a2i_send1;
    uint32_t i2a_recv0;
    uint32_t i2a_recv1;
    bool postoffice;
};

extern const struct ntasi_asc_hw ntasi_asc_hw_v4;
extern const struct ntasi_asc_hw ntasi_asc_hw_v8;
extern const struct ntasi_asc_hw ntasi_asc_hw_t8015;

struct ntasi_asc_ops {
    uint32_t (*cpu_read32)(void *opaque, uint32_t offset);
    void (*cpu_write32)(void *opaque, uint32_t offset, uint32_t value);
    uint32_t (*mailbox_read32)(void *opaque, uint32_t offset);
    uint64_t (*mailbox_read64)(void *opaque, uint32_t offset);
    void (*mailbox_write64)(void *opaque, uint32_t offset, uint64_t value);
    void (*dma_read_barrier)(void *opaque);
    void (*dma_write_barrier)(void *opaque);
    void (*service)(void *opaque);
    void (*mailbox_read_pair)(void *opaque, uint32_t offset, uint64_t *lo, uint64_t *hi);
    void (*mailbox_write_pair)(void *opaque, uint32_t offset, uint64_t lo, uint64_t hi);
};

struct ntasi_asc_transport {
    struct ntasi_asc_ops ops;
    const struct ntasi_asc_hw *hw;
    void *opaque;
    uint32_t poll_limit;
};

int ntasi_asc_init(struct ntasi_asc_transport *transport,
                   const struct ntasi_asc_ops *ops,
                   void *opaque,
                   uint32_t poll_limit);

int ntasi_asc_init_variant(struct ntasi_asc_transport *transport,
                           const struct ntasi_asc_ops *ops,
                           const struct ntasi_asc_hw *hw,
                           void *opaque,
                           uint32_t poll_limit);

void ntasi_asc_cpu_start(struct ntasi_asc_transport *transport);

/*
 * Release the core by WRITING the START bit outright, rather than OR-ing it
 * into whatever CPU_CONTROL already holds.
 *
 * The two references disagree here and the difference only matters on a core
 * that will not start:
 *
 *   m1n1 src/asc.c:70 asc_cpu_start() is set32(), a read-modify-write.  It
 *   operates on a coprocessor iBoot left in a known-good state, so preserving
 *   the other bits is free.
 *
 *   Linux drivers/nvme/host/apple.c:1115-1116 is
 *   writel(APPLE_ANS_COPROC_CPU_CONTROL_RUN, mmio_coproc + CPU_CONTROL) --
 *   a PLAIN WRITE of exactly BIT(4), which clears every other bit in the
 *   register.  Its teardown paths (apple.c:1093, 1711, 1726, 1747) are
 *   likewise plain writel(0).
 *
 * On J414s the core accepts the run bit (CPU_STATUS.STOPPED clears) and never
 * reaches RUNNING, through both a plain resume and a full ps_ans2 reset.  If
 * CPU_CONTROL carries any inherited bit that holds the core back, an RMW
 * preserves it forever and Linux's plain write would not -- so the cold path
 * now matches Linux exactly, and the value written plus its readback are
 * published so the next boot can say whether that was the difference.
 */
void ntasi_asc_cpu_start_exclusive(struct ntasi_asc_transport *transport);

void ntasi_asc_cpu_stop(struct ntasi_asc_transport *transport);
bool ntasi_asc_cpu_running(struct ntasi_asc_transport *transport);

/*
 * Raw CPU_STATUS word.  Deliberately NOT consulted anywhere in the boot path:
 * it is a diagnostic observation, and a boot that gated on an
 * unverified-on-hardware status encoding would fail closed on silicon whose
 * bits differ.  Callers publish it; they do not branch on it.
 */
uint32_t ntasi_asc_cpu_status(struct ntasi_asc_transport *transport);

/*
 * Stop a coprocessor inherited in the running state, service the transport for
 * at least settle_polls, and verify that START remains clear.  The operation
 * is bounded by transport->poll_limit and returns STOP_TIMEOUT if hardware
 * does not accept the stop.  A caller whose service callback delays one
 * microsecond can request a one-millisecond reset-settle interval with 1000
 * polls.  An already-stopped coprocessor needs no settling and returns OK.
 */
int ntasi_asc_cpu_stop_bounded(struct ntasi_asc_transport *transport,
                               uint32_t settle_polls);

bool ntasi_asc_can_send(struct ntasi_asc_transport *transport);
bool ntasi_asc_can_receive(struct ntasi_asc_transport *transport);

/* Polls for A2I space, then publishes payload followed by endpoint. */
int ntasi_asc_send(struct ntasi_asc_transport *transport,
                   const struct ntasi_asc_message *message);

/* Non-blocking receive. Returns NTASI_ASC_NO_MESSAGE when I2A is empty. */
int ntasi_asc_receive(struct ntasi_asc_transport *transport,
                      struct ntasi_asc_message *message);

#endif

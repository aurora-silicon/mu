/* SPDX-License-Identifier: MIT */
#ifndef NTASI_APPLE_NVME_CONTROLLER_CORE_H
#define NTASI_APPLE_NVME_CONTROLLER_CORE_H

#include "AppleNvmeCore.h"

enum ntasi_ans_controller_result {
    NTASI_ANS_CONTROLLER_OK = 0,
    NTASI_ANS_CONTROLLER_ERR_ARGUMENT = -1,
    NTASI_ANS_CONTROLLER_ERR_ALIGNMENT = -2,
    NTASI_ANS_CONTROLLER_ERR_BOOT_TIMEOUT = -3,
    NTASI_ANS_CONTROLLER_ERR_DISABLE_TIMEOUT = -4,
    NTASI_ANS_CONTROLLER_ERR_ENABLE_TIMEOUT = -5,
    NTASI_ANS_CONTROLLER_ERR_COMMAND_TIMEOUT = -6,
    NTASI_ANS_CONTROLLER_ERR_COMPLETION_TAG = -7,
    NTASI_ANS_CONTROLLER_ERR_COMPLETION_STATUS = -8,
    NTASI_ANS_CONTROLLER_ERR_TCB_INVALIDATE = -9,
    NTASI_ANS_CONTROLLER_ERR_SHUTDOWN_TIMEOUT = -10,
    NTASI_ANS_CONTROLLER_ERR_NOT_STARTED = -11,
    NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED = -12,
    NTASI_ANS_CONTROLLER_ERR_FATAL_STATE = -13,
};

enum ntasi_ans_controller_operation {
    NTASI_ANS_OPERATION_NONE = 0,
    NTASI_ANS_OPERATION_MMIO_READ,
    NTASI_ANS_OPERATION_MMIO_WRITE,
    NTASI_ANS_OPERATION_MMIO_POLL,
    NTASI_ANS_OPERATION_COMMAND_DOORBELL,
    NTASI_ANS_OPERATION_CQE_WAIT,
    NTASI_ANS_OPERATION_CQE_VALIDATE,
    NTASI_ANS_OPERATION_TCB_INVALIDATE,
};

enum ntasi_ans_controller_substage {
    NTASI_ANS_SUBSTAGE_NONE = 0,
    NTASI_ANS_SUBSTAGE_BOOT_STATUS_WAIT,
    NTASI_ANS_SUBSTAGE_LINEAR_CONFIG,
    NTASI_ANS_SUBSTAGE_DISABLE_REQUEST,
    NTASI_ANS_SUBSTAGE_DISABLE_WAIT,
    NTASI_ANS_SUBSTAGE_ADMIN_QUEUE_PROGRAM,
    NTASI_ANS_SUBSTAGE_NVMMU_TCB_PROGRAM,
    NTASI_ANS_SUBSTAGE_CAP_INITIAL_READ,
    NTASI_ANS_SUBSTAGE_CC_CONFIG_WRITE,
    NTASI_ANS_SUBSTAGE_CAP_REREAD,
    NTASI_ANS_SUBSTAGE_CC_ENABLE_WRITE,
    NTASI_ANS_SUBSTAGE_CC_READY_WAIT,
    NTASI_ANS_SUBSTAGE_CREATE_IOCQ_WAIT,
    NTASI_ANS_SUBSTAGE_CREATE_IOSQ_WAIT,
    NTASI_ANS_SUBSTAGE_IDENTIFY_WAIT,
    NTASI_ANS_SUBSTAGE_ADMIN_COMMAND_WAIT,
    NTASI_ANS_SUBSTAGE_IO_COMMAND_WAIT,
    /* Append only: offline decoders name these substages by ordinal. */
    NTASI_ANS_SUBSTAGE_SECURE_IO_QUEUE_PROGRAM,
};

typedef bool (*ntasi_ans_synchronized_routine)(void *context);

struct ntasi_ans_queue_memory;

struct ntasi_ans_controller_ops {
    uint32_t (*read32)(void *opaque, uint32_t offset);
    void (*write32)(void *opaque, uint32_t offset, uint32_t value);
    void (*dma_read_barrier)(void *opaque);
    void (*dma_write_barrier)(void *opaque);
    /* Services ASC/RTKit traffic while the controller is polled. */
    void (*service)(void *opaque);
    /*
     * Optionally executes a short queue-state callback synchronized with the
     * platform ISR. Windows supplies StorPortSynchronizeAccess here; firmware
     * and host tests may leave it NULL for direct execution.
     */
    bool (*synchronize)(void *opaque,
                        ntasi_ans_synchronized_routine routine,
                        void *context);
    /*
     * Host-only deterministic fault hook. Production leaves this NULL.
     * A positive return value is retained as the injected fault identifier.
     */
    int (*fault)(void *opaque, uint32_t substage, uint32_t operation,
                 uint32_t offset);
    /* Optional normal resident-SPTM backend. Legacy platforms leave NULL. */
    int (*configure_queues)(void *opaque,
                            const struct ntasi_ans_queue_memory *admin,
                            const struct ntasi_ans_queue_memory *io,
                            uint32_t admin_depth, uint32_t io_depth);
    int (*map_command)(void *opaque, bool admin, uint8_t tag,
                       uint64_t tcb_dma, const struct ntasi_ans_sqe *command);
    int (*unmap_command)(void *opaque, bool admin, uint16_t tag);
};

struct ntasi_ans_queue_memory {
    void *commands;
    struct ntasi_ans_cqe *completions;
    struct ntasi_ans_tcb *tcbs;
    uint64_t commands_dma;
    uint64_t completions_dma;
    uint64_t tcbs_dma;
};

struct ntasi_ans_controller_queue {
    struct ntasi_ans_queue_memory memory;
    struct ntasi_ans_cq_state completion;
    bool admin;
    uint32_t depth;
    uint32_t command_stride;
    uint32_t submission_tail;
    struct {
        struct ntasi_ans_cqe completion;
        uint32_t tcb_status;
        bool valid;
    } deferred;
};

struct ntasi_ans_controller {
    struct ntasi_ans_controller_ops ops;
    void *opaque;
    uint32_t slots;
    uint32_t poll_limit;
    const struct ntasi_ans_hw *hw;
    struct ntasi_ans_controller_queue admin;
    struct ntasi_ans_controller_queue io;
    uint64_t last_result;
    uint64_t last_cap;
    uint16_t last_completion_status;
    uint16_t last_completion_tag;
    uint32_t last_tcb_status;
    uint32_t last_substage;
    uint32_t last_opcode;
    uint32_t last_queue_admin;
    uint32_t last_cq_head;
    uint32_t last_cq_phase;
    uint32_t failure_operation;
    uint32_t failure_register;
    uint32_t failure_observed;
    uint32_t failure_expected;
    uint32_t failure_mask;
    uint32_t failure_attempts;
    uint32_t failure_fault_id;
    uint32_t failure_completion_code;
    struct ntasi_ans_sqe last_sqe;
    struct ntasi_ans_tcb last_tcb;
    struct ntasi_ans_cqe last_cqe;
    bool enabled;
    bool io_queues_created;
};

/*
 * Starts the ANS NVMe block after ASC/RTKit has been booted by the caller.
 * Waits for ANS boot status, configures the selected legacy or linear/NVMMU
 * contract, disables the controller, programs admin queues and CC entry
 * sizes, enables it, then creates the single I/O CQ/SQ.
 */
int ntasi_ans_controller_start(
    struct ntasi_ans_controller *controller,
    const struct ntasi_ans_controller_ops *ops,
    void *opaque,
    uint32_t slots,
    uint32_t poll_limit,
    const struct ntasi_ans_queue_memory *admin,
    const struct ntasi_ans_queue_memory *io);

int ntasi_ans_controller_start_variant(
    struct ntasi_ans_controller *controller,
    const struct ntasi_ans_controller_ops *ops,
    const struct ntasi_ans_hw *hw,
    void *opaque,
    uint32_t slots,
    uint32_t poll_limit,
    const struct ntasi_ans_queue_memory *admin,
    const struct ntasi_ans_queue_memory *io);

/* Caller serializes queue access and retains CID/PRP/DMA ownership until
 * completion consumption succeeds. Submit publishes one command without
 * waiting, RTKit service, or a stall. Native admin/data CID ranges are checked. */
int ntasi_ans_controller_submit(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    const struct ntasi_ans_sqe *command,
    enum ntasi_ans_dma_direction direction, uint8_t tag);

/* ISR-synchronized APIs: peek returns 0 empty / 1 ready / negative error.
 * Validate the CID against live requests before consume. Consume additionally
 * returns 2 for a busy native release; CQ head/tag stay owned on busy/error. */
int ntasi_ans_controller_peek_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion);
int ntasi_ans_controller_consume_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion, uint32_t *tcb_status, bool acknowledge);
/* A batch consumer acknowledges once after all successful CQ advances, even
 * when its next native release is busy. Legacy polling acknowledges each CQE. */
void ntasi_ans_controller_ack_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue);

/* Serialized polling path: admin CID0, native data CID admin_queue_depth. */
int ntasi_ans_controller_execute(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    const struct ntasi_ans_sqe *command,
    enum ntasi_ans_dma_direction direction,
    uint64_t *result);

/*
 * Takes exactly one completion for a polling waiter. A completion previously
 * consumed by the ISR is returned from the queue's deferred slot; otherwise
 * the hardware CQ is consumed with status-first DMA ordering, advanced, and
 * acknowledged. The optional platform synchronization hook serializes all
 * CQ head/phase/deferred accesses with the ISR.
 */
bool ntasi_ans_controller_take_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion,
    uint32_t *tcb_status);

/*
 * Handles the dedicated level-high ANS completion interrupt. Every
 * phase-valid CQE is consumed, acknowledged, and stashed for its serialized
 * polling waiter before returning. If both heads are already phase-empty,
 * the interrupt is claimed as a delayed delivery of an earlier acknowledgement.
 * The caller must already be running in the platform ISR's synchronized
 * context. This routine never services ASC/RTKit or performs a long poll.
 */
bool ntasi_ans_controller_handle_interrupt(
    struct ntasi_ans_controller *controller);

/* Deletes I/O queues, performs normal shutdown, and disables the controller. */
int ntasi_ans_controller_stop(struct ntasi_ans_controller *controller);

#endif

/* SPDX-License-Identifier: MIT */
#include "AppleNvmeControllerCore.h"

#include <string.h>

static void service(struct ntasi_ans_controller *controller)
{
    if (controller->ops.service != NULL)
        controller->ops.service(controller->opaque);
}

static void dma_read_barrier(struct ntasi_ans_controller *controller)
{
    if (controller->ops.dma_read_barrier != NULL)
        controller->ops.dma_read_barrier(controller->opaque);
}

static void dma_write_barrier(struct ntasi_ans_controller *controller)
{
    if (controller->ops.dma_write_barrier != NULL)
        controller->ops.dma_write_barrier(controller->opaque);
}

static uint32_t read32(struct ntasi_ans_controller *controller, uint32_t offset)
{
    return controller->ops.read32(controller->opaque, offset);
}

static void write32(struct ntasi_ans_controller *controller, uint32_t offset,
                    uint32_t value)
{
    controller->ops.write32(controller->opaque, offset, value);
}

static void write64_lo_hi(struct ntasi_ans_controller *controller,
                          uint32_t offset, uint64_t value)
{
    if (controller->hw->queue_address_high_first) {
        write32(controller, offset + 4u, (uint32_t)(value >> 32));
        dma_write_barrier(controller);
        write32(controller, offset, (uint32_t)value);
        dma_write_barrier(controller);
        return;
    }
    write32(controller, offset, (uint32_t)value);
    write32(controller, offset + 4u, (uint32_t)(value >> 32));
}

static uint64_t read64_lo_hi(struct ntasi_ans_controller *controller,
                             uint32_t offset)
{
    uint64_t low = read32(controller, offset);
    uint64_t high = read32(controller, offset + 4u);

    return low | high << 32;
}

static void record_failure(struct ntasi_ans_controller *controller,
                           uint32_t operation, uint32_t offset,
                           uint32_t observed, uint32_t expected,
                           uint32_t mask, uint32_t attempts)
{
    controller->failure_operation = operation;
    controller->failure_register = offset;
    controller->failure_observed = observed;
    controller->failure_expected = expected;
    controller->failure_mask = mask;
    controller->failure_attempts = attempts;
}

static bool fault_injected(struct ntasi_ans_controller *controller,
                           uint32_t operation, uint32_t offset)
{
    int fault;

    if (controller->ops.fault == NULL)
        return false;
    fault = controller->ops.fault(controller->opaque,
                                  controller->last_substage,
                                  operation, offset);
    if (fault <= 0)
        return false;
    controller->failure_fault_id = (uint32_t)fault;
    record_failure(controller, operation, offset, (uint32_t)fault, 0, 0, 0);
    return true;
}

static bool poll_mask(struct ntasi_ans_controller *controller, uint32_t offset,
                      uint32_t mask, uint32_t expected)
{
    uint32_t attempt;
    uint32_t observed = 0;

    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_POLL, offset))
        return false;

    for (attempt = 0; attempt < controller->poll_limit; ++attempt) {
        service(controller);
        observed = read32(controller, offset);
        if ((observed & mask) == expected)
            return true;
        /* A fatal controller cannot become ready through continued polling.
         * Preserve the exact status instead of spending the whole polling
         * budget with the caller's Storport locks held. */
        if (offset == NTASI_ANS_REG_CSTS && expected == NTASI_ANS_CSTS_RDY &&
            (observed & NTASI_ANS_CSTS_CFS)) {
            attempt++;
            break;
        }
    }
    record_failure(controller, NTASI_ANS_OPERATION_MMIO_POLL, offset,
                   observed, expected, mask, attempt);
    return false;
}

static bool queue_valid(const struct ntasi_ans_queue_memory *queue,
                        bool needs_tcbs)
{
    const uint64_t align_mask = NTASI_ANS_QUEUE_ALIGN - 1u;

    return queue != NULL && queue->commands != NULL &&
           queue->completions != NULL && (!needs_tcbs || queue->tcbs != NULL) &&
           (queue->commands_dma & align_mask) == 0 &&
           (queue->completions_dma & align_mask) == 0 &&
           (!needs_tcbs || (queue->tcbs_dma & align_mask) == 0);
}

static void queue_init(struct ntasi_ans_controller_queue *queue,
                       const struct ntasi_ans_queue_memory *memory,
                       const struct ntasi_ans_hw *hw,
                       uint32_t depth, uint32_t tcb_slots, bool admin)
{
    queue->memory = *memory;
    queue->completion = NTASI_ANS_CQ_STATE_INIT;
    queue->admin = admin;
    queue->depth = depth;
    queue->command_stride = admin ? NTASI_ANS_SQE_SIZE : hw->io_command_stride;
    queue->submission_tail = 0;
    memset(&queue->deferred, 0, sizeof(queue->deferred));
    memset(queue->memory.commands, 0,
           ntasi_ans_command_bytes(hw, admin, depth));
    memset(queue->memory.completions, 0, ntasi_ans_cq_bytes(depth));
    if (hw->submission_mode == NTASI_ANS_SUBMISSION_LINEAR_NVMMU)
        memset(queue->memory.tcbs, 0, ntasi_ans_tcb_bytes(tcb_slots));
}

static bool queue_head_ready(const struct ntasi_ans_controller_queue *queue)
{
    uint16_t status;

    if (queue->memory.completions == NULL || queue->depth == 0 ||
        queue->completion.head >= queue->depth)
        return true;
    memcpy(&status,
           &queue->memory.completions[queue->completion.head].status,
           sizeof(status));
    return ntasi_ans_cqe_ready(status, queue->completion.phase);
}

int ntasi_ans_controller_peek_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion)
{
    struct ntasi_ans_cqe *entry;
    uint16_t status;

    if (controller == NULL || queue == NULL || completion == NULL)
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    if (queue->memory.completions == NULL || queue->depth == 0 ||
        queue->completion.head >= queue->depth)
        return 0;

    entry = &queue->memory.completions[queue->completion.head];
    controller->last_queue_admin = queue->admin ? 1u : 0u;
    controller->last_cq_head = queue->completion.head;
    controller->last_cq_phase = queue->completion.phase ? 1u : 0u;

    /*
     * The controller publishes status/phase last. Observe it first, order all
     * subsequent payload loads after that observation, then revalidate the
     * copied status before consuming the entry.
     */
    memcpy(&status, &entry->status, sizeof(status));
    if (!ntasi_ans_cqe_ready(status, queue->completion.phase))
        return 0;
    dma_read_barrier(controller);
    memcpy(completion, entry, sizeof(*completion));
    controller->last_cqe = *completion;
    if (completion->status != status ||
        !ntasi_ans_cqe_ready(completion->status, queue->completion.phase))
        return 0;

    return 1;
}

int ntasi_ans_controller_consume_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion,
    uint32_t *tcb_status, bool acknowledge)
{
    int ready;
    if (tcb_status == NULL) return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    ready = ntasi_ans_controller_peek_completion(controller, queue, completion);
    if (ready != 1) return ready;
    *tcb_status = 0;
    if (controller->hw->submission_mode ==
        NTASI_ANS_SUBMISSION_LINEAR_NVMMU) {
        if (controller->ops.unmap_command != NULL) {
            /* A busy unmap leaves CQ head and tag untouched for retry. */
            int status = controller->ops.unmap_command(controller->opaque,
                                  queue->admin, completion->tag);
            if (status == 1) return 2;
            if (status != 0) {
                record_failure(controller, NTASI_ANS_OPERATION_TCB_INVALIDATE,
                               NTASI_ANS_REG_NVMMU_TCB_STAT,
                               (uint32_t)status, 0, UINT32_MAX, 1);
                return NTASI_ANS_CONTROLLER_ERR_TCB_INVALIDATE;
            }
        } else {
            write32(controller, NTASI_ANS_REG_NVMMU_TCB_INVAL,
                    completion->tag);
            *tcb_status = read32(controller, NTASI_ANS_REG_NVMMU_TCB_STAT);
        }
    }
    controller->last_tcb_status = *tcb_status;
    ntasi_ans_cq_advance(&queue->completion, queue->depth);
    if (acknowledge) ntasi_ans_controller_ack_completion(controller, queue);
    return 1;
}

void ntasi_ans_controller_ack_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue)
{
    write32(controller, ntasi_ans_cq_db_off(queue->admin), queue->completion.head);
}

static bool queue_take_hardware_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion,
    uint32_t *tcb_status)
{
    return ntasi_ans_controller_consume_completion(controller, queue,
                 completion, tcb_status, true) == 1;
}

struct queue_take_context {
    struct ntasi_ans_controller *controller;
    struct ntasi_ans_controller_queue *queue;
    struct ntasi_ans_cqe *completion;
    uint32_t *tcb_status;
};

static bool queue_take_for_poller(void *opaque)
{
    struct queue_take_context *context = opaque;
    struct ntasi_ans_controller_queue *queue = context->queue;

    if (queue->deferred.valid) {
        *context->completion = queue->deferred.completion;
        *context->tcb_status = queue->deferred.tcb_status;
        queue->deferred.valid = false;
        return true;
    }
    return queue_take_hardware_completion(
        context->controller, queue, context->completion,
        context->tcb_status);
}

bool ntasi_ans_controller_take_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    struct ntasi_ans_cqe *completion,
    uint32_t *tcb_status)
{
    struct queue_take_context context;

    if (controller == NULL || queue == NULL || completion == NULL ||
        tcb_status == NULL || !controller->enabled)
        return false;
    context.controller = controller;
    context.queue = queue;
    context.completion = completion;
    context.tcb_status = tcb_status;
    if (controller->ops.synchronize != NULL) {
        return controller->ops.synchronize(
            controller->opaque, queue_take_for_poller, &context);
    }
    return queue_take_for_poller(&context);
}

static bool queue_stash_hardware_completion(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue)
{
    if (queue->deferred.valid)
        return false;
    if (!queue_take_hardware_completion(
            controller, queue, &queue->deferred.completion,
            &queue->deferred.tcb_status))
        return false;
    queue->deferred.valid = true;
    return true;
}

bool ntasi_ans_controller_handle_interrupt(
    struct ntasi_ans_controller *controller)
{
    bool handled;

    if (controller == NULL || !controller->enabled ||
        controller->hw == NULL)
        return false;

    /*
     * Commands are serialized at tag zero, so each queue can have at most one
     * completion awaiting its poller. Drain both dedicated queues before EOI;
     * their CQ-head writes are the level interrupt acknowledgement.
     */
    handled = queue_stash_hardware_completion(controller, &controller->io);
    handled = queue_stash_hardware_completion(controller,
                                               &controller->admin) || handled;
    if (handled)
        return true;

    return !queue_head_ready(&controller->io) &&
           !queue_head_ready(&controller->admin);
}

int ntasi_ans_controller_submit(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    const struct ntasi_ans_sqe *command,
    enum ntasi_ans_dma_direction direction,
    uint8_t tag)
{
    struct ntasi_ans_sqe *slot;
    uint8_t *command_base;
    if (controller == NULL || queue == NULL || command == NULL ||
        controller->ops.read32 == NULL || controller->ops.write32 == NULL ||
        controller->hw == NULL || controller->slots == 0 ||
        queue->memory.commands == NULL || queue->depth == 0 ||
        tag >= controller->slots ||
        (direction != NTASI_ANS_DMA_FROM_DEVICE &&
         direction != NTASI_ANS_DMA_TO_DEVICE))
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    if (!controller->enabled) return NTASI_ANS_CONTROLLER_ERR_NOT_STARTED;
    if (controller->hw->submission_mode == NTASI_ANS_SUBMISSION_LINEAR_NVMMU) {
        if (queue->memory.tcbs == NULL ||
            (queue->admin && tag >= controller->hw->admin_queue_depth) ||
            (!queue->admin && tag < controller->hw->admin_queue_depth))
            return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    } else if (queue->submission_tail >= queue->depth) {
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    }
    command_base = queue->memory.commands;
    if (controller->hw->submission_mode ==
        NTASI_ANS_SUBMISSION_LINEAR_NVMMU) {
        slot = (struct ntasi_ans_sqe *)(command_base +
                                        (size_t)tag * queue->command_stride);
    } else {
        slot = (struct ntasi_ans_sqe *)(command_base +
            (size_t)queue->submission_tail * queue->command_stride);
    }
    *slot = *command;
    slot->tag = tag;
    slot->rsvd = 0;
    if (controller->hw->submission_mode ==
        NTASI_ANS_SUBMISSION_LINEAR_NVMMU) {
        ntasi_ans_tcb_fill(&queue->memory.tcbs[tag], slot, direction);
        controller->last_tcb = queue->memory.tcbs[tag];
    } else {
        memset(&controller->last_tcb, 0, sizeof(controller->last_tcb));
    }
    controller->last_sqe = *slot;
    memset(&controller->last_cqe, 0, sizeof(controller->last_cqe));
    controller->last_opcode = command->opcode;
    controller->last_queue_admin = queue->admin ? 1u : 0u;
    controller->last_cq_head = queue->completion.head;
    controller->last_cq_phase = queue->completion.phase ? 1u : 0u;
    controller->last_tcb_status = 0;
    if (!queue->admin) {
        controller->last_substage = NTASI_ANS_SUBSTAGE_IO_COMMAND_WAIT;
    } else if (command->opcode == NTASI_ANS_ADMIN_CMD_CREATE_CQ) {
        controller->last_substage = NTASI_ANS_SUBSTAGE_CREATE_IOCQ_WAIT;
    } else if (command->opcode == NTASI_ANS_ADMIN_CMD_CREATE_SQ) {
        controller->last_substage = NTASI_ANS_SUBSTAGE_CREATE_IOSQ_WAIT;
    } else if (command->opcode == NTASI_ANS_ADMIN_CMD_IDENTIFY) {
        controller->last_substage = NTASI_ANS_SUBSTAGE_IDENTIFY_WAIT;
    } else {
        controller->last_substage = NTASI_ANS_SUBSTAGE_ADMIN_COMMAND_WAIT;
    }
    {
        uint32_t doorbell = controller->hw->submission_mode ==
            NTASI_ANS_SUBMISSION_LINEAR_NVMMU
                ? ntasi_ans_sq_db_off(queue->admin)
                : ntasi_ans_conventional_sq_db_off(queue->admin);

        if (fault_injected(controller,
                           NTASI_ANS_OPERATION_COMMAND_DOORBELL,
                           doorbell))
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    }
    dma_write_barrier(controller);

    if (controller->hw->submission_mode ==
        NTASI_ANS_SUBMISSION_LINEAR_NVMMU) {
        if (controller->ops.map_command != NULL &&
            controller->ops.map_command(controller->opaque, queue->admin,
                tag, queue->memory.tcbs_dma +
                     (uint64_t)tag * NTASI_ANS_TCB_SIZE, slot) != 0)
            return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
        write32(controller, ntasi_ans_sq_db_off(queue->admin), tag);
    } else {
        queue->submission_tail++;
        if (queue->submission_tail == queue->depth)
            queue->submission_tail = 0;
        write32(controller, ntasi_ans_conventional_sq_db_off(queue->admin),
                queue->submission_tail);
    }

    return NTASI_ANS_CONTROLLER_OK;
}

int ntasi_ans_controller_execute(
    struct ntasi_ans_controller *controller,
    struct ntasi_ans_controller_queue *queue,
    const struct ntasi_ans_sqe *command,
    enum ntasi_ans_dma_direction direction,
    uint64_t *result)
{
    struct ntasi_ans_cqe completion;
    uint32_t attempt;
    uint32_t tcb_status;
    uint8_t tag = 0;
    int status;

    if (controller == NULL || queue == NULL || command == NULL ||
        controller->ops.read32 == NULL || controller->ops.write32 == NULL ||
        controller->hw == NULL || controller->slots == 0 || controller->poll_limit == 0 ||
        (direction != NTASI_ANS_DMA_FROM_DEVICE &&
         direction != NTASI_ANS_DMA_TO_DEVICE))
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    if (!controller->enabled)
        return NTASI_ANS_CONTROLLER_ERR_NOT_STARTED;

    /* Linear ANS uses one NVMMU tag space for both queues. Linux reserves
     * the first admin_queue_depth tags for admin requests. This synchronous
     * engine uses one slot per queue, but those slots must still be distinct.
     * Conventional controllers retain their original tag-zero behavior. */
    if (controller->hw->submission_mode == NTASI_ANS_SUBMISSION_LINEAR_NVMMU &&
        !queue->admin) {
        if (controller->hw->admin_queue_depth >= controller->slots)
            return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
        tag = (uint8_t)controller->hw->admin_queue_depth;
    }

    service(controller);
    status = ntasi_ans_controller_submit(controller, queue, command, direction, tag);
    if (status != NTASI_ANS_CONTROLLER_OK) return status;
    service(controller);

    for (attempt = 0; attempt < controller->poll_limit; ++attempt) {
        service(controller);
        if (!ntasi_ans_controller_take_completion(
                controller, queue, &completion, &tcb_status))
            continue;

        controller->last_result = completion.result;
        controller->last_completion_status = completion.status;
        controller->last_completion_tag = completion.tag;
        controller->last_tcb_status = tcb_status;

        if (tcb_status != 0) {
            record_failure(controller, NTASI_ANS_OPERATION_TCB_INVALIDATE,
                           NTASI_ANS_REG_NVMMU_TCB_STAT, tcb_status, 0,
                           UINT32_MAX, attempt + 1u);
            return NTASI_ANS_CONTROLLER_ERR_TCB_INVALIDATE;
        }
        if (completion.tag != tag) {
            record_failure(controller, NTASI_ANS_OPERATION_CQE_VALIDATE,
                           ntasi_ans_cq_db_off(queue->admin), completion.tag,
                           tag, UINT32_MAX, attempt + 1u);
            return NTASI_ANS_CONTROLLER_ERR_COMPLETION_TAG;
        }
        if (ntasi_ans_cqe_code(completion.status) != 0) {
            controller->failure_completion_code =
                ntasi_ans_cqe_code(completion.status);
            record_failure(controller, NTASI_ANS_OPERATION_CQE_VALIDATE,
                           ntasi_ans_cq_db_off(queue->admin),
                           completion.status, queue->completion.phase,
                           UINT32_C(0xffff), attempt + 1u);
            return NTASI_ANS_CONTROLLER_ERR_COMPLETION_STATUS;
        }
        if (result != NULL)
            *result = completion.result;
        return NTASI_ANS_CONTROLLER_OK;
    }
    if (queue->memory.completions != NULL && queue->depth != 0 &&
        queue->completion.head < queue->depth) {
        dma_read_barrier(controller);
        controller->last_cqe =
            queue->memory.completions[queue->completion.head];
        controller->last_cq_head = queue->completion.head;
        controller->last_cq_phase = queue->completion.phase ? 1u : 0u;
    }
    record_failure(controller, NTASI_ANS_OPERATION_CQE_WAIT,
                   ntasi_ans_cq_db_off(queue->admin),
                   controller->last_cqe.status,
                   controller->last_cq_phase,
                   UINT32_C(1), attempt);
    return NTASI_ANS_CONTROLLER_ERR_COMMAND_TIMEOUT;
}

int ntasi_ans_controller_start(
    struct ntasi_ans_controller *controller,
    const struct ntasi_ans_controller_ops *ops,
    void *opaque,
    uint32_t slots,
    uint32_t poll_limit,
    const struct ntasi_ans_queue_memory *admin,
    const struct ntasi_ans_queue_memory *io)
{
    return ntasi_ans_controller_start_variant(
        controller, ops, &ntasi_ans_hw_t8103, opaque, slots, poll_limit,
        admin, io);
}

int ntasi_ans_controller_start_variant(
    struct ntasi_ans_controller *controller,
    const struct ntasi_ans_controller_ops *ops,
    const struct ntasi_ans_hw *hw,
    void *opaque,
    uint32_t slots,
    uint32_t poll_limit,
    const struct ntasi_ans_queue_memory *admin,
    const struct ntasi_ans_queue_memory *io)
{
    struct ntasi_ans_sqe command;
    uint32_t value;
    int status;

    bool linear;

    if (controller == NULL || ops == NULL || hw == NULL ||
        ops->read32 == NULL ||
        ops->write32 == NULL || slots == 0 ||
        slots > hw->max_queue_depth || hw->admin_queue_depth == 0 ||
        hw->admin_queue_depth > slots || hw->io_command_stride <
            NTASI_ANS_SQE_SIZE || poll_limit == 0)
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    linear = hw->submission_mode == NTASI_ANS_SUBMISSION_LINEAR_NVMMU;
    if (!queue_valid(admin, linear) || !queue_valid(io, linear))
        return NTASI_ANS_CONTROLLER_ERR_ALIGNMENT;

    memset(controller, 0, sizeof(*controller));
    controller->ops = *ops;
    controller->opaque = opaque;
    controller->slots = slots;
    controller->poll_limit = poll_limit;
    controller->hw = hw;
    queue_init(&controller->admin, admin, hw, hw->admin_queue_depth,
               slots, true);
    queue_init(&controller->io, io, hw, slots, slots, false);

    controller->last_substage = NTASI_ANS_SUBSTAGE_BOOT_STATUS_WAIT;
    if (!poll_mask(controller, NTASI_ANS_REG_BOOT_STATUS, UINT32_MAX,
                   NTASI_ANS_BOOT_STATUS_OK)) {
        if (controller->failure_fault_id != 0)
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
        return NTASI_ANS_CONTROLLER_ERR_BOOT_TIMEOUT;
    }

    if (linear) {
        controller->last_substage = NTASI_ANS_SUBSTAGE_LINEAR_CONFIG;
        if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                           NTASI_ANS_REG_LINEAR_SQ_CTRL))
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
        /*
         * "linear" no longer implies "has the ANS2 control registers".
         * T8142 is linear-NVMMU only and has none of the three below; they
         * are fabric errors there, not writes that get ignored.  See the
         * flags on struct ntasi_ans_hw.
         */
        if (hw->linear_sq_ctrl_present) {
            value = read32(controller, NTASI_ANS_REG_LINEAR_SQ_CTRL);
            write32(controller, NTASI_ANS_REG_LINEAR_SQ_CTRL,
                    value | NTASI_ANS_LINEAR_SQ_EN);
        }
        if (hw->prp_null_check_ctrl_present) {
            value = read32(controller, NTASI_ANS_REG_UNKNOWN_CTRL);
            write32(controller, NTASI_ANS_REG_UNKNOWN_CTRL,
                    value & ~NTASI_ANS_UNKCTRL_PRP_NULL_CHECK);
        }
        if (hw->max_pend_cmds_ctrl_present)
            write32(controller, NTASI_ANS_REG_MAX_PEND_CMDS,
                    ntasi_ans_max_pend_cmds(slots));
        if (controller->ops.configure_queues == NULL)
            write32(controller, NTASI_ANS_REG_NVMMU_NUM,
                    ntasi_ans_nvmmu_num(slots));
    }

    controller->last_substage = NTASI_ANS_SUBSTAGE_DISABLE_REQUEST;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                       NTASI_ANS_REG_CC))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    value = read32(controller, NTASI_ANS_REG_CC);
    /* nvme_disable_ctrl(false) clears shutdown notification as well as EN.
     * A firmware handoff can retain SHN_NORMAL with EN already clear;
     * replaying it here requests shutdown while preparing the new queues. */
    write32(controller, NTASI_ANS_REG_CC,
            value & ~(NTASI_ANS_CC_EN | NTASI_ANS_CC_SHN_MASK));
    controller->last_substage = NTASI_ANS_SUBSTAGE_DISABLE_WAIT;
    if (!poll_mask(controller, NTASI_ANS_REG_CSTS, NTASI_ANS_CSTS_RDY, 0)) {
        if (controller->failure_fault_id != 0)
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
        return NTASI_ANS_CONTROLLER_ERR_DISABLE_TIMEOUT;
    }

    controller->last_substage = NTASI_ANS_SUBSTAGE_ADMIN_QUEUE_PROGRAM;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                       NTASI_ANS_REG_ASQ))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    if (controller->ops.configure_queues != NULL) {
        status = controller->ops.configure_queues(controller->opaque,
                    admin, io, hw->admin_queue_depth, slots);
        if (status != 0)
            return status;
    } else {
    write64_lo_hi(controller, NTASI_ANS_REG_ASQ, admin->commands_dma);
    write64_lo_hi(controller, NTASI_ANS_REG_ACQ, admin->completions_dma);
    write32(controller, NTASI_ANS_REG_AQA,
            ntasi_ans_aqa(hw->admin_queue_depth));
    if (linear) {
        /* Linux programs the NVMMU TCB bases after ASQ/ACQ/AQA. */
        controller->last_substage = NTASI_ANS_SUBSTAGE_NVMMU_TCB_PROGRAM;
        if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                           NTASI_ANS_REG_NVMMU_ASQ_BASE))
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
        write64_lo_hi(controller, NTASI_ANS_REG_NVMMU_ASQ_BASE,
                      admin->tcbs_dma);
        write64_lo_hi(controller, NTASI_ANS_REG_NVMMU_IOSQ_BASE,
                      io->tcbs_dma);
        if (hw->secure_io_queue_registers) {
            /*
             * T8142 admits the linear I/O queues through the secure NVMe BAR
             * instead of the ANS2 LINEAR_SQ controls, in this exact order:
             * geometry, then completion queue, then submission queue.
             *
             * The controller WILL accept the standard Create CQ/SQ admin
             * commands without these, and then fail its first host I/O SQ
             * fetch with a DECERR -- so a missing write here does not show up
             * until data actually moves.  m1n1's
             * nvme_t8142_register_io_queues() is the working reference;
             * preserve its low-dword / barrier / high-dword ordering, because
             * a single 64-bit store is not equivalent at this aperture.
             */
            controller->last_substage =
                NTASI_ANS_SUBSTAGE_SECURE_IO_QUEUE_PROGRAM;
            if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                               NTASI_ANS_REG_T8142_IOQA))
                return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
            write32(controller, NTASI_ANS_REG_T8142_IOQA,
                    ntasi_ans_aqa(slots));
            dma_write_barrier(controller);
            if (hw->queue_address_high_first) {
                write64_lo_hi(controller, NTASI_ANS_REG_T8142_IOCQ_ADDR,
                              io->completions_dma);
                write64_lo_hi(controller, NTASI_ANS_REG_T8142_IOSQ_ADDR,
                              io->commands_dma);
            } else {
            write32(controller, NTASI_ANS_REG_T8142_IOCQ_ADDR,
                    (uint32_t)io->completions_dma);
            dma_write_barrier(controller);
            write32(controller, NTASI_ANS_REG_T8142_IOCQ_ADDR + 4u,
                    (uint32_t)(io->completions_dma >> 32));
            dma_write_barrier(controller);
            write32(controller, NTASI_ANS_REG_T8142_IOSQ_ADDR,
                    (uint32_t)io->commands_dma);
            dma_write_barrier(controller);
            write32(controller, NTASI_ANS_REG_T8142_IOSQ_ADDR + 4u,
                    (uint32_t)(io->commands_dma >> 32));
            dma_write_barrier(controller);
            }
        }
    }
    }

    /*
     * Match nvme_enable_ctrl(): CAP is sampled before configuration, CC is
     * written with EN clear, then CAP is sampled again because the controller
     * is permitted to change it after the initial CC write.  Only the second
     * write sets EN.
     */
    controller->last_substage = NTASI_ANS_SUBSTAGE_CAP_INITIAL_READ;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_READ,
                       NTASI_ANS_REG_CAP))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    controller->last_cap = read64_lo_hi(controller, NTASI_ANS_REG_CAP);
    controller->last_substage = NTASI_ANS_SUBSTAGE_CC_CONFIG_WRITE;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                       NTASI_ANS_REG_CC))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    write32(controller, NTASI_ANS_REG_CC, ntasi_ans_cc_config());
    controller->last_substage = NTASI_ANS_SUBSTAGE_CAP_REREAD;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_READ,
                       NTASI_ANS_REG_CAP))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    controller->last_cap = read64_lo_hi(controller, NTASI_ANS_REG_CAP);
    controller->last_substage = NTASI_ANS_SUBSTAGE_CC_ENABLE_WRITE;
    if (fault_injected(controller, NTASI_ANS_OPERATION_MMIO_WRITE,
                       NTASI_ANS_REG_CC))
        return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
    write32(controller, NTASI_ANS_REG_CC,
            ntasi_ans_cc_config() | NTASI_ANS_CC_EN);
    controller->last_substage = NTASI_ANS_SUBSTAGE_CC_READY_WAIT;
    if (!poll_mask(controller, NTASI_ANS_REG_CSTS, NTASI_ANS_CSTS_RDY,
                   NTASI_ANS_CSTS_RDY)) {
        if (controller->failure_fault_id != 0)
            return NTASI_ANS_CONTROLLER_ERR_FAULT_INJECTED;
        if (controller->failure_observed & NTASI_ANS_CSTS_CFS)
            return NTASI_ANS_CONTROLLER_ERR_FATAL_STATE;
        return NTASI_ANS_CONTROLLER_ERR_ENABLE_TIMEOUT;
    }
    controller->enabled = true;

    ntasi_ans_cmd_create_iocq(&command, 1, slots, io->completions_dma);
    status = ntasi_ans_controller_execute(controller, &controller->admin,
                                          &command, NTASI_ANS_DMA_TO_DEVICE,
                                          NULL);
    if (status != NTASI_ANS_CONTROLLER_OK)
        return status;
    ntasi_ans_cmd_create_iosq(&command, 1, 1, slots, io->commands_dma);
    status = ntasi_ans_controller_execute(controller, &controller->admin,
                                          &command, NTASI_ANS_DMA_TO_DEVICE,
                                          NULL);
    if (status != NTASI_ANS_CONTROLLER_OK)
        return status;
    controller->io_queues_created = true;
    return NTASI_ANS_CONTROLLER_OK;
}

int ntasi_ans_controller_stop(struct ntasi_ans_controller *controller)
{
    struct ntasi_ans_sqe command;
    uint32_t value;
    int status;

    if (controller == NULL)
        return NTASI_ANS_CONTROLLER_ERR_ARGUMENT;
    if (!controller->enabled)
        return NTASI_ANS_CONTROLLER_ERR_NOT_STARTED;

    if (controller->io_queues_created) {
        ntasi_ans_cmd_delete_q(&command, NTASI_ANS_ADMIN_CMD_DELETE_SQ, 1);
        status = ntasi_ans_controller_execute(controller, &controller->admin,
                                              &command, NTASI_ANS_DMA_FROM_DEVICE,
                                              NULL);
        if (status != NTASI_ANS_CONTROLLER_OK)
            return status;
        ntasi_ans_cmd_delete_q(&command, NTASI_ANS_ADMIN_CMD_DELETE_CQ, 1);
        status = ntasi_ans_controller_execute(controller, &controller->admin,
                                              &command, NTASI_ANS_DMA_FROM_DEVICE,
                                              NULL);
        if (status != NTASI_ANS_CONTROLLER_OK)
            return status;
        controller->io_queues_created = false;
    }

    value = read32(controller, NTASI_ANS_REG_CC);
    value &= ~NTASI_ANS_CC_SHN_MASK;
    value |= NTASI_ANS_CC_SHN_NORMAL << NTASI_ANS_CC_SHN_SHIFT;
    write32(controller, NTASI_ANS_REG_CC, value);
    if (!poll_mask(controller, NTASI_ANS_REG_CSTS,
                   NTASI_ANS_CSTS_SHST_MASK,
                   NTASI_ANS_CSTS_SHST_DONE << NTASI_ANS_CSTS_SHST_SHIFT))
        return NTASI_ANS_CONTROLLER_ERR_SHUTDOWN_TIMEOUT;

    value = read32(controller, NTASI_ANS_REG_CC);
    write32(controller, NTASI_ANS_REG_CC, value & ~NTASI_ANS_CC_EN);
    if (!poll_mask(controller, NTASI_ANS_REG_CSTS, NTASI_ANS_CSTS_RDY, 0))
        return NTASI_ANS_CONTROLLER_ERR_DISABLE_TIMEOUT;
    controller->enabled = false;
    return NTASI_ANS_CONTROLLER_OK;
}

/* SPDX-License-Identifier: MIT */
#include "AppleNvmeCore.h"

const struct ntasi_ans_hw ntasi_ans_hw_t8015 = {
    .submission_mode = NTASI_ANS_SUBMISSION_CONVENTIONAL,
    .max_queue_depth = 16,
    .admin_queue_depth = 16,
    .io_command_stride = 128,
    .linear_sq_ctrl_present = false,
    .prp_null_check_ctrl_present = false,
    .max_pend_cmds_ctrl_present = false,
    .secure_io_queue_registers = false,
};

const struct ntasi_ans_hw ntasi_ans_hw_t8103 = {
    .submission_mode = NTASI_ANS_SUBMISSION_LINEAR_NVMMU,
    .max_queue_depth = 64,
    .admin_queue_depth = 2,
    .io_command_stride = NTASI_ANS_SQE_SIZE,
    .linear_sq_ctrl_present = true,
    .prp_null_check_ctrl_present = true,
    .max_pend_cmds_ctrl_present = true,
    .secure_io_queue_registers = false,
};

/*
 * T8142 (M5).  Same submission model as T8103 -- linear NVMMU is the only
 * contract this generation has -- but the ANS2-era control registers are
 * gone.  Byte-for-byte the flags Mu's AppleNANDStorageDxe uses, which is the
 * code that demonstrably reads this SSD on J813.
 */
const struct ntasi_ans_hw ntasi_ans_hw_t8142 = {
    .submission_mode = NTASI_ANS_SUBMISSION_LINEAR_NVMMU,
    .max_queue_depth = 64,
    .admin_queue_depth = 2,
    .io_command_stride = NTASI_ANS_SQE_SIZE,
    .linear_sq_ctrl_present = false,
    .prp_null_check_ctrl_present = false,
    .max_pend_cmds_ctrl_present = false,
    .secure_io_queue_registers = true,
};

const struct ntasi_ans_hw ntasi_ans_hw_t8152 = {
    .submission_mode = NTASI_ANS_SUBMISSION_LINEAR_NVMMU,
    .max_queue_depth = 64,
    .admin_queue_depth = 2,
    .io_command_stride = NTASI_ANS_SQE_SIZE,
    .secure_io_queue_registers = true,
    .queue_address_high_first = true,
};

uint32_t ntasi_ans_pmgr_power_on_value(uint32_t current)
{
    /*
     * Match Asahi Linux e8efe09d4f378992c890d181d65e2ed8d8cb1194
     * drivers/pmdomain/apple/pmgr-pwrstate.c:
     * apple_pmgr_ps_set(..., APPLE_PMGR_PS_ACTIVE, true).  An ACTIVE
     * transition clears both control bits left by a prior power/reset cycle.
     * Merely observing ACTUAL=ACTIVE is insufficient because DEV_DISABLE or
     * PS_RESET may still prevent the subsequent ps_ans2 reset transaction.
     */
    return (current & ~(NTASI_ANS_PMGR_DEV_DISABLE |
                        NTASI_ANS_PMGR_PS_RESET |
                        NTASI_ANS_PMGR_AUTO_ENABLE |
                        NTASI_ANS_PMGR_FLAGS |
                        NTASI_ANS_PMGR_TARGET_MASK)) |
           NTASI_ANS_PMGR_TARGET_ACTIVE;
}

uint32_t ntasi_ans_pmgr_auto_enable_value(uint32_t programmed)
{
    return programmed | NTASI_ANS_PMGR_AUTO_ENABLE;
}

/*
 * True when the domain is already in the steady ACTIVE state we would be
 * programming it into, so no write is required.
 *
 * Measured on live J414s/T6020 (2026-07-30): iBoot leaves ANS2, APCIE_ST,
 * APCIE_ST_SYS and APCIE_ST1_SYS all at ACTUAL=0xf TARGET=0xf before any
 * software touches them, and m1n1's pmgr_init() only ever powers inactive
 * PARENTS up -- it never powers anything down.  So this is the common path
 * on this hardware and it must not thrash state or add latency.
 *
 * ACTUAL==ACTIVE alone is deliberately NOT sufficient: a domain can read
 * ACTUAL=0xf while DEV_DISABLE or PS_RESET is still latched from a previous
 * power/reset cycle, and those bits would block the later ps_ans2 reset
 * transaction.  Such a domain still gets the corrective write.
 */
static bool ntasi_ans_pmgr_is_steady_active(uint32_t value)
{
    return (value & NTASI_ANS_PMGR_ACTUAL_MASK) ==
               NTASI_ANS_PMGR_ACTUAL_ACTIVE &&
           (value & NTASI_ANS_PMGR_TARGET_MASK) ==
               NTASI_ANS_PMGR_TARGET_ACTIVE &&
           (value & (NTASI_ANS_PMGR_DEV_DISABLE |
                     NTASI_ANS_PMGR_PS_RESET)) == 0;
}

int ntasi_ans_pmgr_power_on(const struct ntasi_ans_pmgr_reset_ops *ops,
                            void *opaque, uint32_t poll_limit)
{
    uint32_t programmed;
    uint32_t current;
    uint32_t index;

    if (ops == NULL || ops->read32 == NULL || ops->write32 == NULL ||
        ops->stall_us == NULL || poll_limit == 0)
        return NTASI_ANS_PMGR_POWER_ERR_ARGUMENT;

    current = ops->read32(opaque);
    if (ntasi_ans_pmgr_is_steady_active(current))
        return NTASI_ANS_PMGR_POWER_OK_ALREADY_ACTIVE;

    programmed = ntasi_ans_pmgr_power_on_value(current);
    ops->write32(opaque, programmed);
    for (index = 0; index < poll_limit; ++index) {
        if ((ops->read32(opaque) & NTASI_ANS_PMGR_ACTUAL_MASK) ==
            NTASI_ANS_PMGR_ACTUAL_ACTIVE) {
            ops->write32(opaque,
                         ntasi_ans_pmgr_auto_enable_value(programmed));
            return NTASI_ANS_PMGR_POWER_OK;
        }
        ops->stall_us(opaque, 1);
    }
    return NTASI_ANS_PMGR_POWER_ERR_TIMEOUT;
}

/*
 * The single source of truth for ANS power-domain ordering and for which
 * ACPI _CRS resource carries which domain.  Resource indices are the ABI
 * documented in Mu's AcpiPlatformDxe/AcpiPlatform.c:
 *   3: ANS2   4: APCIE_ST   5: APCIE_ST_SYS   6: APCIE_ST1_SYS
 * Note the emission order (ANS2 first) is deliberately NOT the bring-up
 * order (APCIE_ST first); that is exactly why this mapping is explicit.
 */
const struct ntasi_ans_pmgr_domain_step
    ntasi_ans_pmgr_power_sequence[NTASI_ANS_PMGR_DOMAIN_COUNT] = {
    {NTASI_ANS_PMGR_DOMAIN_APCIE_ST, 4,
     NTASI_ANS_PMGR_OFFSET_APCIE_ST, "APCIE_ST"},
    {NTASI_ANS_PMGR_DOMAIN_ANS2, 3,
     NTASI_ANS_PMGR_OFFSET_ANS2, "ANS2"},
    {NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS, 5,
     NTASI_ANS_PMGR_OFFSET_APCIE_ST_SYS, "APCIE_ST_SYS"},
    {NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS, 6,
     NTASI_ANS_PMGR_OFFSET_APCIE_ST1_SYS, "APCIE_ST1_SYS"},
};

/*
 * The measured SoC geometries.  Adding a machine means adding a row here and
 * a test, not editing arithmetic -- which is the whole point of the change
 * that introduced this table (see the T8142 comment in AppleNvmeCore.h).
 *
 * The T602x row must agree with the compile-time constants the rest of this
 * file and its tests still reference; ntasi_ans_pmgr_soc_layout_selfcheck()
 * proves that rather than leaving it to review.
 */
static const struct ntasi_ans_pmgr_soc_layout ntasi_ans_pmgr_soc_layouts[] = {
    {
        "T602x", NTASI_ANS_T602X_ASC_BASE, NTASI_ANS_PMGR_T602X_EAST_BASE,
        {
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST] = NTASI_ANS_PMGR_OFFSET_APCIE_ST,
            [NTASI_ANS_PMGR_DOMAIN_ANS2] = NTASI_ANS_PMGR_OFFSET_ANS2,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS] =
                NTASI_ANS_PMGR_OFFSET_APCIE_ST_SYS,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS] =
                NTASI_ANS_PMGR_OFFSET_APCIE_ST1_SYS,
        },
        {
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST] = NTASI_ANS_PMGR_POLICY_REQUIRED,
            [NTASI_ANS_PMGR_DOMAIN_ANS2] = NTASI_ANS_PMGR_POLICY_REQUIRED,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS] =
                NTASI_ANS_PMGR_POLICY_REQUIRED,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS] =
                NTASI_ANS_PMGR_POLICY_REQUIRED,
        },
    },
    {
        "T8142", NTASI_ANS_T8142_ASC_BASE, NTASI_ANS_PMGR_T8142_BASE,
        {
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST] =
                NTASI_ANS_PMGR_T8142_OFFSET_APCIE_ST,
            [NTASI_ANS_PMGR_DOMAIN_ANS2] = NTASI_ANS_PMGR_T8142_OFFSET_ANS2,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS] =
                NTASI_ANS_PMGR_T8142_OFFSET_APCIE_ST_SYS,
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS] = 0,
        },
        {
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST] = NTASI_ANS_PMGR_POLICY_REQUIRED,
            [NTASI_ANS_PMGR_DOMAIN_ANS2] = NTASI_ANS_PMGR_POLICY_REQUIRED,
            /* Reads PS_ACTUAL 0 with PS_TARGET already 0xF; Mu's ANS DXE
             * completes bring-up anyway.  Observed, never written. */
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS] =
                NTASI_ANS_PMGR_POLICY_ADVISORY,
            /* No such domain on this SoC. */
            [NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS] =
                NTASI_ANS_PMGR_POLICY_ABSENT,
        },
    },
};

const struct ntasi_ans_pmgr_soc_layout *
ntasi_ans_pmgr_soc_for_asc_base(uint64_t asc_base)
{
    unsigned int index;

    if (asc_base == 0)
        return NULL;
    for (index = 0;
         index < sizeof(ntasi_ans_pmgr_soc_layouts) /
                 sizeof(ntasi_ans_pmgr_soc_layouts[0]);
         ++index)
        if (ntasi_ans_pmgr_soc_layouts[index].asc_base == asc_base)
            return &ntasi_ans_pmgr_soc_layouts[index];
    return NULL;
}

int ntasi_ans_pmgr_validate_layout(const uint64_t *addresses,
                                   unsigned int count,
                                   const struct ntasi_ans_pmgr_soc_layout *soc,
                                   unsigned int *failed_step)
{
    unsigned int index;
    unsigned int other;

    if (failed_step != NULL)
        *failed_step = 0;
    if (addresses == NULL || soc == NULL ||
        count != NTASI_ANS_PMGR_DOMAIN_COUNT)
        return NTASI_ANS_PMGR_LAYOUT_ERR_ARGUMENT;

    for (index = 0; index < count; ++index) {
        const struct ntasi_ans_pmgr_domain_step *step =
            &ntasi_ans_pmgr_power_sequence[index];
        uint64_t address = addresses[step->domain];

        if (failed_step != NULL)
            *failed_step = index;
        /*
         * A domain this SoC does not have must be absent, not plausible.  An
         * address here means the caller resolved something it cannot name, and
         * the whole purpose of this function is to refuse exactly that.
         */
        if (soc->policy[step->domain] == NTASI_ANS_PMGR_POLICY_ABSENT) {
            if (address != 0)
                return NTASI_ANS_PMGR_LAYOUT_ERR_BLOCK;
            continue;
        }
        if (address == 0 ||
            (address & (NTASI_ANS_PMGR_WORD_SIZE - 1u)) != 0)
            return NTASI_ANS_PMGR_LAYOUT_ERR_ALIGNMENT;
        /*
         * Reject a plausible-looking but wrong PMGR block outright.  See the
         * ntasi_ans_pmgr_validate_layout() comment in the header: the wrong
         * base makes these offsets land on always-on DRAM controller
         * power-state registers, so "close enough" is not acceptable here.
         * The base is now the SELECTED SoC's, so a T602x address on a T8142
         * (and the reverse) fails here instead of being waved through.
         */
        if ((address & ~UINT64_C(0xffff)) != soc->block_base)
            return NTASI_ANS_PMGR_LAYOUT_ERR_BLOCK;
        if ((address - soc->block_base) != soc->offset[step->domain])
            return NTASI_ANS_PMGR_LAYOUT_ERR_OFFSET;
        for (other = 0; other < index; ++other) {
            enum ntasi_ans_pmgr_domain_id prior =
                ntasi_ans_pmgr_power_sequence[other].domain;

            if (soc->policy[prior] != NTASI_ANS_PMGR_POLICY_ABSENT &&
                addresses[prior] == address)
                return NTASI_ANS_PMGR_LAYOUT_ERR_DUPLICATE;
        }
    }
    if (failed_step != NULL)
        *failed_step = count;
    return NTASI_ANS_PMGR_LAYOUT_OK;
}

int ntasi_ans_pmgr_power_on_sequence(
    const struct ntasi_ans_pmgr_reset_ops *ops,
    const struct ntasi_ans_pmgr_sequence_ops *sequence_ops,
    const struct ntasi_ans_pmgr_soc_layout *soc,
    void *context, uint32_t poll_limit, unsigned int *failed_step)
{
    unsigned int index;

    if (failed_step != NULL)
        *failed_step = 0;
    if (ops == NULL || sequence_ops == NULL || soc == NULL ||
        sequence_ops->domain == NULL || poll_limit == 0)
        return NTASI_ANS_PMGR_POWER_ERR_ARGUMENT;

    for (index = 0; index < NTASI_ANS_PMGR_DOMAIN_COUNT; ++index) {
        const struct ntasi_ans_pmgr_domain_step *step =
            &ntasi_ans_pmgr_power_sequence[index];
        void *opaque;
        int status;

        if (failed_step != NULL)
            *failed_step = index;
        /*
         * Step over what this SoC does not have or does not need, BEFORE
         * resolving the opaque -- an absent domain has no mapped word, so
         * sequence_ops->domain() would return NULL and be indistinguishable
         * from a real resolution failure.
         */
        if (soc->policy[step->domain] != NTASI_ANS_PMGR_POLICY_REQUIRED) {
            if (sequence_ops->done != NULL)
                sequence_ops->done(context, step,
                                   NTASI_ANS_PMGR_POWER_OK_SKIPPED);
            continue;
        }
        opaque = sequence_ops->domain(context, step->domain);
        if (opaque == NULL)
            return NTASI_ANS_PMGR_POWER_ERR_ARGUMENT;
        if (sequence_ops->begin != NULL)
            sequence_ops->begin(context, step);
        status = ntasi_ans_pmgr_power_on(ops, opaque, poll_limit);
        if (sequence_ops->done != NULL)
            sequence_ops->done(context, step, status);
        /*
         * Fail closed: stop before touching any later domain.  A caller that
         * ignored this would issue MMIO against a domain whose parent never
         * came up, which stalls the bus rather than returning an error.
         */
        if (status != NTASI_ANS_PMGR_POWER_OK &&
            status != NTASI_ANS_PMGR_POWER_OK_ALREADY_ACTIVE)
            return status;
    }
    if (failed_step != NULL)
        *failed_step = NTASI_ANS_PMGR_DOMAIN_COUNT;
    return NTASI_ANS_PMGR_POWER_OK;
}

uint32_t ntasi_ans_pmgr_disable_value(uint32_t current)
{
    return (current & ~(NTASI_ANS_PMGR_FLAGS |
                        NTASI_ANS_PMGR_DEV_DISABLE)) |
           NTASI_ANS_PMGR_DEV_DISABLE;
}

uint32_t ntasi_ans_pmgr_assert_value(uint32_t current)
{
    return (current & ~(NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_RESET)) |
           NTASI_ANS_PMGR_RESET;
}

uint32_t ntasi_ans_pmgr_deassert_value(uint32_t current)
{
    return current & ~(NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_RESET);
}

uint32_t ntasi_ans_pmgr_enable_value(uint32_t current)
{
    return current & ~(NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_DEV_DISABLE);
}

static int ntasi_ans_pmgr_wait(const struct ntasi_ans_pmgr_reset_ops *ops,
                               void *opaque, uint32_t poll_limit,
                               uint32_t mask, uint32_t expected,
                               int timeout_status)
{
    uint32_t index;

    for (index = 0; index < poll_limit; ++index) {
        if ((ops->read32(opaque) & mask) == expected)
            return NTASI_ANS_PMGR_RESET_OK;
        ops->stall_us(opaque, 1);
    }
    return timeout_status;
}

int ntasi_ans_pmgr_reset_assert(const struct ntasi_ans_pmgr_reset_ops *ops,
                                void *opaque, uint32_t poll_limit)
{
    uint32_t value;
    int status;

    if (ops == NULL || ops->read32 == NULL || ops->write32 == NULL ||
        ops->stall_us == NULL || poll_limit == 0)
        return NTASI_ANS_PMGR_RESET_ERR_ARGUMENT;

    value = ops->read32(opaque);
    if ((value & NTASI_ANS_PMGR_ACTUAL_MASK) !=
        NTASI_ANS_PMGR_ACTUAL_ACTIVE)
        return NTASI_ANS_PMGR_RESET_ERR_NOT_ACTIVE;
    if ((value & (NTASI_ANS_PMGR_RESET | NTASI_ANS_PMGR_DEV_DISABLE)) != 0)
        return NTASI_ANS_PMGR_RESET_ERR_STALE_CONTROL;

    ops->write32(opaque, ntasi_ans_pmgr_disable_value(value));
    status = ntasi_ans_pmgr_wait(
        ops, opaque, poll_limit,
        NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_DEV_DISABLE,
        NTASI_ANS_PMGR_DEV_DISABLE,
        NTASI_ANS_PMGR_RESET_ERR_DISABLE_TIMEOUT);
    if (status != NTASI_ANS_PMGR_RESET_OK)
        return status;

    value = ops->read32(opaque);
    ops->write32(opaque, ntasi_ans_pmgr_assert_value(value));
    status = ntasi_ans_pmgr_wait(
        ops, opaque, poll_limit,
        NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_DEV_DISABLE |
            NTASI_ANS_PMGR_RESET,
        NTASI_ANS_PMGR_DEV_DISABLE | NTASI_ANS_PMGR_RESET,
        NTASI_ANS_PMGR_RESET_ERR_ASSERT_TIMEOUT);
    if (status != NTASI_ANS_PMGR_RESET_OK)
        return status;

    /* Matches the reset controller's 1-2 us post-operation delay. */
    ops->stall_us(opaque, 2);
    return NTASI_ANS_PMGR_RESET_OK;
}

int ntasi_ans_pmgr_reset_deassert(const struct ntasi_ans_pmgr_reset_ops *ops,
                                  void *opaque, uint32_t poll_limit)
{
    uint32_t value;
    int status;

    if (ops == NULL || ops->read32 == NULL || ops->write32 == NULL ||
        ops->stall_us == NULL || poll_limit == 0)
        return NTASI_ANS_PMGR_RESET_ERR_ARGUMENT;

    value = ops->read32(opaque);
    if ((value & (NTASI_ANS_PMGR_RESET | NTASI_ANS_PMGR_DEV_DISABLE)) !=
        (NTASI_ANS_PMGR_RESET | NTASI_ANS_PMGR_DEV_DISABLE))
        return NTASI_ANS_PMGR_RESET_ERR_STALE_CONTROL;

    ops->write32(opaque, ntasi_ans_pmgr_deassert_value(value));
    status = ntasi_ans_pmgr_wait(
        ops, opaque, poll_limit,
        NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_RESET,
        0, NTASI_ANS_PMGR_RESET_ERR_DEASSERT_TIMEOUT);
    if (status != NTASI_ANS_PMGR_RESET_OK)
        return status;

    value = ops->read32(opaque);
    ops->write32(opaque, ntasi_ans_pmgr_enable_value(value));
    status = ntasi_ans_pmgr_wait(
        ops, opaque, poll_limit,
        NTASI_ANS_PMGR_FLAGS | NTASI_ANS_PMGR_RESET |
            NTASI_ANS_PMGR_DEV_DISABLE | NTASI_ANS_PMGR_ACTUAL_MASK,
        NTASI_ANS_PMGR_ACTUAL_ACTIVE,
        NTASI_ANS_PMGR_RESET_ERR_ENABLE_TIMEOUT);
    if (status != NTASI_ANS_PMGR_RESET_OK)
        return status;

    ops->stall_us(opaque, 2);
    return NTASI_ANS_PMGR_RESET_OK;
}

uint32_t ntasi_ans_aqa(uint32_t slots)
{
    /* nvme.c:366: ((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1) */
    return ((slots - 1u) << 16) | (slots - 1u);
}

uint32_t ntasi_ans_max_pend_cmds(uint32_t slots)
{
    /* Linux apple.c:1157-1160: register takes the actual depth, not depth-1. */
    return (slots << 16) | slots;
}

uint32_t ntasi_ans_nvmmu_num(uint32_t slots)
{
    /* nvme.c:355: write32(nvme_base + NVMMU_NUM, NVME_QUEUE_SIZE - 1) */
    return slots - 1u;
}

uint32_t ntasi_ans_cc_config(void)
{
    /* Matches nvme_enable_ctrl(): standard 64-byte SQEs and 16-byte CQEs. */
    return NTASI_ANS_CC_IOSQES_64 | NTASI_ANS_CC_IOCQES_16;
}

uint32_t ntasi_ans_sq_db_off(bool admin)
{
    /* nvme.c:234-237 */
    return admin ? NTASI_ANS_REG_DB_LINEAR_ASQ : NTASI_ANS_REG_DB_LINEAR_IOSQ;
}

uint32_t ntasi_ans_conventional_sq_db_off(bool admin)
{
    return admin ? NTASI_ANS_REG_DB_ASQ : NTASI_ANS_REG_DB_IOSQ;
}

uint32_t ntasi_ans_cq_db_off(bool admin)
{
    /* nvme.c:270-273 */
    return admin ? NTASI_ANS_REG_DB_ACQ : NTASI_ANS_REG_DB_IOCQ;
}

size_t ntasi_ans_sq_bytes(uint32_t slots)
{
    return (size_t)slots * NTASI_ANS_SQE_SIZE;
}

size_t ntasi_ans_cq_bytes(uint32_t slots)
{
    return (size_t)slots * NTASI_ANS_CQE_SIZE;
}

size_t ntasi_ans_tcb_bytes(uint32_t slots)
{
    return (size_t)slots * NTASI_ANS_TCB_SIZE;
}

size_t ntasi_ans_command_bytes(const struct ntasi_ans_hw *hw,
                               bool admin, uint32_t slots)
{
    size_t stride;

    if (hw == NULL)
        return 0;
    stride = admin ? NTASI_ANS_SQE_SIZE : hw->io_command_stride;
    return (size_t)slots * stride;
}

void ntasi_ans_tcb_fill(struct ntasi_ans_tcb *tcb,
                        const struct ntasi_ans_sqe *sqe,
                        enum ntasi_ans_dma_direction direction)
{
    /* Linux apple.c:826-837, and m1n1 nvme.c:406-418. */
    *tcb = (struct ntasi_ans_tcb){0};
    tcb->opcode = sqe->opcode;
    /*
     * A COMMAND THAT MOVES NO DATA MUST DECLARE NO DIRECTION.
     *
     * MEASURED 2026-08-14 on J813: with dma_flags set unconditionally from
     * `direction`, every command carrying a buffer worked -- 47 READ10, plus
     * READ_CAPACITY, INQUIRY, SERVICE_ACTION16 -- and FLUSH, the only command
     * with no buffer, came back an error.  ProcessScsi turned that into sense
     * HARDWARE_ERROR/0x44 on SYNCHRONIZE CACHE and Windows Setup died at 45%
     * with 0x8007045D ERROR_IO_DEVICE out of FileStream::Flush.  Every FUA
     * write flushes too, so this was not confined to explicit flushes.
     *
     * The NVMMU was being handed a transfer descriptor that claimed a DMA
     * direction while pointing at PRP 0.  m1n1 gets this right and its
     * nvme_flush() succeeds on this exact controller -- verified live over the
     * proxy before this was written, which is also what ruled out the
     * hypothesis that Apple's ANS simply does not implement FLUSH.
     */
    tcb->dma_flags = sqe->prp1 != 0 ? (uint8_t)direction : 0u;
    tcb->command_id = sqe->tag;
    tcb->length = (uint16_t)sqe->cdw12;
    tcb->prp1 = sqe->prp1;
    tcb->prp2 = sqe->prp2;
}

void ntasi_ans_cmd_flush(struct ntasi_ans_sqe *c, uint32_t nsid)
{
    /* nvme.c:522-525 */
    *c = (struct ntasi_ans_sqe){0};
    c->opcode = NTASI_ANS_CMD_FLUSH;
    c->nsid = nsid;
}

void ntasi_ans_cmd_rw(struct ntasi_ans_sqe *c, uint8_t opcode, uint32_t nsid, uint64_t lba,
                      uint32_t cdw12, uint64_t prp1, uint64_t prp2)
{
    /* nvme.c:541-549 (READ path; see header for the WRITE generalization note) */
    *c = (struct ntasi_ans_sqe){0};
    c->opcode = opcode;
    c->nsid = nsid;
    c->prp1 = prp1;
    c->prp2 = prp2;
    c->cdw10 = (uint32_t)(lba & 0xffffffffu);
    c->cdw11 = (uint32_t)(lba >> 32);
    c->cdw12 = cdw12;
}

void ntasi_ans_cmd_create_iocq(struct ntasi_ans_sqe *c, uint16_t qid, uint32_t slots,
                               uint64_t prp1)
{
    /* nvme.c:373-384 */
    *c = (struct ntasi_ans_sqe){0};
    c->opcode = NTASI_ANS_ADMIN_CMD_CREATE_CQ;
    c->prp1 = prp1;
    c->cdw10 = (uint32_t)qid | ((slots - 1u) << 16);
    c->cdw11 = NTASI_ANS_QUEUE_CONTIGUOUS |
               NTASI_ANS_QUEUE_CQ_IRQ_ENABLED;
}

void ntasi_ans_cmd_create_iosq(struct ntasi_ans_sqe *c, uint16_t qid, uint16_t cqid,
                               uint32_t slots, uint64_t prp1)
{
    /* nvme.c:386-396 */
    *c = (struct ntasi_ans_sqe){0};
    c->opcode = NTASI_ANS_ADMIN_CMD_CREATE_SQ;
    c->prp1 = prp1;
    c->cdw10 = (uint32_t)qid | ((slots - 1u) << 16);
    c->cdw11 = NTASI_ANS_QUEUE_CONTIGUOUS | ((uint32_t)cqid << 16);
}

void ntasi_ans_cmd_delete_q(struct ntasi_ans_sqe *c, uint8_t opcode, uint16_t qid)
{
    /* nvme.c:482-494 */
    *c = (struct ntasi_ans_sqe){0};
    c->opcode = opcode;
    c->cdw10 = qid;
}

bool ntasi_ans_cqe_ready(uint16_t cqe_status, uint8_t phase)
{
    /* nvme.c:248: if ((cqe.status & 1) != q->cq_phase) continue; */
    return (uint8_t)(cqe_status & 1u) == phase;
}

uint16_t ntasi_ans_cqe_code(uint16_t cqe_status)
{
    /* nvme.c:282-284: cqe.status >>= 1 */
    return (uint16_t)(cqe_status >> 1);
}

void ntasi_ans_cq_advance(struct ntasi_ans_cq_state *st, uint32_t slots)
{
    /* nvme.c:264-268 */
    st->head += 1u;
    if ((uint32_t)st->head == slots) {
        st->head = 0;
        st->phase ^= 1u;
    }
}

bool ntasi_ans_extent_intersects(const struct ntasi_ans_lba_extent *extents,
                                 uint32_t count,
                                 uint64_t lba,
                                 uint64_t blocks)
{
    uint32_t index;

    if (extents == NULL || blocks == 0u)
        return true; /* Unanswerable, so refuse rather than allow. */
    for (index = 0; index < count; ++index) {
        if (lba + blocks - 1u >= extents[index].start &&
            lba <= extents[index].end)
            return true;
    }
    return false;
}

bool ntasi_ans_largest_free_run(const struct ntasi_ans_lba_extent *extents,
                                uint32_t count,
                                uint64_t first_usable,
                                uint64_t last_usable,
                                uint64_t *run_start,
                                uint64_t *run_end)
{
    uint64_t cursor = first_usable;
    uint64_t best_start = 0;
    uint64_t best_len = 0;
    uint32_t step;

    if (run_start == NULL || run_end == NULL)
        return false;
    *run_start = 0;
    *run_end = 0;
    /*
     * count == 0 refuses rather than reporting the whole range free.  Read
     * literally, "no partitions" does mean "all free" -- but the only caller
     * uses this to pick a block to WRITE on a disk holding a live OS, and a
     * partition walk that produced nothing is far more likely to be a failed
     * walk than an empty disk.  Refusing is the answer that cannot destroy
     * anything.
     */
    if (extents == NULL || count == 0u || last_usable <= first_usable)
        return false;

    /*
     * Sweep a cursor forward, each round jumping to the next extent that could
     * still block it.  Bounded by count + 1 rounds and the cursor only ever
     * increases, so an unordered or self-overlapping table cannot spin here.
     */
    for (step = 0; step <= count; ++step) {
        uint64_t next_start = last_usable + 1u;
        uint64_t next_end = 0;
        uint32_t index;
        bool found = false;

        for (index = 0; index < count; ++index) {
            if (extents[index].end < cursor)
                continue;
            if (!found || extents[index].start < next_start) {
                next_start = extents[index].start;
                next_end = extents[index].end;
                found = true;
            }
        }
        if (!found)
            next_start = last_usable + 1u;
        if (next_start > cursor) {
            uint64_t stop = next_start - 1u > last_usable ? last_usable :
                                                            next_start - 1u;

            if (stop >= cursor && (stop - cursor + 1u) > best_len) {
                best_len = stop - cursor + 1u;
                best_start = cursor;
            }
        }
        if (!found || next_start > last_usable)
            break;
        cursor = next_end >= cursor ? next_end + 1u : cursor + 1u;
        if (cursor > last_usable)
            break;
    }
    if (best_len == 0u)
        return false;
    *run_start = best_start;
    *run_end = best_start + best_len - 1u;
    return true;
}

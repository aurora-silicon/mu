/* SPDX-License-Identifier: MIT */
#include "AppleRtkitRuntimeCoreM5.h"

#include <limits.h>
#include <string.h>

#define MGMT_POWER_STATE_MASK UINT64_C(0xffff)
#define MGMT_IOP_POWER_STATE 6u
#define MGMT_IOP_POWER_STATE_ACK 7u
#define MGMT_START_ENDPOINT 5u
#define MGMT_START_ENDPOINT_FLAG (UINT64_C(1) << 1)
#define MGMT_START_ENDPOINT_SHIFT 32u
#define MGMT_AP_POWER_STATE 0x0bu

#define BUFFER_REQUEST 1u
#define BUFFER_REQUEST_SIZE_SHIFT 44u
#define BUFFER_REQUEST_SIZE_MASK (UINT64_C(0xff) << BUFFER_REQUEST_SIZE_SHIFT)
#define BUFFER_REQUEST_IOVA_MASK ((UINT64_C(1) << 44) - 1u)

#define SYSLOG_INIT 8u
#define SYSLOG_LOG 5u
#define IOREPORT_ACK_A 8u
#define IOREPORT_ACK_B 0x0cu

#define OSLOG_ENDPOINT 8u
#define TRACEKIT_ENDPOINT 0x0au
#define OSLOG_TYPE_SHIFT 56u
#define OSLOG_TYPE_MASK (UINT64_C(0xff) << OSLOG_TYPE_SHIFT)
#define OSLOG_BUFFER_REQUEST 1u
#define OSLOG_SIZE_SHIFT 36u
#define OSLOG_SIZE_MASK (UINT64_C(0xfffff) << OSLOG_SIZE_SHIFT)
#define OSLOG_IOVA_MASK ((UINT64_C(1) << 36) - 1u)

static uint64_t with_type(uint8_t type)
{
    return ntasi_rtkit_with_mgmt_type(0, type);
}

static int send_message(struct ntasi_rtkit_runtime *runtime,
                        uint8_t endpoint, uint64_t payload)
{
    struct ntasi_asc_message message = {
        .payload = payload,
        .endpoint = endpoint,
    };

    if (ntasi_asc_send(runtime->asc, &message) != NTASI_ASC_OK)
        return NTASI_RTKIT_RUNTIME_ERR_TRANSPORT;
    runtime->last_tx_payload = payload;
    runtime->last_tx_endpoint = endpoint;
    runtime->messages_sent++;
    return NTASI_RTKIT_RUNTIME_OK;
}

/*
 * Single point at which an inbound mailbox message is taken.  Both the bounded
 * boot waits and ntasi_rtkit_runtime_service() go through it so the "last
 * message the coprocessor sent" is recorded exactly once and can never drift
 * between the two paths.
 */
static int receive_once(struct ntasi_rtkit_runtime *runtime,
                        struct ntasi_asc_message *message)
{
    int status = ntasi_asc_receive(runtime->asc, message);

    if (status == NTASI_ASC_OK) {
        runtime->last_rx_payload = message->payload;
        runtime->last_rx_endpoint = message->endpoint;
        runtime->messages_received++;
    }
    return status;
}

static int receive_bounded(struct ntasi_rtkit_runtime *runtime,
                           struct ntasi_asc_message *message)
{
    uint32_t attempt;

    runtime->poll_attempts = 0;
    for (attempt = 0; attempt < runtime->poll_limit; ++attempt) {
        int status;

        runtime->poll_attempts = attempt + 1u;
        status = receive_once(runtime, message);
        if (status == NTASI_ASC_OK)
            return NTASI_RTKIT_RUNTIME_OK;
        if (status != NTASI_ASC_NO_MESSAGE)
            return NTASI_RTKIT_RUNTIME_ERR_TRANSPORT;
    }
    return NTASI_RTKIT_RUNTIME_ERR_TIMEOUT;
}

static struct ntasi_rtkit_shared_buffer *buffer_for_endpoint(
    struct ntasi_rtkit_runtime *runtime, uint8_t endpoint)
{
    switch (endpoint) {
    case NTASI_RTKIT_EP_CRASHLOG:
        return &runtime->crashlog;
    case NTASI_RTKIT_EP_SYSLOG:
        return &runtime->syslog;
    case NTASI_RTKIT_EP_IOREPORT:
        return &runtime->ioreport;
    case OSLOG_ENDPOINT:
        return &runtime->oslog;
    default:
        return NULL;
    }
}

static int handle_buffer_request(struct ntasi_rtkit_runtime *runtime,
                                 const struct ntasi_asc_message *message)
{
    struct ntasi_rtkit_shared_buffer *buffer;
    uint64_t requested_address;
    uint64_t pages;
    uint64_t reply;
    size_t size;
    int status;

    buffer = buffer_for_endpoint(runtime, (uint8_t)message->endpoint);
    if (buffer == NULL || runtime->ops.allocate_shared == NULL)
        return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
    if (message->endpoint == OSLOG_ENDPOINT) {
        uint64_t raw_size = (message->payload & OSLOG_SIZE_MASK) >>
                            OSLOG_SIZE_SHIFT;

        requested_address = (message->payload & OSLOG_IOVA_MASK) << 12;
        if (raw_size == 0 || raw_size > SIZE_MAX)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
        size = (size_t)raw_size;
        pages = (raw_size + ((UINT64_C(1) << 12) - 1u)) >> 12;
    } else {
        pages = (message->payload & BUFFER_REQUEST_SIZE_MASK) >>
                BUFFER_REQUEST_SIZE_SHIFT;
        requested_address = message->payload & BUFFER_REQUEST_IOVA_MASK;
        if (pages == 0 || pages > SIZE_MAX >> 12)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
        size = (size_t)pages << 12;
    }
    if (requested_address != 0) {
        /*
         * M5 fork: firmware-owned buffer.  Linux maps it (rtkit-helper
         * shmem_setup against the "sram" resource) and sends no reply.
         */
        if (buffer->cpu_address != NULL) {
            if (message->endpoint == NTASI_RTKIT_EP_CRASHLOG) {
                runtime->crashed = true;
                if (runtime->ops.crashed != NULL)
                    runtime->ops.crashed(runtime->opaque, buffer);
                return NTASI_RTKIT_RUNTIME_ERR_CRASHED;
            }
            return NTASI_RTKIT_RUNTIME_OK;
        }
        if (runtime->ops.map_firmware_buffer == NULL)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
        status = runtime->ops.map_firmware_buffer(
            runtime->opaque, (uint8_t)message->endpoint, requested_address,
            size, buffer);
        if (status != 0 || buffer->cpu_address == NULL ||
            buffer->device_address != requested_address)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
        return NTASI_RTKIT_RUNTIME_OK;
    }

    if (buffer->cpu_address == NULL) {
        status = runtime->ops.allocate_shared(runtime->opaque,
                                               (uint8_t)message->endpoint,
                                               size, buffer);
        if (status != 0 || buffer->cpu_address == NULL ||
            buffer->size < size || buffer->device_address == 0)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
    } else if (message->endpoint == NTASI_RTKIT_EP_CRASHLOG) {
        runtime->crashed = true;
        if (runtime->ops.crashed != NULL)
            runtime->ops.crashed(runtime->opaque, buffer);
        return NTASI_RTKIT_RUNTIME_ERR_CRASHED;
    }

    if (message->endpoint == OSLOG_ENDPOINT) {
        if ((buffer->device_address & ((UINT64_C(1) << 12) - 1u)) != 0 ||
            (buffer->device_address >> 12) > OSLOG_IOVA_MASK)
            return NTASI_RTKIT_RUNTIME_ERR_BUFFER;
        reply = ((uint64_t)OSLOG_BUFFER_REQUEST << OSLOG_TYPE_SHIFT) |
                ((uint64_t)size << OSLOG_SIZE_SHIFT) |
                (buffer->device_address >> 12);
    } else {
        reply = with_type(BUFFER_REQUEST) |
                (pages << BUFFER_REQUEST_SIZE_SHIFT) |
                (buffer->device_address & BUFFER_REQUEST_IOVA_MASK);
    }
    return send_message(runtime, (uint8_t)message->endpoint, reply);
}

int ntasi_rtkit_runtime_service(struct ntasi_rtkit_runtime *runtime,
                                struct ntasi_asc_message *application_message)
{
    struct ntasi_asc_message message;
    uint8_t type;
    int status;

    if (runtime == NULL || runtime->asc == NULL)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    if (runtime->crashed)
        return NTASI_RTKIT_RUNTIME_ERR_CRASHED;

    status = receive_once(runtime, &message);
    if (status == NTASI_ASC_NO_MESSAGE)
        return NTASI_RTKIT_RUNTIME_NO_MESSAGE;
    if (status != NTASI_ASC_OK)
        return NTASI_RTKIT_RUNTIME_ERR_TRANSPORT;
    if (!ntasi_rtkit_ep_valid(message.endpoint))
        return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    if (message.endpoint >= NTASI_RTKIT_SYSTEM_ENDPOINT_LIMIT) {
        if (application_message != NULL)
            *application_message = message;
        return NTASI_RTKIT_RUNTIME_APP_MESSAGE;
    }

    type = ntasi_rtkit_mgmt_type(message.payload);
    switch (message.endpoint) {
    case NTASI_RTKIT_EP_MGMT:
        if (type == MGMT_IOP_POWER_STATE_ACK) {
            runtime->iop_power = (enum ntasi_rtkit_power_state)
                (message.payload & MGMT_POWER_STATE_MASK);
            return NTASI_RTKIT_RUNTIME_OK;
        }
        if (type == MGMT_AP_POWER_STATE) {
            runtime->ap_power = (enum ntasi_rtkit_power_state)
                (message.payload & MGMT_POWER_STATE_MASK);
            return NTASI_RTKIT_RUNTIME_OK;
        }
        /*
         * Anything else on the management endpoint is NOTED, NOT FATAL.
         *
         * Linux's apple_rtkit_management_rx() ends in a default case that only
         * dev_warn()s (drivers/soc/apple/rtkit.c:253-258), and m1n1's
         * rtkit_recv() likewise ignores management types it does not model.
         * Returning ERR_PROTOCOL here made every bounded power wait abort on
         * the first unmodelled message -- i.e. one unknown type could fail a
         * handshake that had already completed.  The counter keeps it visible.
         */
        runtime->unexpected_mgmt_messages++;
        runtime->last_unexpected_mgmt_type = type;
        return NTASI_RTKIT_RUNTIME_OK;
    case NTASI_RTKIT_EP_CRASHLOG:
    case NTASI_RTKIT_EP_SYSLOG:
    case NTASI_RTKIT_EP_IOREPORT:
        if (type == BUFFER_REQUEST)
            return handle_buffer_request(runtime, &message);
        if (message.endpoint == NTASI_RTKIT_EP_SYSLOG && type == SYSLOG_INIT)
            return NTASI_RTKIT_RUNTIME_OK;
        if (message.endpoint == NTASI_RTKIT_EP_SYSLOG && type == SYSLOG_LOG)
            return send_message(runtime, (uint8_t)message.endpoint,
                                message.payload);
        if (message.endpoint == NTASI_RTKIT_EP_IOREPORT &&
            (type == IOREPORT_ACK_A || type == IOREPORT_ACK_B))
            return send_message(runtime, (uint8_t)message.endpoint,
                                message.payload);
        return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    case NTASI_RTKIT_EP_DEBUG:
        return NTASI_RTKIT_RUNTIME_OK;
    case NTASI_RTKIT_EP_OSLOG:
        if (((message.payload & OSLOG_TYPE_MASK) >> OSLOG_TYPE_SHIFT) ==
            OSLOG_BUFFER_REQUEST)
            return handle_buffer_request(runtime, &message);
        return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    case TRACEKIT_ENDPOINT:
        return NTASI_RTKIT_RUNTIME_OK;
    default:
        return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    }
}

int ntasi_rtkit_runtime_init(struct ntasi_rtkit_runtime *runtime,
                             struct ntasi_asc_transport *asc,
                             const struct ntasi_rtkit_runtime_ops *ops,
                             void *opaque,
                             uint32_t poll_limit)
{
    if (runtime == NULL || asc == NULL || ops == NULL || poll_limit == 0)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    *runtime = (struct ntasi_rtkit_runtime){
        .asc = asc,
        .ops = *ops,
        .opaque = opaque,
        .poll_limit = poll_limit,
        .iop_power = NTASI_RTKIT_POWER_OFF,
        .ap_power = NTASI_RTKIT_POWER_OFF,
    };
    return NTASI_RTKIT_RUNTIME_OK;
}

static int start_endpoint(struct ntasi_rtkit_runtime *runtime,
                          uint8_t endpoint)
{
    uint64_t payload = with_type(MGMT_START_ENDPOINT) |
                       MGMT_START_ENDPOINT_FLAG |
                       (uint64_t)endpoint << MGMT_START_ENDPOINT_SHIFT;

    return send_message(runtime, NTASI_RTKIT_EP_MGMT, payload);
}

int ntasi_rtkit_runtime_start_endpoint(
    struct ntasi_rtkit_runtime *runtime, uint8_t endpoint)
{
    if (runtime == NULL || runtime->asc == NULL || !runtime->booted)
        return NTASI_RTKIT_RUNTIME_ERR_STATE;
    if (!ntasi_rtkit_ep_valid(endpoint) ||
        endpoint < NTASI_RTKIT_SYSTEM_ENDPOINT_LIMIT)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    return start_endpoint(runtime, endpoint);
}

int ntasi_rtkit_runtime_send_application(
    struct ntasi_rtkit_runtime *runtime, uint8_t endpoint, uint64_t payload)
{
    if (runtime == NULL || runtime->asc == NULL)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    if (!runtime->booted)
        return NTASI_RTKIT_RUNTIME_ERR_STATE;
    if (!ntasi_rtkit_ep_valid(endpoint) ||
        endpoint < NTASI_RTKIT_SYSTEM_ENDPOINT_LIMIT)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    return send_message(runtime, endpoint, payload);
}

static int wait_for_iop_power(struct ntasi_rtkit_runtime *runtime,
                              enum ntasi_rtkit_power_state target)
{
    uint32_t attempt;

    runtime->poll_attempts = 0;
    for (attempt = 0; attempt < runtime->poll_limit; ++attempt) {
        int status;

        if (runtime->iop_power == target)
            return NTASI_RTKIT_RUNTIME_OK;
        runtime->poll_attempts = attempt + 1u;
        status = ntasi_rtkit_runtime_service(runtime, NULL);
        if (status < 0)
            return status;
        if (status == NTASI_RTKIT_RUNTIME_APP_MESSAGE)
            return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    }
    return NTASI_RTKIT_RUNTIME_ERR_TIMEOUT;
}

static int wait_for_ap_power(struct ntasi_rtkit_runtime *runtime,
                             enum ntasi_rtkit_power_state target)
{
    uint32_t attempt;

    runtime->poll_attempts = 0;
    for (attempt = 0; attempt < runtime->poll_limit; ++attempt) {
        int status;

        if (runtime->ap_power == target)
            return NTASI_RTKIT_RUNTIME_OK;
        runtime->poll_attempts = attempt + 1u;
        status = ntasi_rtkit_runtime_service(runtime, NULL);
        if (status < 0)
            return status;
        if (status == NTASI_RTKIT_RUNTIME_APP_MESSAGE)
            return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    }
    return NTASI_RTKIT_RUNTIME_ERR_TIMEOUT;
}

int ntasi_rtkit_runtime_boot(struct ntasi_rtkit_runtime *runtime)
{
    struct ntasi_asc_message message;
    uint16_t min_version;
    uint16_t max_version;
    uint16_t wanted_version;
    uint32_t epmap_count;
    bool done = false;
    int status;

    if (runtime == NULL || runtime->asc == NULL)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    runtime->booted = false;
    runtime->crashed = false;
    runtime->iop_power = NTASI_RTKIT_POWER_OFF;
    runtime->ap_power = NTASI_RTKIT_POWER_OFF;
    runtime->system_endpoints = 0;
    runtime->optional_endpoints_failed = 0;
    runtime->poll_attempts = 0;
    runtime->epmap_rounds = 0;
    runtime->messages_received = 0;
    runtime->messages_sent = 0;
    runtime->hello_version = 0;
    runtime->last_rx_payload = 0;
    runtime->last_tx_payload = 0;
    runtime->last_rx_endpoint = 0;
    runtime->last_tx_endpoint = 0;
    runtime->ap_power_acked = false;
    runtime->ap_ack_poll_attempts = 0;
    runtime->unexpected_mgmt_messages = 0;
    runtime->last_unexpected_mgmt_type = 0;
    runtime->cpu_control_before_start = 0;
    runtime->cpu_control_written = 0;
    runtime->cpu_control_readback = 0;
    runtime->cpu_status_before_start = 0;
    runtime->cpu_status_after_start = 0;
    runtime->cpu_status_first_change = 0;
    runtime->cpu_status_change_polls = 0;
    runtime->cpu_status_transitions = 0;
    runtime->stale_messages_drained = 0;
    runtime->stale_first_endpoint = 0;
    runtime->stale_first_payload = 0;
    for (unsigned int slot = 0; slot < NTASI_RTKIT_CPU_STATUS_TRACE_SLOTS;
         ++slot) {
        runtime->cpu_status_trace[slot] = 0;
        runtime->cpu_status_trace_polls[slot] = 0;
    }

    if (runtime->boot_mode == NTASI_RTKIT_BOOT_MODE_COLD) {
        /*
         * THE apple_rtkit_reinit() MAILBOX TEARDOWN, in the only form a polled
         * transport can take.
         *
         * Linux brackets its cold path with apple_mbox_stop() /
         * apple_mbox_start() (rtkit.c:741,768) precisely so no message queued
         * before the reset can be mistaken for part of the new handshake.  We
         * have no interrupt to mask, so we read I2A dry HERE -- while the core
         * is still held with its run bit clear, so nothing can be racing us --
         * and count what came out.
         *
         * The count is evidence in its own right.  On a genuinely cold core it
         * MUST be zero; a non-zero drain on the cold path means the
         * coprocessor was alive and talking, which is the one observation that
         * would put the "left running by the previous stage" theory back on
         * the table.
         */
        uint32_t drain;

        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_DRAIN;
        for (drain = 0; drain < NTASI_RTKIT_STALE_DRAIN_LIMIT; ++drain) {
            struct ntasi_asc_message stale;

            /*
             * Deliberately NOT receive_once(): messages_received counts the
             * handshake, and a drained leftover is not part of it.
             */
            if (ntasi_asc_receive(runtime->asc, &stale) != NTASI_ASC_OK)
                break;
            if (runtime->stale_messages_drained == 0) {
                runtime->stale_first_payload = stale.payload;
                runtime->stale_first_endpoint = stale.endpoint;
            }
            runtime->stale_messages_drained++;
        }
    }

    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_CPU_START;
    if (runtime->boot_mode == NTASI_RTKIT_BOOT_MODE_WAKE) {
        /*
         * apple_rtkit_wake() NEVER TOUCHES CPU_CONTROL (rtkit.c:920-942), and
         * neither does the caller that selects it (apple.c:1119-1121).  The
         * register is still SAMPLED so the record says what the live core was
         * doing; cpu_control_written stays 0, which is what distinguishes
         * "we did not write it" from "we wrote 0".
         */
        runtime->cpu_control_before_start =
            runtime->asc->ops.cpu_read32(runtime->asc->opaque,
                                         NTASI_ASC_CPU_CONTROL);
        runtime->cpu_status_before_start = ntasi_asc_cpu_status(runtime->asc);
        runtime->cpu_control_readback = runtime->cpu_control_before_start;
        runtime->cpu_status_after_start = runtime->cpu_status_before_start;
        runtime->cpu_status_first_change = runtime->cpu_status_after_start;
        runtime->cpu_status_change_polls = 0;
    } else {
        /*
         * THE LAST-INSTANT GATE.  Nothing may go between this and the write
         * below -- not a log line, not a telemetry sample -- because the whole
         * point is that the answer is only valid at this instruction boundary.
         * See the ready_to_release comment in the header for the J414s
         * measurement that put it here.
         */
        if (runtime->ops.ready_to_release != NULL &&
            runtime->ops.ready_to_release(runtime->opaque) != 0)
            return NTASI_RTKIT_RUNTIME_ERR_NOT_READY;
        /*
         * RELEASE THE CORE, AND WATCH IT.
         *
         * Everything here except the two writes is diagnostic, and it exists
         * because a coprocessor that accepts the run bit and never executes has
         * exactly three explanations that a single post-mortem sample cannot
         * tell apart: the write did not stick, the core never started, or the
         * core started and stopped again before anything was sampled.
         */
        runtime->cpu_control_before_start =
            runtime->asc->ops.cpu_read32(runtime->asc->opaque,
                                         NTASI_ASC_CPU_CONTROL);
        runtime->cpu_status_before_start = ntasi_asc_cpu_status(runtime->asc);
        if (runtime->cpu_start_exclusive) {
            runtime->cpu_control_written = NTASI_ASC_CPU_CONTROL_START;
            ntasi_asc_cpu_start_exclusive(runtime->asc);
        } else {
            runtime->cpu_control_written =
                runtime->cpu_control_before_start | NTASI_ASC_CPU_CONTROL_START;
            ntasi_asc_cpu_start(runtime->asc);
        }
        runtime->cpu_control_readback =
            runtime->asc->ops.cpu_read32(runtime->asc->opaque,
                                         NTASI_ASC_CPU_CONTROL);
        runtime->cpu_status_after_start = ntasi_asc_cpu_status(runtime->asc);
        /*
         * The caller's one chance to observe device state at the instant after
         * the release.  Diagnostic only: it cannot fail the boot, and it runs
         * before the watch below so it is not separated from the write by 1 ms
         * of polling.
         */
        if (runtime->ops.after_release != NULL)
            runtime->ops.after_release(runtime->opaque);
        runtime->cpu_status_first_change = runtime->cpu_status_after_start;
        runtime->cpu_status_change_polls = 0;
        {
            /*
             * RUN THE WHOLE BUDGET.  This used to break on the first
             * transition, which on J414s is always the STOPPED bit clearing at
             * the release -- so it returned instantly and never saw the IDLE
             * bit clear later.  first_change/change_polls keep their original
             * meaning (the FIRST transition) for ABI compatibility; the trace
             * records the rest.
             */
            uint32_t watch;
            uint32_t last = runtime->cpu_status_after_start;

            for (watch = 0; watch < NTASI_RTKIT_CPU_STATUS_WATCH_POLLS;
                 ++watch) {
                uint32_t status_word = ntasi_asc_cpu_status(runtime->asc);

                if (status_word != last) {
                    if (runtime->cpu_status_transitions == 0) {
                        runtime->cpu_status_first_change = status_word;
                        runtime->cpu_status_change_polls = watch + 1u;
                    }
                    if (runtime->cpu_status_transitions <
                        NTASI_RTKIT_CPU_STATUS_TRACE_SLOTS) {
                        runtime->cpu_status_trace[
                            runtime->cpu_status_transitions] = status_word;
                        runtime->cpu_status_trace_polls[
                            runtime->cpu_status_transitions] = watch + 1u;
                    }
                    /* Counted even once the slots are full, so a trajectory
                     * richer than four steps is still reported as such. */
                    runtime->cpu_status_transitions++;
                    last = status_word;
                }
                if (runtime->asc->ops.service != NULL)
                    runtime->asc->ops.service(runtime->asc->opaque);
            }
        }
    }
    if (runtime->boot_mode != NTASI_RTKIT_BOOT_MODE_COLD) {
        /*
         * SEND NOTHING ON THE COLD PATH.  In RTKit the COPROCESSOR opens the
         * conversation: Linux's cold path (apple.c:1103-1118) releases the
         * core and then calls apple_rtkit_boot(), which transmits nothing at
         * all until after the endpoint map and the IOP power ACK have arrived
         * (rtkit.c:817-838).  Only apple_rtkit_wake() sends
         * SET_IOP_PWR_STATE(INIT), and only to a core that is already running.
         *
         * m1n1 sends it on both paths (src/rtkit.c:501-509) and boots this
         * silicon, so this is not the difference between working and not --
         * but a message the AP puts in the A2I FIFO before HELLO is out of
         * protocol on the COLD path in the only reference that distinguishes
         * the two, and a bring-up that cannot even say which shape it took is
         * one variable too many.
         */
        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_SEND_INIT;
        status = send_message(runtime, NTASI_RTKIT_EP_MGMT,
                              with_type(MGMT_IOP_POWER_STATE) |
                                  NTASI_RTKIT_POWER_INIT);
        if (status != 0)
            return status;
    }
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_WAIT_HELLO;
    status = receive_bounded(runtime, &message);
    if (status != 0)
        return status;
    if (message.endpoint != NTASI_RTKIT_EP_MGMT ||
        ntasi_rtkit_mgmt_type(message.payload) != NTASI_RTKIT_MGMT_HELLO)
        return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
    ntasi_rtkit_hello_parse(message.payload, &min_version, &max_version);
    runtime->hello_version = (uint32_t)min_version |
                             ((uint32_t)max_version << 16);
    if (!ntasi_rtkit_hello_negotiate(min_version, max_version,
                                     &wanted_version))
        return NTASI_RTKIT_RUNTIME_ERR_VERSION;
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_SEND_HELLO_ACK;
    status = send_message(runtime, NTASI_RTKIT_EP_MGMT,
                          ntasi_rtkit_hello_ack(wanted_version));
    if (status != 0)
        return status;

    for (epmap_count = 0; epmap_count < runtime->poll_limit && !done;
         ++epmap_count) {
        uint8_t base;
        uint32_t bitmap;
        unsigned int bit;
        uint64_t reply;

        runtime->epmap_rounds = epmap_count + 1u;
        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_WAIT_EPMAP;
        status = receive_bounded(runtime, &message);
        if (status != 0)
            return status;
        if (message.endpoint != NTASI_RTKIT_EP_MGMT ||
            ntasi_rtkit_mgmt_type(message.payload) != NTASI_RTKIT_MGMT_EPMAP)
            return NTASI_RTKIT_RUNTIME_ERR_PROTOCOL;
        ntasi_rtkit_epmap_parse(message.payload, &base, &bitmap, &done);
        for (bit = 0; bit < 32; ++bit) {
            uint8_t endpoint;

            if ((bitmap & (UINT32_C(1) << bit)) == 0)
                continue;
            endpoint = ntasi_rtkit_epmap_endpoint(base, bit);
            if (endpoint < NTASI_RTKIT_SYSTEM_ENDPOINT_LIMIT)
                runtime->system_endpoints |= UINT32_C(1) << endpoint;
        }
        reply = with_type(NTASI_RTKIT_MGMT_EPMAP) |
                (uint64_t)base << NTASI_RTKIT_EPMAP_BASE_SHIFT;
        reply |= done ? NTASI_RTKIT_EPMAP_DONE : UINT64_C(1);
        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_REPLY_EPMAP;
        status = send_message(runtime, NTASI_RTKIT_EP_MGMT, reply);
        if (status != 0)
            return status;
    }
    if (!done)
        return NTASI_RTKIT_RUNTIME_ERR_TIMEOUT;
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_START_ENDPOINTS;

    {
        /*
         * `required` endpoints abort the boot if they are advertised but
         * refuse to start.  TRACEKIT (0x0a) is deliberately optional: m1n1's
         * rtkit.c, which is hardware-verified to bring ANS up on this exact
         * J414s/T6020 silicon, does not start it at all -- it advertises in
         * the endpoint map, m1n1 logs "unknown system endpoint 0x0a", skips
         * it, and NVMe initialises and reads successfully.  Treating it as
         * required would let an endpoint that is provably unnecessary for
         * storage fail the whole controller start.  It is still attempted, so
         * we keep its telemetry when the firmware does support it.
         */
        static const uint8_t required[] = {
            NTASI_RTKIT_EP_DEBUG,
            NTASI_RTKIT_EP_CRASHLOG,
            NTASI_RTKIT_EP_SYSLOG,
            NTASI_RTKIT_EP_IOREPORT,
            NTASI_RTKIT_EP_OSLOG,
        };
        static const uint8_t optional[] = {
            TRACEKIT_ENDPOINT,
        };
        size_t index;

        for (index = 0; index < sizeof(required); ++index) {
            uint8_t endpoint = required[index];

            if ((runtime->system_endpoints &
                 (UINT32_C(1) << endpoint)) == 0)
                continue;
            status = start_endpoint(runtime, endpoint);
            if (status != 0)
                return status;
        }
        for (index = 0; index < sizeof(optional); ++index) {
            uint8_t endpoint = optional[index];

            if ((runtime->system_endpoints &
                 (UINT32_C(1) << endpoint)) == 0)
                continue;
            if (start_endpoint(runtime, endpoint) != 0)
                runtime->optional_endpoints_failed |=
                    UINT32_C(1) << endpoint;
        }
    }

    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_WAIT_IOP_POWER;
    status = wait_for_iop_power(runtime, NTASI_RTKIT_POWER_ON);
    if (status != 0)
        return status;
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_SEND_AP_POWER;
    status = send_message(runtime, NTASI_RTKIT_EP_MGMT,
                          with_type(MGMT_AP_POWER_STATE) |
                              NTASI_RTKIT_POWER_ON);
    if (status != 0)
        return status;

    /*
     * WAIT FOR THE AP POWER ACK -- bounded, and deliberately NOT fatal.
     *
     * Linux does wait: apple_rtkit_boot() ends in
     * apple_rtkit_set_ap_power_state(rtk, PWR_STATE_ON)
     * (drivers/soc/apple/rtkit.c:838), which blocks on ap_pwr_ack_completion
     * and then verifies rtk->ap_power_state == state (rtkit.c:785-791).
     * m1n1's plain rtkit_boot() does not (src/rtkit.c:820-826).
     *
     * Not waiting at all was wrong for one concrete reason: it left
     * runtime->ap_power at the POWER_OFF assigned on entry FOREVER, including
     * on a completely successful boot -- so the telemetry field could never
     * distinguish "the AP side is on" from "we never asked". Failing on it
     * would have been wrong too, because m1n1 boots this exact silicon
     * without the wait, so a firmware that answers late must not lose the
     * controller. Hence: measure, record, continue.
     *
     * poll_attempts is left untouched. It belongs to whichever bounded wait
     * FAILED, and this one cannot fail; a boot that timed out at WAIT_HELLO
     * must not have its budget evidence overwritten by a later success.
     */
    {
        uint32_t attempt;

        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_WAIT_AP_POWER;
        for (attempt = 0; attempt < runtime->poll_limit; ++attempt) {
            int service_status;

            if (runtime->ap_power == NTASI_RTKIT_POWER_ON) {
                runtime->ap_power_acked = true;
                break;
            }
            runtime->ap_ack_poll_attempts = attempt + 1u;
            service_status = ntasi_rtkit_runtime_service(runtime, NULL);
            /*
             * A transport failure or a firmware crash during this window is
             * still fatal: those are not "the ACK is late", they are the
             * coprocessor going away, and continuing would publish a booted
             * runtime over dead hardware.
             */
            if (service_status == NTASI_RTKIT_RUNTIME_ERR_TRANSPORT ||
                service_status == NTASI_RTKIT_RUNTIME_ERR_CRASHED)
                return service_status;
        }
        if (runtime->ap_power == NTASI_RTKIT_POWER_ON)
            runtime->ap_power_acked = true;
    }

    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_DONE;
    runtime->booted = true;
    return NTASI_RTKIT_RUNTIME_OK;
}

static int switch_power_state(struct ntasi_rtkit_runtime *runtime,
                              enum ntasi_rtkit_power_state target)
{
    int status;

    if (runtime == NULL || runtime->asc == NULL || !runtime->booted)
        return NTASI_RTKIT_RUNTIME_ERR_ARGUMENT;
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_SLEEP;
    status = send_message(runtime, NTASI_RTKIT_EP_MGMT,
                          with_type(MGMT_AP_POWER_STATE) |
                              NTASI_RTKIT_POWER_QUIESCED);
    if (status != 0)
        return status;
    status = wait_for_ap_power(runtime, NTASI_RTKIT_POWER_QUIESCED);
    if (status != 0)
        return status;
    status = send_message(runtime, NTASI_RTKIT_EP_MGMT,
                          with_type(MGMT_IOP_POWER_STATE) |
                              target);
    if (status != 0)
        return status;
    status = wait_for_iop_power(runtime, target);
    if (status != 0)
        return status;
    return NTASI_RTKIT_RUNTIME_OK;
}

int ntasi_rtkit_runtime_quiesce(struct ntasi_rtkit_runtime *runtime)
{
    int status = switch_power_state(runtime, NTASI_RTKIT_POWER_QUIESCED);

    if (status == NTASI_RTKIT_RUNTIME_OK) {
        runtime->booted = false;
        runtime->boot_step = NTASI_RTKIT_BOOT_STEP_IDLE;
    }
    return status;
}

int ntasi_rtkit_runtime_sleep(struct ntasi_rtkit_runtime *runtime)
{
    int status = switch_power_state(runtime, NTASI_RTKIT_POWER_SLEEP);

    if (status != NTASI_RTKIT_RUNTIME_OK)
        return status;
    ntasi_asc_cpu_stop(runtime->asc);
    runtime->booted = false;
    runtime->boot_step = NTASI_RTKIT_BOOT_STEP_IDLE;
    return NTASI_RTKIT_RUNTIME_OK;
}

void ntasi_rtkit_runtime_release_buffers(struct ntasi_rtkit_runtime *runtime)
{
    struct ntasi_rtkit_shared_buffer *buffers[4];
    const uint8_t endpoints[] = {
        NTASI_RTKIT_EP_CRASHLOG,
        NTASI_RTKIT_EP_SYSLOG,
        NTASI_RTKIT_EP_IOREPORT,
        NTASI_RTKIT_EP_OSLOG,
    };
    size_t index;

    if (runtime == NULL || runtime->ops.release_shared == NULL)
        return;
    buffers[0] = &runtime->crashlog;
    buffers[1] = &runtime->syslog;
    buffers[2] = &runtime->ioreport;
    buffers[3] = &runtime->oslog;
    for (index = 0; index < sizeof(buffers) / sizeof(buffers[0]); ++index) {
        if (buffers[index]->cpu_address == NULL)
            continue;
        runtime->ops.release_shared(runtime->opaque, endpoints[index],
                                    buffers[index]);
        *buffers[index] = (struct ntasi_rtkit_shared_buffer){0};
    }
}

/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Portable, bounded bootstrap model for Apple DockChannel HID interfaces.
 *
 * The protocol is derived from Asahi Linux dockchannel-hid at commit
 * e8efe09d4f378992c890d181d65e2ed8d8cb1194.  No firmware bytes or platform
 * GPIO addresses are embedded here.
 */

#pragma once

#include "AppleMtpProtocol.h"

#include <stddef.h>
#include <stdint.h>

#define APPLE_MTP_FIRMWARE_MAGIC              UINT32_C(0x46444948)
#define APPLE_MTP_FIRMWARE_VERSION            UINT32_C(1)
#define APPLE_MTP_FIRMWARE_HEADER_SIZE        20U
#define APPLE_MTP_FIRMWARE_MAX_HEADER_SIZE    4096U
#define APPLE_MTP_FIRMWARE_MAX_DATA_SIZE      (1024U * 1024U)
#define APPLE_MTP_FIRMWARE_MAX_CONTAINER_SIZE \
    (APPLE_MTP_FIRMWARE_MAX_HEADER_SIZE + APPLE_MTP_FIRMWARE_MAX_DATA_SIZE)

#define APPLE_MTP_GPIO_CONNECTION_AFE_RESET 0U
#define APPLE_MTP_GPIO_CONNECTION_STM_RESET 1U

typedef enum _APPLE_MTP_BOOTSTRAP_STATUS {
    AppleMtpBootstrapSuccess = 0,
    AppleMtpBootstrapInvalidArgument,
    AppleMtpBootstrapInvalidState,
    AppleMtpBootstrapInvalidGpio,
    AppleMtpBootstrapMissingResource,
    AppleMtpBootstrapInvalidFirmware,
    AppleMtpBootstrapBufferTooSmall,
    AppleMtpBootstrapDeviceError
} APPLE_MTP_BOOTSTRAP_STATUS;

typedef enum _APPLE_MTP_BOOTSTRAP_STATE {
    AppleMtpBootstrapUninitialized = 0,
    AppleMtpBootstrapNeedResources,
    AppleMtpBootstrapNeedEnable,
    AppleMtpBootstrapWaitEnable,
    AppleMtpBootstrapNeedFirmware,
    AppleMtpBootstrapWaitFirmware,
    /* Method 1 uses the Will states only; method 2 also uses the Has states. */
    AppleMtpBootstrapNeedResetAssertWill,
    AppleMtpBootstrapWaitResetAssertWill,
    AppleMtpBootstrapNeedResetAssertHas,
    AppleMtpBootstrapWaitResetAssertHas,
    AppleMtpBootstrapNeedResetReleaseWill,
    AppleMtpBootstrapWaitResetReleaseWill,
    AppleMtpBootstrapNeedResetReleaseHas,
    AppleMtpBootstrapWaitResetReleaseHas,
    AppleMtpBootstrapWaitReady,
    AppleMtpBootstrapReady,
    AppleMtpBootstrapFailed
} APPLE_MTP_BOOTSTRAP_STATE;

typedef enum _APPLE_MTP_BOOTSTRAP_COMMAND {
    AppleMtpBootstrapCommandNone = 0,
    AppleMtpBootstrapCommandEnable,
    AppleMtpBootstrapCommandFirmware,
    AppleMtpBootstrapCommandResetAssertWill,
    AppleMtpBootstrapCommandResetAssertHas,
    AppleMtpBootstrapCommandResetReleaseWill,
    AppleMtpBootstrapCommandResetReleaseHas
} APPLE_MTP_BOOTSTRAP_COMMAND;

/*
 * Delay between WillChange and HasChanged for the transition into state 2.
 * The interface's enable sequence specifies 50 ms, and the firmware only
 * accepts HasChanged once the hardware has actually settled.
 */
#define APPLE_MTP_POWER_SETTLE_MS 50U

typedef struct _APPLE_MTP_GPIO_REQUIREMENT {
    int Present;
    uint16_t Unknown;
    uint16_t Id;
    uint8_t Name[APPLE_MTP_GPIO_NAME_SIZE];
    size_t NameLength;
} APPLE_MTP_GPIO_REQUIREMENT;

typedef struct _APPLE_MTP_FIRMWARE_VIEW {
    const uint8_t *Data;
    size_t DataLength;
    uint32_t InterfaceOffset;
} APPLE_MTP_FIRMWARE_VIEW;

typedef struct _APPLE_MTP_BOOTSTRAP_CONTEXT {
    APPLE_MTP_BOOTSTRAP_STATE State;
    APPLE_MTP_INTERFACE_KIND InterfaceKind;
    APPLE_MTP_GPIO_REQUIREMENT Gpio;
    uint8_t Interface;
    uint8_t PowerMethod;
    int RequiresFirmware;
    int ReadyObserved;
} APPLE_MTP_BOOTSTRAP_CONTEXT;

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapInitialize(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_INTERFACE_KIND InterfaceKind,
    uint8_t Interface
    );

/*
 * Re-arm an interrupted or failed interface after the parent device has been
 * stopped and started.  A completed interface remains complete; every other
 * state returns to resource acquisition while retaining immutable INIT data.
 */
APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapRestart(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpRecordGpioRequirement(
    APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    const APPLE_MTP_INIT_BLOCK_VIEW *Block
    );

/*
 * Resolve the firmware-requested GPIO name to the ordered ACPI connection.
 * The INIT Unknown/Id fields are protocol tokens echoed in the ACK, not stable
 * board identifiers; current Asahi likewise resolves only the exact name.
 */
APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpGpioConnectionIndex(
    const APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    uint32_t *ConnectionIndex
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapRecordGpio(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    const APPLE_MTP_INIT_BLOCK_VIEW *Block
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapResourcesReady(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    int GpioAvailable,
    int FirmwareAvailable
    );

APPLE_MTP_BOOTSTRAP_COMMAND
AppleMtpBootstrapNextCommand(
    const APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapCommandSent(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_BOOTSTRAP_COMMAND Command
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapCommandComplete(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_BOOTSTRAP_COMMAND Command,
    uint32_t ReturnCode
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapObserveReady(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    uint8_t Interface
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpGpioRequirementMatches(
    const APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    uint8_t Interface,
    const APPLE_MTP_GPIO_EVENT_VIEW *Event
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapMatchGpioEvent(
    const APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    const APPLE_MTP_GPIO_EVENT_VIEW *Event
    );

void
AppleMtpBootstrapFail(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpParseFirmwareContainer(
    const uint8_t *Container,
    size_t ContainerLength,
    APPLE_MTP_FIRMWARE_VIEW *View
    );

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpPrepareFirmwarePayload(
    const uint8_t *Container,
    size_t ContainerLength,
    uint8_t Interface,
    uint8_t *Output,
    size_t OutputCapacity,
    size_t *OutputLength
    );

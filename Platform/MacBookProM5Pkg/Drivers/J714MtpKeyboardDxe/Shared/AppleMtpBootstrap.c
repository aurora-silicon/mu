/* SPDX-License-Identifier: GPL-2.0-only OR MIT */

#include "AppleMtpBootstrap.h"

#include <string.h>

static uint32_t
AppleMtpBootstrapReadLe32(
    const uint8_t *Data
    )
{
    return (uint32_t)Data[0] |
           ((uint32_t)Data[1] << 8) |
           ((uint32_t)Data[2] << 16) |
           ((uint32_t)Data[3] << 24);
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapInitialize(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_INTERFACE_KIND InterfaceKind,
    uint8_t Interface
    )
{
    if (Context == NULL || Interface == APPLE_MTP_INTERFACE_COMM ||
        Interface >= APPLE_MTP_MAX_INTERFACES ||
        (InterfaceKind != AppleMtpInterfaceKeyboard &&
         InterfaceKind != AppleMtpInterfaceMultiTouch &&
         InterfaceKind != AppleMtpInterfaceStm &&
         InterfaceKind != AppleMtpInterfaceActuator))
        return AppleMtpBootstrapInvalidArgument;

    memset(Context, 0, sizeof(*Context));
    Context->State = AppleMtpBootstrapNeedResources;
    Context->InterfaceKind = InterfaceKind;
    Context->Interface = Interface;
    Context->PowerMethod = APPLE_MTP_POWER_METHOD_TRANSACTIONAL;
    Context->RequiresFirmware =
        InterfaceKind == AppleMtpInterfaceMultiTouch;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapRestart(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    )
{
    APPLE_MTP_GPIO_REQUIREMENT gpio;
    APPLE_MTP_INTERFACE_KIND interfaceKind;
    uint8_t interface;
    int readyObserved;
    APPLE_MTP_BOOTSTRAP_STATUS status;

    if (Context == NULL ||
        Context->State == AppleMtpBootstrapUninitialized)
        return AppleMtpBootstrapInvalidArgument;
    if (Context->State == AppleMtpBootstrapReady)
        return AppleMtpBootstrapSuccess;

    gpio = Context->Gpio;
    interfaceKind = Context->InterfaceKind;
    interface = Context->Interface;
    readyObserved = Context->ReadyObserved;
    status = AppleMtpBootstrapInitialize(
        Context, interfaceKind, interface);
    if (status != AppleMtpBootstrapSuccess)
        return status;
    Context->Gpio = gpio;
    Context->ReadyObserved = readyObserved;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpRecordGpioRequirement(
    APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    const APPLE_MTP_INIT_BLOCK_VIEW *Block
    )
{
    if (Requirement == NULL || Block == NULL ||
        Block->Type != APPLE_MTP_INIT_GPIO_REQUEST || Block->String == NULL ||
        Block->StringLength == 0U ||
        Block->StringLength >= APPLE_MTP_GPIO_NAME_SIZE ||
        Block->GpioId == 0U || Block->GpioId > UINT8_MAX ||
        Requirement->Present)
        return AppleMtpBootstrapInvalidGpio;

    Requirement->Present = 1;
    Requirement->Unknown = Block->GpioUnknown;
    Requirement->Id = Block->GpioId;
    Requirement->NameLength = Block->StringLength;
    memcpy(Requirement->Name, Block->String, Block->StringLength);
    Requirement->Name[Block->StringLength] = 0U;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpGpioConnectionIndex(
    const APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    uint32_t *ConnectionIndex
    )
{
    static const uint8_t afeReset[] = "afe-reset";
    static const uint8_t stmReset[] = "stm-reset";

    if (Requirement == NULL || ConnectionIndex == NULL ||
        !Requirement->Present)
        return AppleMtpBootstrapInvalidArgument;
    if (Requirement->NameLength == sizeof(afeReset) - 1U &&
        memcmp(Requirement->Name, afeReset, sizeof(afeReset) - 1U) == 0) {
        *ConnectionIndex = APPLE_MTP_GPIO_CONNECTION_AFE_RESET;
        return AppleMtpBootstrapSuccess;
    }
    if (Requirement->NameLength == sizeof(stmReset) - 1U &&
        memcmp(Requirement->Name, stmReset, sizeof(stmReset) - 1U) == 0) {
        *ConnectionIndex = APPLE_MTP_GPIO_CONNECTION_STM_RESET;
        return AppleMtpBootstrapSuccess;
    }
    return AppleMtpBootstrapInvalidGpio;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapRecordGpio(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    const APPLE_MTP_INIT_BLOCK_VIEW *Block
    )
{
    APPLE_MTP_BOOTSTRAP_STATUS status;

    if (Context == NULL ||
        Context->State != AppleMtpBootstrapNeedResources)
        return AppleMtpBootstrapInvalidState;
    status = AppleMtpRecordGpioRequirement(&Context->Gpio, Block);
    if (status != AppleMtpBootstrapSuccess)
        Context->State = AppleMtpBootstrapFailed;
    return status;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapResourcesReady(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    int GpioAvailable,
    int FirmwareAvailable
    )
{
    if (Context == NULL ||
        Context->State != AppleMtpBootstrapNeedResources)
        return AppleMtpBootstrapInvalidState;
    if ((Context->Gpio.Present && !GpioAvailable) ||
        (Context->RequiresFirmware && !FirmwareAvailable)) {
        Context->State = AppleMtpBootstrapFailed;
        return AppleMtpBootstrapMissingResource;
    }
    /* J414s advertises its reset GPIO in INIT and uses Asahi's method 1. */
    Context->PowerMethod = Context->Gpio.Present ?
        APPLE_MTP_POWER_METHOD_DIRECT :
        APPLE_MTP_POWER_METHOD_TRANSACTIONAL;
    Context->State = AppleMtpBootstrapNeedEnable;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_COMMAND
AppleMtpBootstrapNextCommand(
    const APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    )
{
    if (Context == NULL)
        return AppleMtpBootstrapCommandNone;
    switch (Context->State) {
    case AppleMtpBootstrapNeedEnable:
        return AppleMtpBootstrapCommandEnable;
    case AppleMtpBootstrapNeedFirmware:
        return AppleMtpBootstrapCommandFirmware;
    case AppleMtpBootstrapNeedResetAssertWill:
        return AppleMtpBootstrapCommandResetAssertWill;
    case AppleMtpBootstrapNeedResetAssertHas:
        return AppleMtpBootstrapCommandResetAssertHas;
    case AppleMtpBootstrapNeedResetReleaseWill:
        return AppleMtpBootstrapCommandResetReleaseWill;
    case AppleMtpBootstrapNeedResetReleaseHas:
        return AppleMtpBootstrapCommandResetReleaseHas;
    default:
        return AppleMtpBootstrapCommandNone;
    }
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapCommandSent(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_BOOTSTRAP_COMMAND Command
    )
{
    if (Context == NULL || Command != AppleMtpBootstrapNextCommand(Context))
        return AppleMtpBootstrapInvalidState;
    switch (Command) {
    case AppleMtpBootstrapCommandEnable:
        Context->State = AppleMtpBootstrapWaitEnable;
        break;
    case AppleMtpBootstrapCommandFirmware:
        Context->State = AppleMtpBootstrapWaitFirmware;
        break;
    case AppleMtpBootstrapCommandResetAssertWill:
        Context->State = AppleMtpBootstrapWaitResetAssertWill;
        break;
    case AppleMtpBootstrapCommandResetAssertHas:
        Context->State = AppleMtpBootstrapWaitResetAssertHas;
        break;
    case AppleMtpBootstrapCommandResetReleaseWill:
        Context->State = AppleMtpBootstrapWaitResetReleaseWill;
        break;
    case AppleMtpBootstrapCommandResetReleaseHas:
        Context->State = AppleMtpBootstrapWaitResetReleaseHas;
        break;
    default:
        return AppleMtpBootstrapInvalidState;
    }
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapCommandComplete(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    APPLE_MTP_BOOTSTRAP_COMMAND Command,
    uint32_t ReturnCode
    )
{
    APPLE_MTP_BOOTSTRAP_STATE expected;

    if (Context == NULL)
        return AppleMtpBootstrapInvalidArgument;
    switch (Command) {
    case AppleMtpBootstrapCommandEnable:
        expected = AppleMtpBootstrapWaitEnable;
        break;
    case AppleMtpBootstrapCommandFirmware:
        expected = AppleMtpBootstrapWaitFirmware;
        break;
    case AppleMtpBootstrapCommandResetAssertWill:
        expected = AppleMtpBootstrapWaitResetAssertWill;
        break;
    case AppleMtpBootstrapCommandResetAssertHas:
        expected = AppleMtpBootstrapWaitResetAssertHas;
        break;
    case AppleMtpBootstrapCommandResetReleaseWill:
        expected = AppleMtpBootstrapWaitResetReleaseWill;
        break;
    case AppleMtpBootstrapCommandResetReleaseHas:
        expected = AppleMtpBootstrapWaitResetReleaseHas;
        break;
    default:
        return AppleMtpBootstrapInvalidState;
    }
    if (Context->State != expected)
        return AppleMtpBootstrapInvalidState;
    if (ReturnCode != 0U) {
        Context->State = AppleMtpBootstrapFailed;
        return AppleMtpBootstrapDeviceError;
    }

    switch (Command) {
    case AppleMtpBootstrapCommandEnable:
        Context->State = Context->RequiresFirmware ?
            AppleMtpBootstrapNeedFirmware : AppleMtpBootstrapReady;
        break;
    case AppleMtpBootstrapCommandFirmware:
        Context->State = AppleMtpBootstrapNeedResetAssertWill;
        break;
    case AppleMtpBootstrapCommandResetAssertWill:
        Context->State = Context->PowerMethod ==
            APPLE_MTP_POWER_METHOD_DIRECT ?
            AppleMtpBootstrapNeedResetReleaseWill :
            AppleMtpBootstrapNeedResetAssertHas;
        break;
    case AppleMtpBootstrapCommandResetAssertHas:
        Context->State = AppleMtpBootstrapNeedResetReleaseWill;
        break;
    case AppleMtpBootstrapCommandResetReleaseWill:
        Context->State = Context->PowerMethod ==
            APPLE_MTP_POWER_METHOD_DIRECT ?
            (Context->ReadyObserved ? AppleMtpBootstrapReady :
                                      AppleMtpBootstrapWaitReady) :
            AppleMtpBootstrapNeedResetReleaseHas;
        break;
    case AppleMtpBootstrapCommandResetReleaseHas:
        Context->State = Context->ReadyObserved ?
            AppleMtpBootstrapReady : AppleMtpBootstrapWaitReady;
        break;
    default:
        return AppleMtpBootstrapInvalidState;
    }
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapObserveReady(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    uint8_t Interface
    )
{
    if (Context == NULL || Interface != Context->Interface)
        return AppleMtpBootstrapInvalidArgument;
    if (Context->State == AppleMtpBootstrapFailed ||
        Context->State == AppleMtpBootstrapUninitialized)
        return AppleMtpBootstrapInvalidState;
    Context->ReadyObserved = 1;
    if (Context->State == AppleMtpBootstrapWaitReady)
        Context->State = AppleMtpBootstrapReady;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpGpioRequirementMatches(
    const APPLE_MTP_GPIO_REQUIREMENT *Requirement,
    uint8_t Interface,
    const APPLE_MTP_GPIO_EVENT_VIEW *Event
    )
{
    if (Requirement == NULL || Event == NULL)
        return AppleMtpBootstrapInvalidArgument;
    if (!Requirement->Present || Event->Interface != Interface ||
        Event->Command != 3U || Event->Gpio != (uint8_t)Requirement->Id)
        return AppleMtpBootstrapInvalidGpio;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpBootstrapMatchGpioEvent(
    const APPLE_MTP_BOOTSTRAP_CONTEXT *Context,
    const APPLE_MTP_GPIO_EVENT_VIEW *Event
    )
{
    if (Context == NULL)
        return AppleMtpBootstrapInvalidArgument;
    if (Context->State == AppleMtpBootstrapFailed)
        return AppleMtpBootstrapInvalidGpio;
    return AppleMtpGpioRequirementMatches(
        &Context->Gpio, Context->Interface, Event);
}

void
AppleMtpBootstrapFail(
    APPLE_MTP_BOOTSTRAP_CONTEXT *Context
    )
{
    if (Context != NULL)
        Context->State = AppleMtpBootstrapFailed;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpParseFirmwareContainer(
    const uint8_t *Container,
    size_t ContainerLength,
    APPLE_MTP_FIRMWARE_VIEW *View
    )
{
    uint32_t headerLength;
    uint32_t dataLength;
    uint32_t interfaceOffset;

    if (Container == NULL || View == NULL)
        return AppleMtpBootstrapInvalidArgument;
    memset(View, 0, sizeof(*View));
    if (ContainerLength < APPLE_MTP_FIRMWARE_HEADER_SIZE ||
        ContainerLength > APPLE_MTP_FIRMWARE_MAX_CONTAINER_SIZE)
        return AppleMtpBootstrapInvalidFirmware;
    if (AppleMtpBootstrapReadLe32(Container) != APPLE_MTP_FIRMWARE_MAGIC ||
        AppleMtpBootstrapReadLe32(Container + 4U) !=
            APPLE_MTP_FIRMWARE_VERSION)
        return AppleMtpBootstrapInvalidFirmware;

    headerLength = AppleMtpBootstrapReadLe32(Container + 8U);
    dataLength = AppleMtpBootstrapReadLe32(Container + 12U);
    interfaceOffset = AppleMtpBootstrapReadLe32(Container + 16U);
    if (headerLength < APPLE_MTP_FIRMWARE_HEADER_SIZE ||
        headerLength > APPLE_MTP_FIRMWARE_MAX_HEADER_SIZE ||
        (size_t)headerLength > ContainerLength || dataLength == 0U ||
        dataLength > APPLE_MTP_FIRMWARE_MAX_DATA_SIZE ||
        (size_t)dataLength > ContainerLength - (size_t)headerLength ||
        interfaceOffset >= dataLength)
        return AppleMtpBootstrapInvalidFirmware;

    View->Data = Container + headerLength;
    View->DataLength = dataLength;
    View->InterfaceOffset = interfaceOffset;
    return AppleMtpBootstrapSuccess;
}

APPLE_MTP_BOOTSTRAP_STATUS
AppleMtpPrepareFirmwarePayload(
    const uint8_t *Container,
    size_t ContainerLength,
    uint8_t Interface,
    uint8_t *Output,
    size_t OutputCapacity,
    size_t *OutputLength
    )
{
    APPLE_MTP_FIRMWARE_VIEW view;
    APPLE_MTP_BOOTSTRAP_STATUS status;

    if (Output == NULL || OutputLength == NULL ||
        Interface == APPLE_MTP_INTERFACE_COMM ||
        Interface >= APPLE_MTP_MAX_INTERFACES)
        return AppleMtpBootstrapInvalidArgument;
    *OutputLength = 0U;
    status = AppleMtpParseFirmwareContainer(
        Container, ContainerLength, &view);
    if (status != AppleMtpBootstrapSuccess)
        return status;
    if (view.DataLength > OutputCapacity)
        return AppleMtpBootstrapBufferTooSmall;

    memcpy(Output, view.Data, view.DataLength);
    if (view.InterfaceOffset != 0U)
        Output[view.InterfaceOffset] = Interface;
    *OutputLength = view.DataLength;
    return AppleMtpBootstrapSuccess;
}

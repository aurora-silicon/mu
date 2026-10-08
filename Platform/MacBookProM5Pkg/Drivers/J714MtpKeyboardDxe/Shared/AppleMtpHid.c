/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* MTP HID payload extraction with optional descriptor-selected report IDs. */

#include "AppleMtpHid.h"

#include <string.h>

#define HID_ITEM_LONG_PREFIX             0xfeU
#define HID_ITEM_TYPE_GLOBAL             1U
#define HID_GLOBAL_ITEM_REPORT_ID_TAG    8U

APPLE_MTP_STATUS
AppleMtpHidDescriptorUsesReportIds(
    const uint8_t *Descriptor,
    size_t DescriptorLength,
    int *UsesReportIds
    )
{
    size_t offset = 0U;

    if (Descriptor == NULL || UsesReportIds == NULL || DescriptorLength == 0U)
        return AppleMtpInvalidArgument;
    *UsesReportIds = 0;

    while (offset < DescriptorLength) {
        uint8_t prefix = Descriptor[offset++];
        size_t dataLength;
        unsigned int type;
        unsigned int tag;

        if (prefix == HID_ITEM_LONG_PREFIX) {
            if (DescriptorLength - offset < 2U)
                return AppleMtpMalformedMessage;
            dataLength = Descriptor[offset];
            offset += 2U;
            if (dataLength > DescriptorLength - offset)
                return AppleMtpMalformedMessage;
            offset += dataLength;
            continue;
        }

        switch (prefix & 3U) {
        case 0U:
            dataLength = 0U;
            break;
        case 1U:
            dataLength = 1U;
            break;
        case 2U:
            dataLength = 2U;
            break;
        default:
            dataLength = 4U;
            break;
        }
        if (dataLength > DescriptorLength - offset)
            return AppleMtpMalformedMessage;

        type = (unsigned int)((prefix >> 2) & 3U);
        tag = (unsigned int)((prefix >> 4) & 0x0fU);
        if (type == HID_ITEM_TYPE_GLOBAL &&
            tag == HID_GLOBAL_ITEM_REPORT_ID_TAG) {
            if (dataLength != 1U || Descriptor[offset] == 0U)
                return AppleMtpMalformedMessage;
            *UsesReportIds = 1;
        }
        offset += dataLength;
    }

    return AppleMtpSuccess;
}

static APPLE_MTP_STATUS
AppleMtpExtractEndpointReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_ENDPOINT_KIND EndpointKind,
    APPLE_MTP_HID_REPORT_VIEW *View
    )
{
    APPLE_MTP_MESSAGE_VIEW message;
    APPLE_MTP_HID_REPORT_VIEW result;
    APPLE_MTP_STATUS status;

    if (Packet == NULL || View == NULL)
        return AppleMtpInvalidArgument;
    memset(View, 0, sizeof(*View));
    if (ExpectedInterface == APPLE_MTP_INTERFACE_COMM ||
        ExpectedInterface >= APPLE_MTP_MAX_INTERFACES)
        return AppleMtpInvalidInterface;
    if (UsesReportIds != 0 && UsesReportIds != 1)
        return AppleMtpInvalidArgument;
    if (MaximumReportLength == 0U)
        return AppleMtpInvalidLength;
    if (EndpointKind != AppleMtpHidKeyboard &&
        EndpointKind != AppleMtpHidMultiTouch &&
        EndpointKind != AppleMtpHidActuator)
        return AppleMtpInvalidArgument;

    status = AppleMtpParseMessage(Packet, &message);
    if (status != AppleMtpSuccess)
        return status;
    if (Packet->Channel != APPLE_DOCKCHANNEL_CHANNEL_REPORT)
        return AppleMtpInvalidChannel;
    if (Packet->Interface != ExpectedInterface)
        return AppleMtpInvalidInterface;
    if (message.ReportType != AppleMtpInputReport ||
        message.RequestType != AppleMtpSetReport)
        return AppleMtpInvalidFlags;
    if (message.PayloadLength == 0U ||
        (size_t)message.PayloadLength > MaximumReportLength)
        return AppleMtpInvalidLength;

    memset(&result, 0, sizeof(result));
    result.EndpointKind = EndpointKind;
    result.Interface = ExpectedInterface;
    result.UsesReportIds = UsesReportIds;
    result.WireReport = message.Payload;
    result.WireReportLength = message.PayloadLength;
    if (UsesReportIds) {
        if (message.Payload[0] == 0U)
            return AppleMtpMalformedMessage;
        result.ReportId = message.Payload[0];
        result.ReportPayload = message.Payload + 1;
        result.ReportPayloadLength = (size_t)message.PayloadLength - 1U;
    } else {
        result.ReportPayload = message.Payload;
        result.ReportPayloadLength = message.PayloadLength;
    }
    *View = result;
    return AppleMtpSuccess;
}

APPLE_MTP_STATUS
AppleMtpExtractKeyboardReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    )
{
    return AppleMtpExtractEndpointReport(
        Packet, ExpectedInterface, UsesReportIds, MaximumReportLength,
        AppleMtpHidKeyboard, View);
}

APPLE_MTP_STATUS
AppleMtpExtractMultiTouchReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    )
{
    return AppleMtpExtractEndpointReport(
        Packet, ExpectedInterface, UsesReportIds, MaximumReportLength,
        AppleMtpHidMultiTouch, View);
}

APPLE_MTP_STATUS
AppleMtpExtractActuatorReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    )
{
    return AppleMtpExtractEndpointReport(
        Packet, ExpectedInterface, UsesReportIds, MaximumReportLength,
        AppleMtpHidActuator, View);
}

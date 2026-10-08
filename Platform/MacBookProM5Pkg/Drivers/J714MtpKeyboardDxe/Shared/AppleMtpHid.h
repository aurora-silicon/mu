/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Descriptor-aware raw HID report extraction for Apple MTP endpoints.
 *
 * Apple MTP carries ordinary HID reports; their field layout is defined by
 * the HID descriptor announced in an INIT block. This API deliberately does
 * not invent fixed keyboard or multitouch structures.
 */

#pragma once

#include "AppleMtpProtocol.h"

#include <stddef.h>
#include <stdint.h>

typedef enum _APPLE_MTP_HID_ENDPOINT_KIND {
    AppleMtpHidControl = 0,
    AppleMtpHidKeyboard = 1,
    AppleMtpHidMultiTouch = 2,
    AppleMtpHidActuator = 3
} APPLE_MTP_HID_ENDPOINT_KIND;

typedef struct _APPLE_MTP_HID_REPORT_VIEW {
    APPLE_MTP_HID_ENDPOINT_KIND EndpointKind;
    uint8_t Interface;
    int UsesReportIds;
    uint8_t ReportId;
    const uint8_t *WireReport;
    size_t WireReportLength;
    const uint8_t *ReportPayload;
    size_t ReportPayloadLength;
} APPLE_MTP_HID_REPORT_VIEW;

APPLE_MTP_STATUS
AppleMtpHidDescriptorUsesReportIds(
    const uint8_t *Descriptor,
    size_t DescriptorLength,
    int *UsesReportIds
    );

APPLE_MTP_STATUS
AppleMtpExtractKeyboardReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    );

APPLE_MTP_STATUS
AppleMtpExtractMultiTouchReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    );

APPLE_MTP_STATUS
AppleMtpExtractActuatorReport(
    const APPLE_DOCKCHANNEL_PACKET_VIEW *Packet,
    uint8_t ExpectedInterface,
    int UsesReportIds,
    size_t MaximumReportLength,
    APPLE_MTP_HID_REPORT_VIEW *View
    );

/** @file
  Native Apple MTP (DockChannel) Keyboard + Trackpad input and FPWM Keyboard
  Backlight controller for Aurora UEFI Setup on Apple Silicon (J813 / M5).

  Protocol and register semantics match drivers/AppleMtpHid and
  drivers/AppleKbdBacklight (provenance: m1n1 & Asahi Linux MIT/GPL-2.0).

  Dual-compiled for AArch64 UEFI and host unit tests (AURORA_BIOS_HOST_TEST).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef AURORA_MTP_INPUT_H_
#define AURORA_MTP_INPUT_H_

#include "AuroraBiosConfig.h"

// DockChannel register offsets
#define AURORA_DC_IRQ_MASK          0x0000u
#define AURORA_DC_IRQ_FLAG          0x0004u
#define AURORA_DC_CFG_TX_THRESH     0x0000u
#define AURORA_DC_CFG_RX_THRESH     0x0004u
#define AURORA_DC_DATA_TX8          0x0004u
#define AURORA_DC_DATA_TX32         0x0010u
#define AURORA_DC_DATA_TX_FREE      0x0014u
#define AURORA_DC_DATA_RX8          0x001Cu
#define AURORA_DC_DATA_RX32         0x0028u
#define AURORA_DC_DATA_RX_COUNT     0x002Cu

#define AURORA_DC_FIFO_SIZE         0x0800u
#define AURORA_DC_HDR_SIZE          8u
#define AURORA_DC_CSUM_SIZE         4u
#define AURORA_DC_CHANNEL_CMD       0x11u
#define AURORA_DC_CHANNEL_RPT       0x12u
#define AURORA_DC_MAX_IFACE         16u
#define AURORA_DC_MAX_PACKET        4352u

// MTP subheader & commands
#define AURORA_MTP_SUBHDR_SIZE      8u
#define AURORA_MTP_IFACE_COMM       0u
#define AURORA_MTP_EVENT_GPIO       0xA0u
#define AURORA_MTP_EVENT_INIT       0xF0u
#define AURORA_MTP_EVENT_READY      0xF1u
#define AURORA_MTP_CMD_RESET        0x40u
#define AURORA_MTP_CMD_SEND_FW      0x95u
#define AURORA_MTP_CMD_ENABLE       0xB4u
#define AURORA_MTP_POWER_DIRECT     1u
#define AURORA_MTP_POWER_TRANSACT   2u
#define AURORA_MTP_PHASE_WILL       0u
#define AURORA_MTP_PHASE_HAS        1u
#define AURORA_MTP_POWER_SETTLE_MS  50u

// HIDF Trackpad Firmware Container
#define AURORA_MTP_FW_MAGIC         0x46444948u  // 'HIDF'
#define AURORA_MTP_FW_VERSION       1u
#define AURORA_MTP_FW_HDR_SIZE      20u
#define AURORA_MTP_FW_MAX_HDR       4096u
#define AURORA_MTP_FW_MAX_DATA      (1024u * 1024u)
#define AURORA_MTP_FW_MAX_CONTAINER (AURORA_MTP_FW_MAX_HDR + AURORA_MTP_FW_MAX_DATA)

// J813 verified defaults (cross-checked against live ADT at runtime)
#define AURORA_J813_MTP_STAGING_PA  0x10020000000ULL
#define AURORA_J813_MTP_STAGING_DVA 0x01800000ULL
#define AURORA_J813_MTP_STAGING_SZ  0x00100000ULL

// UEFI -> Windows AppleMtpHid.sys Preboot Handoff Record (at Staging + 0xF8000,
// 28 KiB below the 0xFF000 telemetry page and > 910 KiB above tpmtfw-j813.bin).
#define AURORA_MTP_HANDOFF_OFFSET         0x000F8000u
#define AURORA_MTP_HANDOFF_MAGIC          0x4850544Du  // 'MTPH'
#define AURORA_MTP_HANDOFF_VERSION        1u
#define AURORA_MTP_HANDOFF_MAX_INIT       4096u
#define AURORA_MTP_HANDOFF_FLAG_STM_READY (1u << 0)
#define AURORA_MTP_HANDOFF_FLAG_KB_READY  (1u << 1)
#define AURORA_MTP_HANDOFF_FLAG_MT_READY  (1u << 2)

typedef struct {
  UINT32  Magic;
  UINT32  Version;
  UINT32  Flags;
  UINT32  InitStreamLen;
  UINT8   TxSeq[AURORA_DC_MAX_IFACE];
  UINT32  Crc32;
  UINT32  Reserved;
  UINT8   InitStream[AURORA_MTP_HANDOFF_MAX_INIT];
} AURORA_MTP_PREBOOT_HANDOFF;

// FPWM Keyboard Backlight constants
#define AURORA_KBL_FPWM_CTRL        0x00u
#define AURORA_KBL_FPWM_OFF_CYCLES  0x18u
#define AURORA_KBL_FPWM_ON_CYCLES   0x1Cu
#define AURORA_KBL_FPWM_CTRL_RUN    ((1u << 0) | (1u << 5) | (1u << 14))
#define AURORA_KBL_FPWM_CTRL_STOP   0u
#define AURORA_KBL_CLOCK_HZ         24000000ULL
#define AURORA_KBL_PERIOD_NS        40000u
#define AURORA_KBL_TOTAL_CYCLES     960u

typedef struct {
  UINT8   Channel;
  UINT8   Sequence;
  UINT8   Interface;
  UINT16  BodyLength;
  const UINT8  *Body;
  UINT8   ReportType;     // 0=Input, 1=Output, 2=Feature
  UINT8   RequestType;    // 0=Set, 1=Get
  UINT16  PayloadLength;
  UINT32  ReturnCode;
  const UINT8  *Payload;
} AURORA_MTP_PACKET;

typedef struct {
  UINT8   Buffer[AURORA_DC_MAX_PACKET];
  UINT32  Length;
  UINT32  ExpectedLength;
} AURORA_MTP_STREAM;

typedef struct {
  UINT8   PrevKeys[6];
  UINT8   HeldKey;
  UINT8   HeldModifiers;
  UINT32  HeldStartMs;
  UINT32  LastRepeatMs;
} AURORA_MTP_KB_STATE;

typedef struct {
  BOOLEAN Active;
  INT16   LastX;
  INT16   LastY;
  INT32   RemX;
  INT32   RemY;
  BOOLEAN PhysicalDown;
  // Tap-to-click state
  BOOLEAN TapCandidate;
  UINT8   PrevFingerCount;
  UINT32  TapStartMs;
  INT16   TapStartX;
  INT16   TapStartY;
  BOOLEAN TapClickPulse;
  BOOLEAN TapReleasePending;
} AURORA_MTP_TP_STATE;

typedef struct {
  UINT8   Keys[8];        // Decoded AURORA_BIOS_KEY values
  UINT32  KeyCount;
  BOOLEAN HasPointerEvent;
  INT32   DeltaX;
  INT32   DeltaY;
  BOOLEAN LeftDown;
  BOOLEAN TapPulse;       // Synthetic click press+release
} AURORA_MTP_INPUT_EVENTS;

typedef struct {
  BOOLEAN Present;
  UINT8   Interface;
  BOOLEAN InitComplete;
  BOOLEAN UsesReportIds;
  BOOLEAN HasGpio;
  BOOLEAN Enabled;
  BOOLEAN Ready;
  UINT16  DescLength;
} AURORA_MTP_ENDPOINT_INFO;

typedef struct {
  BOOLEAN                   Initialized;
  BOOLEAN                   Available;
  UINT64                    IrqBase;
  UINT64                    CfgBase;
  UINT64                    DataBase;
  UINT64                    StagingVa;
  UINT64                    StagingDva;
  UINT32                    StagingSize;

  AURORA_MTP_STREAM         Stream;
  UINT8                     TxSeq[AURORA_DC_MAX_IFACE];

  AURORA_MTP_ENDPOINT_INFO  Stm;
  AURORA_MTP_ENDPOINT_INFO  Keyboard;
  AURORA_MTP_ENDPOINT_INFO  MultiTouch;

  // Synchronous command ACK tracking during bootstrap
  BOOLEAN                   AckWaiting;
  UINT8                     AckExpectedIface;
  UINT8                     AckExpectedSeq;
  UINT8                     AckExpectedCmd;
  BOOLEAN                   AckReceived;
  UINT32                    AckReturnCode;
  UINT32                    AckRespLen;
  UINT8                     AckRespData[32];

  AURORA_MTP_KB_STATE       KbState;
  AURORA_MTP_TP_STATE       TpState;

  BOOLEAN                   FwLoaded;
  char                      FwSource[24];
  UINT32                    FwPayloadSize;
  UINT32                    PacketsRx;
  UINT32                    FramingErrors;
  UINT32                    KbReports;
  UINT32                    TpReports;
  UINT32                    TpTouchLogs;

  // Verbatim DockChannel 0xF0 INIT packets captured for OS driver handoff
  UINT32                    CapturedInitLen;
  UINT8                     CapturedInit[AURORA_MTP_HANDOFF_MAX_INIT];
} AURORA_MTP_CONTEXT;

typedef struct {
  BOOLEAN  Available;
  UINT64   PwmBase;
  UINT64   LeafPsAddr;
  UINT64   ParentPsAddr;
  UINT32   CurrentBrightness;
} AURORA_KBL_CONTEXT;

// ---------------------------------------------------------------------------
// Portable Host-Testable Protocol & Decoder Functions
// ---------------------------------------------------------------------------
UINT32
AuroraDcComputeChecksum (
  const UINT8  *Data,
  UINTN        Length
  );

BOOLEAN
AuroraMtpBuildCommandPacket (
  UINT8        Sequence,
  UINT8        Interface,
  UINT8        ReportType,
  UINT8        RequestType,
  const UINT8  *Payload,
  UINTN        PayloadLength,
  UINT8        *OutBuf,
  UINTN        OutCap,
  UINTN        *OutLen
  );

BOOLEAN
AuroraMtpStreamPushByte (
  AURORA_MTP_STREAM  *Stream,
  UINT8              Byte,
  AURORA_MTP_PACKET  *OutPacket,
  BOOLEAN            *OutReady
  );

VOID
AuroraMtpStreamConsume (
  AURORA_MTP_STREAM  *Stream
  );

BOOLEAN
AuroraMtpHidUsesReportIds (
  const UINT8  *Descriptor,
  UINTN        Length,
  BOOLEAN      *UsesReportIds
  );

BOOLEAN
AuroraMtpPrepareFirmware (
  const UINT8  *Container,
  UINTN        ContainerLen,
  UINT8        TargetInterface,
  UINT8        *StagingOut,
  UINTN        StagingCap,
  UINTN        *OutPayloadLen
  );

AURORA_BIOS_KEY
AuroraMtpHidUsageToKey (
  UINT8    Usage,
  BOOLEAN  ShiftHeld
  );

VOID
AuroraMtpDecodeKeyboardReport (
  AURORA_MTP_KB_STATE      *Kb,
  const UINT8              *WireReport,
  UINTN                    WireLen,
  BOOLEAN                  UsesReportIds,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  );

VOID
AuroraMtpTickKeyboardRepeat (
  AURORA_MTP_KB_STATE      *Kb,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  );

BOOLEAN
AuroraMtpDecodeTrackpadReport (
  AURORA_MTP_TP_STATE      *Tp,
  const UINT8              *WireReport,
  UINTN                    WireLen,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  );

VOID
AuroraKblComputeCycles (
  UINT32  Brightness,
  UINT32  *OnCycles,
  UINT32  *OffCycles
  );

BOOLEAN
AuroraMtpBuildPrebootHandoff (
  const AURORA_MTP_CONTEXT    *Ctx,
  AURORA_MTP_PREBOOT_HANDOFF  *OutHandoff
  );

BOOLEAN
AuroraMtpValidatePrebootHandoff (
  const AURORA_MTP_PREBOOT_HANDOFF  *Handoff
  );

// ---------------------------------------------------------------------------
// UEFI Hardware Runtime Entrypoints
// ---------------------------------------------------------------------------
#ifndef AURORA_BIOS_HOST_TEST

BOOLEAN
AuroraMtpHwInit (
  OUT AURORA_MTP_CONTEXT  *Ctx
  );

VOID
AuroraMtpHwPublishHandoff (
  IN OUT AURORA_MTP_CONTEXT  *Ctx
  );

VOID
AuroraMtpHwPoll (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     UINT32              NowMs,
  IN OUT AURORA_BIOS_STATE   *BiosState
  );

BOOLEAN
AuroraKblHwInit (
  OUT AURORA_KBL_CONTEXT  *Kbl,
  IN  UINT32              InitialBrightness
  );

VOID
AuroraKblHwSetBrightness (
  IN OUT AURORA_KBL_CONTEXT  *Kbl,
  IN     UINT32              Brightness
  );

#endif  // !AURORA_BIOS_HOST_TEST

#endif  // AURORA_MTP_INPUT_H_

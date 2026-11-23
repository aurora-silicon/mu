/** @file
  Native Apple MTP (DockChannel) Keyboard + Trackpad input and FPWM Keyboard
  Backlight controller for Aurora UEFI Setup on Apple Silicon (J813 / M5).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AuroraMtpInput.h"
#include "AuroraNtfsReader.h"

#ifdef AURORA_BIOS_HOST_TEST
#include <string.h>
#define MTP_ZERO(P, S)    memset ((P), 0, (S))
#define MTP_COPY(D, S, N) memcpy ((D), (S), (N))
#else
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <IndustryStandard/AuraMtpHandoff.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#define MTP_ZERO(P, S)    ZeroMem ((P), (S))
#define MTP_COPY(D, S, N) CopyMem ((D), (S), (N))
#endif

// ---------------------------------------------------------------------------
// Little-Endian Helpers
// ---------------------------------------------------------------------------
static UINT16
MtpRdLe16 (
  const UINT8  *P
  )
{
  return (UINT16)((UINT16)P[0] | ((UINT16)P[1] << 8));
}

static INT16
MtpRdLeS16 (
  const UINT8  *P
  )
{
  return (INT16)MtpRdLe16 (P);
}

static UINT32
MtpRdLe32 (
  const UINT8  *P
  )
{
  return (UINT32)P[0] |
         ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) |
         ((UINT32)P[3] << 24);
}

static VOID
MtpWrLe16 (
  UINT8   *P,
  UINT16  V
  )
{
  P[0] = (UINT8)(V & 0xFFu);
  P[1] = (UINT8)((V >> 8) & 0xFFu);
}

static VOID
MtpWrLe32 (
  UINT8   *P,
  UINT32  V
  )
{
  P[0] = (UINT8)(V & 0xFFu);
  P[1] = (UINT8)((V >> 8) & 0xFFu);
  P[2] = (UINT8)((V >> 16) & 0xFFu);
  P[3] = (UINT8)((V >> 24) & 0xFFu);
}

#ifndef AURORA_BIOS_HOST_TEST
static VOID
MtpWrLe64 (
  UINT8   *P,
  UINT64  V
  )
{
  MtpWrLe32 (P, (UINT32)(V & 0xFFFFFFFFULL));
  MtpWrLe32 (P + 4u, (UINT32)((V >> 32) & 0xFFFFFFFFULL));
}

static BOOLEAN
MtpStrEq (
  const UINT8  *Buf,
  UINTN        Len,
  const char   *Lit
  )
{
  UINTN  I = 0;

  while (Lit[I] != '\0') {
    if (I >= Len || Buf[I] != (UINT8)Lit[I]) {
      return FALSE;
    }
    I++;
  }
  return (I == Len);
}
#endif

// ---------------------------------------------------------------------------
// Portable DockChannel + MTP Protocol Core
// ---------------------------------------------------------------------------
UINT32
AuroraDcComputeChecksum (
  const UINT8  *Data,
  UINTN        Length
  )
{
  UINT32  Sum = 0;
  UINTN   Off;

  for (Off = 0; Off + 4u <= Length; Off += 4u) {
    Sum += MtpRdLe32 (Data + Off);
  }
  return 0xFFFFFFFFu - Sum;
}

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
  )
{
  UINTN   PaddedLen;
  UINTN   BodyLen;
  UINTN   TotalLen;
  UINT8   *Body;
  UINT32  Csum;

  if (OutBuf == NULL || OutLen == NULL || Interface >= AURORA_DC_MAX_IFACE) {
    return FALSE;
  }
  if (Payload == NULL && PayloadLength != 0u) {
    return FALSE;
  }

  PaddedLen = (PayloadLength + 3u) & ~(UINTN)3u;
  BodyLen   = AURORA_MTP_SUBHDR_SIZE + PaddedLen;
  TotalLen  = AURORA_DC_HDR_SIZE + BodyLen + AURORA_DC_CSUM_SIZE;
  if (TotalLen > OutCap || BodyLen > 0xFFFCu) {
    return FALSE;
  }

  // DockChannel 8-byte header
  OutBuf[0] = AURORA_DC_HDR_SIZE;
  OutBuf[1] = AURORA_DC_CHANNEL_CMD;
  MtpWrLe16 (OutBuf + 2u, (UINT16)BodyLen);
  OutBuf[4] = Sequence;
  OutBuf[5] = Interface;
  OutBuf[6] = 0;
  OutBuf[7] = 0;

  // MTP 8-byte subheader + aligned payload
  Body = OutBuf + AURORA_DC_HDR_SIZE;
  Body[0] = (UINT8)(((ReportType & 3u) << 6) | (RequestType & 0x3Fu));
  Body[1] = 0;
  MtpWrLe16 (Body + 2u, (UINT16)PayloadLength);
  MtpWrLe32 (Body + 4u, 0u);
  if (PayloadLength > 0u) {
    MTP_COPY (Body + AURORA_MTP_SUBHDR_SIZE, Payload, PayloadLength);
  }
  if (PaddedLen > PayloadLength) {
    MTP_ZERO (Body + AURORA_MTP_SUBHDR_SIZE + PayloadLength, PaddedLen - PayloadLength);
  }

  Csum = AuroraDcComputeChecksum (OutBuf, AURORA_DC_HDR_SIZE + BodyLen);
  MtpWrLe32 (OutBuf + AURORA_DC_HDR_SIZE + BodyLen, Csum);
  *OutLen = TotalLen;
  return TRUE;
}

static BOOLEAN
ParsePacketBuffer (
  const UINT8        *Buf,
  UINT32             Len,
  AURORA_MTP_PACKET  *Out
  )
{
  UINT16       BodyLen;
  UINT32       ExpectedCsum;
  UINT32       SuppliedCsum;
  const UINT8  *Body;
  UINT8        Flags;
  UINT8        Group;
  UINT8        Req;
  UINT16       PayloadLen;
  UINT32       PaddedLen;

  MTP_ZERO (Out, sizeof (*Out));
  if (Len < AURORA_DC_HDR_SIZE + AURORA_DC_CSUM_SIZE) {
    return FALSE;
  }
  if (Buf[0] != AURORA_DC_HDR_SIZE) {
    return FALSE;
  }
  if (Buf[1] != AURORA_DC_CHANNEL_CMD && Buf[1] != AURORA_DC_CHANNEL_RPT) {
    return FALSE;
  }
  if (Buf[5] >= AURORA_DC_MAX_IFACE || Buf[6] != 0u || Buf[7] != 0u) {
    return FALSE;
  }
  BodyLen = MtpRdLe16 (Buf + 2u);
  if ((BodyLen & 3u) != 0u) {
    return FALSE;
  }
  if (Len != (UINT32)AURORA_DC_HDR_SIZE + (UINT32)BodyLen + (UINT32)AURORA_DC_CSUM_SIZE) {
    return FALSE;
  }

  ExpectedCsum = AuroraDcComputeChecksum (Buf, (UINTN)AURORA_DC_HDR_SIZE + (UINTN)BodyLen);
  SuppliedCsum = MtpRdLe32 (Buf + AURORA_DC_HDR_SIZE + BodyLen);
  if (ExpectedCsum != SuppliedCsum) {
    return FALSE;
  }

  if (BodyLen < AURORA_MTP_SUBHDR_SIZE) {
    return FALSE;
  }
  Body       = Buf + AURORA_DC_HDR_SIZE;
  Flags      = Body[0];
  Group      = (Flags >> 6) & 3u;
  Req        = Flags & 0x3Fu;
  PayloadLen = MtpRdLe16 (Body + 2u);
  PaddedLen  = ((UINT32)PayloadLen + 3u) & ~3u;
  if (Body[1] != 0u || Group > 2u || Req > 1u) {
    return FALSE;
  }
  if ((UINT32)AURORA_MTP_SUBHDR_SIZE + PaddedLen != (UINT32)BodyLen) {
    return FALSE;
  }

  // Note: trailing bytes in [PayloadLen, PaddedLen) are NOT required to be zero
  // (Apple MTP firmware echoes the interface ID in the pad tail on ENABLE ACK).
  Out->Channel       = Buf[1];
  Out->Sequence      = Buf[4];
  Out->Interface     = Buf[5];
  Out->BodyLength    = BodyLen;
  Out->Body          = Body;
  Out->ReportType    = Group;
  Out->RequestType   = Req;
  Out->PayloadLength = PayloadLen;
  Out->ReturnCode    = MtpRdLe32 (Body + 4u);
  Out->Payload       = Body + AURORA_MTP_SUBHDR_SIZE;
  return TRUE;
}

BOOLEAN
AuroraMtpStreamPushByte (
  AURORA_MTP_STREAM  *Stream,
  UINT8              Byte,
  AURORA_MTP_PACKET  *OutPacket,
  BOOLEAN            *OutReady
  )
{
  UINT16  BodyLen;

  *OutReady = FALSE;
  if (Stream == NULL || OutPacket == NULL) {
    return FALSE;
  }
  if (Stream->Length >= sizeof (Stream->Buffer)) {
    MTP_ZERO (Stream, sizeof (*Stream));
    return FALSE;
  }

  // Re-sync byte-by-byte if the very first byte is not valid header length 0x08
  if (Stream->Length == 0u && Byte != AURORA_DC_HDR_SIZE) {
    return FALSE;
  }

  Stream->Buffer[Stream->Length++] = Byte;
  if (Stream->Length == AURORA_DC_HDR_SIZE) {
    if (Stream->Buffer[0] != AURORA_DC_HDR_SIZE ||
        (Stream->Buffer[1] != AURORA_DC_CHANNEL_CMD && Stream->Buffer[1] != AURORA_DC_CHANNEL_RPT) ||
        Stream->Buffer[5] >= AURORA_DC_MAX_IFACE ||
        Stream->Buffer[6] != 0u ||
        Stream->Buffer[7] != 0u) {
      MTP_ZERO (Stream, sizeof (*Stream));
      return FALSE;
    }
    BodyLen = MtpRdLe16 (Stream->Buffer + 2u);
    if ((BodyLen & 3u) != 0u) {
      MTP_ZERO (Stream, sizeof (*Stream));
      return FALSE;
    }
    Stream->ExpectedLength = AURORA_DC_HDR_SIZE + (UINT32)BodyLen + AURORA_DC_CSUM_SIZE;
    if (Stream->ExpectedLength > sizeof (Stream->Buffer)) {
      MTP_ZERO (Stream, sizeof (*Stream));
      return FALSE;
    }
  }

  if (Stream->ExpectedLength == 0u || Stream->Length < Stream->ExpectedLength) {
    return TRUE;
  }

  if (!ParsePacketBuffer (Stream->Buffer, Stream->Length, OutPacket)) {
    MTP_ZERO (Stream, sizeof (*Stream));
    return FALSE;
  }
  *OutReady = TRUE;
  return TRUE;
}

VOID
AuroraMtpStreamConsume (
  AURORA_MTP_STREAM  *Stream
  )
{
  if (Stream != NULL) {
    Stream->Length         = 0;
    Stream->ExpectedLength = 0;
  }
}

BOOLEAN
AuroraMtpHidUsesReportIds (
  const UINT8  *Descriptor,
  UINTN        Length,
  BOOLEAN      *UsesReportIds
  )
{
  UINTN  Off = 0;

  if (Descriptor == NULL || UsesReportIds == NULL || Length == 0u) {
    return FALSE;
  }
  *UsesReportIds = FALSE;
  while (Off < Length) {
    UINT8   Prefix = Descriptor[Off++];
    UINTN   DataLen;
    UINT32  Type;
    UINT32  Tag;

    if (Prefix == 0xFEu) {
      if (Length - Off < 2u) {
        return FALSE;
      }
      DataLen = Descriptor[Off];
      Off += 2u;
      if (DataLen > Length - Off) {
        return FALSE;
      }
      Off += DataLen;
      continue;
    }
    switch (Prefix & 3u) {
      case 0u: DataLen = 0u; break;
      case 1u: DataLen = 1u; break;
      case 2u: DataLen = 2u; break;
      default: DataLen = 4u; break;
    }
    if (DataLen > Length - Off) {
      return FALSE;
    }
    Type = (Prefix >> 2) & 3u;
    Tag  = (Prefix >> 4) & 0x0Fu;
    if (Type == 1u && Tag == 8u) {  // Global item: Report ID
      if (DataLen != 1u || Descriptor[Off] == 0u) {
        return FALSE;
      }
      *UsesReportIds = TRUE;
    }
    Off += DataLen;
  }
  return TRUE;
}

BOOLEAN
AuroraMtpPrepareFirmware (
  const UINT8  *Container,
  UINTN        ContainerLen,
  UINT8        TargetInterface,
  UINT8        *StagingOut,
  UINTN        StagingCap,
  UINTN        *OutPayloadLen
  )
{
  UINT32  Magic;
  UINT32  Version;
  UINT32  HdrLen;
  UINT32  DataLen;
  UINT32  IfaceOff;

  if (Container == NULL || StagingOut == NULL || OutPayloadLen == NULL) {
    return FALSE;
  }
  *OutPayloadLen = 0;
  if (TargetInterface == AURORA_MTP_IFACE_COMM || TargetInterface >= AURORA_DC_MAX_IFACE) {
    return FALSE;
  }
  if (ContainerLen < AURORA_MTP_FW_HDR_SIZE || ContainerLen > AURORA_MTP_FW_MAX_CONTAINER) {
    return FALSE;
  }

  Magic    = MtpRdLe32 (Container + 0u);
  Version  = MtpRdLe32 (Container + 4u);
  HdrLen   = MtpRdLe32 (Container + 8u);
  DataLen  = MtpRdLe32 (Container + 12u);
  IfaceOff = MtpRdLe32 (Container + 16u);
  if (Magic != AURORA_MTP_FW_MAGIC || Version != AURORA_MTP_FW_VERSION) {
    return FALSE;
  }
  if (HdrLen < AURORA_MTP_FW_HDR_SIZE || HdrLen > AURORA_MTP_FW_MAX_HDR || (UINTN)HdrLen > ContainerLen) {
    return FALSE;
  }
  if (DataLen == 0u || DataLen > AURORA_MTP_FW_MAX_DATA || (UINTN)DataLen > ContainerLen - (UINTN)HdrLen) {
    return FALSE;
  }
  if (IfaceOff >= DataLen || (UINTN)DataLen > StagingCap) {
    return FALSE;
  }

  MTP_COPY (StagingOut, Container + HdrLen, DataLen);
  if (IfaceOff != 0u) {
    StagingOut[IfaceOff] = TargetInterface;
  }
  *OutPayloadLen = DataLen;
  return TRUE;
}

// ---------------------------------------------------------------------------
// Apple USB-HID Keyboard Usage Translation + Key Repeat
// ---------------------------------------------------------------------------
AURORA_BIOS_KEY
AuroraMtpHidUsageToKey (
  UINT8    Usage,
  BOOLEAN  ShiftHeld
  )
{
  switch (Usage) {
    case 0x52u: return AURORA_KEY_UP;
    case 0x51u: return AURORA_KEY_DOWN;
    case 0x50u: return AURORA_KEY_LEFT;
    case 0x4Fu: return AURORA_KEY_RIGHT;
    case 0x28u:
    case 0x58u: return AURORA_KEY_ENTER;
    case 0x2Cu: return AURORA_KEY_SPACE;
    case 0x29u:
    case 0x2Au: return AURORA_KEY_ESC;
    case 0x2Bu: return ShiftHeld ? AURORA_KEY_SHIFT_TAB : AURORA_KEY_TAB;
    case 0x3Au: return AURORA_KEY_F1;
    case 0x3Bu: return AURORA_KEY_F2;
    case 0x3Cu: return AURORA_KEY_F3;
    case 0x3Du: return AURORA_KEY_F4;
    case 0x3Eu: return AURORA_KEY_F5;
    case 0x40u: return AURORA_KEY_F7;
    case 0x42u: return AURORA_KEY_F9;
    case 0x43u: return AURORA_KEY_F10;
    case 0x44u: return AURORA_KEY_F11;
    case 0x1Eu: return AURORA_KEY_F1;   // '1' -> Help / Tab 1
    case 0x1Fu: return AURORA_KEY_F2;   // '2' -> Easy/Advanced toggle
    case 0x20u: return AURORA_KEY_F3;   // '3' -> Sub-tab 3
    case 0x21u: return AURORA_KEY_F4;   // '4' -> Sub-tab 4
    case 0x22u: return AURORA_KEY_F5;   // '5' -> Sub-tab 5
    case 0x2Eu:
    case 0x57u: return AURORA_KEY_PLUS;
    case 0x2Du:
    case 0x56u: return AURORA_KEY_MINUS;
    case 0x0Bu: return AURORA_KEY_F1;   // 'H' -> Help
    default:    return AURORA_KEY_NONE;
  }
}

static BOOLEAN
IsUsageRepeatable (
  UINT8  Usage
  )
{
  return (Usage == 0x52u || Usage == 0x51u || Usage == 0x50u || Usage == 0x4Fu ||
          Usage == 0x2Eu || Usage == 0x2Du || Usage == 0x2Bu);
}

static VOID
PushEventKey (
  AURORA_MTP_INPUT_EVENTS  *Events,
  AURORA_BIOS_KEY          Key
  )
{
  if (Key != AURORA_KEY_NONE && Events->KeyCount < 8u) {
    Events->Keys[Events->KeyCount++] = (UINT8)Key;
  }
}

VOID
AuroraMtpDecodeKeyboardReport (
  AURORA_MTP_KB_STATE      *Kb,
  const UINT8              *WireReport,
  UINTN                    WireLen,
  BOOLEAN                  UsesReportIds,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  )
{
  const UINT8  *Payload;
  UINTN        PayloadLen;
  UINT8        Modifiers;
  BOOLEAN      ShiftHeld;
  const UINT8  *Keys;
  UINT32       I;
  UINT32       J;
  BOOLEAN      HeldStillPresent = FALSE;

  if (Kb == NULL || WireReport == NULL || Events == NULL || WireLen == 0u) {
    return;
  }

  if (UsesReportIds) {
    // Report ID 0x01 is the standard Apple MTP keyboard report.
    // Ignore non-keyboard reports (Fn/Consumer report IDs like 0x0C, 0xFF).
    if (WireReport[0] != 0x01u || WireLen < 9u) {
      return;
    }
    Payload    = WireReport + 1u;
    PayloadLen = WireLen - 1u;
  } else {
    if (WireLen < 8u) {
      return;
    }
    Payload    = WireReport;
    PayloadLen = WireLen;
  }

  if (PayloadLen < 8u) {
    return;
  }
  Modifiers = Payload[0];
  ShiftHeld = ((Modifiers & 0x22u) != 0u);  // Left Shift (0x02) or Right Shift (0x20)
  Keys      = Payload + 2u;

  // Reject rollover error (all 0x01)
  if (Keys[0] == 0x01u && Keys[1] == 0x01u) {
    return;
  }

  for (I = 0; I < 6u; I++) {
    UINT8    Usage = Keys[I];
    BOOLEAN  WasDown = FALSE;

    if (Usage == 0u) {
      continue;
    }
    if (Usage == Kb->HeldKey) {
      HeldStillPresent = TRUE;
    }
    for (J = 0; J < 6u; J++) {
      if (Kb->PrevKeys[J] == Usage) {
        WasDown = TRUE;
        break;
      }
    }
    if (!WasDown) {
      AURORA_BIOS_KEY  Mapped = AuroraMtpHidUsageToKey (Usage, ShiftHeld);
      PushEventKey (Events, Mapped);
      if (IsUsageRepeatable (Usage)) {
        Kb->HeldKey       = Usage;
        Kb->HeldModifiers = Modifiers;
        Kb->HeldStartMs   = NowMs;
        Kb->LastRepeatMs  = NowMs;
        HeldStillPresent  = TRUE;
      }
    }
  }

  if (!HeldStillPresent) {
    Kb->HeldKey = 0;
  }
  MTP_COPY (Kb->PrevKeys, Keys, 6u);
}

VOID
AuroraMtpTickKeyboardRepeat (
  AURORA_MTP_KB_STATE      *Kb,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  )
{
  if (Kb == NULL || Events == NULL || Kb->HeldKey == 0u) {
    return;
  }
  if (NowMs - Kb->HeldStartMs >= 360u && NowMs - Kb->LastRepeatMs >= 55u) {
    BOOLEAN          ShiftHeld = ((Kb->HeldModifiers & 0x22u) != 0u);
    AURORA_BIOS_KEY  Mapped    = AuroraMtpHidUsageToKey (Kb->HeldKey, ShiftHeld);
    PushEventKey (Events, Mapped);
    Kb->LastRepeatMs = NowMs;
  }
}

// ---------------------------------------------------------------------------
// Apple MultiTouch Trackpad Decoder (MTP 0x75 & SPI 0x02 Layouts)
// ---------------------------------------------------------------------------
#define MTP_TP_REPORT_ID         0x75u
#define MTP_TP_HDR_LEN           38u
#define MTP_TP_FINGER_COUNT_OFF  22u
#define MTP_TP_BUTTONS_OFF       23u
#define MTP_TP_FINGER_STRIDE     30u
#define MTP_TP_FINGER_X_OFF      4u
#define MTP_TP_FINGER_Y_OFF      6u
#define MTP_TP_FINGER_MAJOR_OFF  18u

#define SPI_TP_REPORT_ID         0x02u
#define SPI_TP_HDR_LEN           46u
#define SPI_TP_FINGER_COUNT_OFF  30u
#define SPI_TP_BUTTONS_OFF       31u

#define TP_MOTION_DIVISOR        6
#define TP_JUMP_GUARD_RAW        1800
#define TP_TAP_MAX_TIME_MS       220u
#define TP_TAP_MAX_DIST_RAW      260

static INT32
TpScaleMotion (
  INT32  RawDelta,
  INT32  *Remainder
  )
{
  INT32  Total  = RawDelta + *Remainder;
  INT32  Scaled = Total / TP_MOTION_DIVISOR;

  *Remainder = Total - Scaled * TP_MOTION_DIVISOR;
  if (Scaled > 96) {
    Scaled     = 96;
    *Remainder = 0;
  } else if (Scaled < -96) {
    Scaled     = -96;
    *Remainder = 0;
  }
  return Scaled;
}

BOOLEAN
AuroraMtpDecodeTrackpadReport (
  AURORA_MTP_TP_STATE      *Tp,
  const UINT8              *WireReport,
  UINTN                    WireLen,
  UINT32                   NowMs,
  AURORA_MTP_INPUT_EVENTS  *Events
  )
{
  const UINT8  *Frame     = WireReport;
  UINTN        FrameLen   = WireLen;
  UINTN        HdrLen     = MTP_TP_HDR_LEN;
  UINTN        CountOff   = MTP_TP_FINGER_COUNT_OFF;
  UINTN        BtnOff     = MTP_TP_BUTTONS_OFF;
  UINT8        HwCount;
  UINT8        ActiveCount = 0;
  BOOLEAN      PhysDown;
  INT16        PrimaryX = 0;
  INT16        PrimaryY = 0;
  INT32        Dx       = 0;
  INT32        Dy       = 0;
  UINT32       I;

  if (Tp == NULL || WireReport == NULL || Events == NULL || WireLen < MTP_TP_HDR_LEN) {
    return FALSE;
  }

  // Detect MTP 0x75 prefixed frame vs raw 38-byte MTP frame vs SPI 0x02 frame
  if (WireLen >= 1u + MTP_TP_HDR_LEN &&
      WireReport[0] == MTP_TP_REPORT_ID &&
      ((WireLen - 1u - MTP_TP_HDR_LEN) % MTP_TP_FINGER_STRIDE) == 0u) {
    Frame    = WireReport + 1u;
    FrameLen = WireLen - 1u;
  } else if (WireLen >= SPI_TP_HDR_LEN &&
             WireReport[0] == SPI_TP_REPORT_ID &&
             ((WireLen - SPI_TP_HDR_LEN) % MTP_TP_FINGER_STRIDE) == 0u) {
    HdrLen   = SPI_TP_HDR_LEN;
    CountOff = SPI_TP_FINGER_COUNT_OFF;
    BtnOff   = SPI_TP_BUTTONS_OFF;
  } else if (((WireLen - MTP_TP_HDR_LEN) % MTP_TP_FINGER_STRIDE) != 0u) {
    return FALSE;
  }

  HwCount  = Frame[CountOff];
  PhysDown = ((Frame[BtnOff] & 1u) != 0u);
  if (HwCount > 16u || HdrLen + (UINTN)HwCount * MTP_TP_FINGER_STRIDE > FrameLen) {
    return FALSE;
  }

  // Find active contacts (TouchMajor != 0)
  for (I = 0; I < HwCount; I++) {
    const UINT8  *F = Frame + HdrLen + I * MTP_TP_FINGER_STRIDE;
    if (MtpRdLeS16 (F + MTP_TP_FINGER_MAJOR_OFF) == 0) {
      continue;
    }
    if (ActiveCount == 0u) {
      PrimaryX = MtpRdLeS16 (F + MTP_TP_FINGER_X_OFF);
      PrimaryY = MtpRdLeS16 (F + MTP_TP_FINGER_Y_OFF);
    }
    ActiveCount++;
  }

  // Compute smooth relative cursor delta when primary finger is active
  if (ActiveCount >= 1u) {
    if (Tp->Active) {
      INT32  RawDx = (INT32)PrimaryX - (INT32)Tp->LastX;
      INT32  RawDy = (INT32)Tp->LastY - (INT32)PrimaryY;  // Invert Y: trackpad Y grows up
      if (RawDx > -TP_JUMP_GUARD_RAW && RawDx < TP_JUMP_GUARD_RAW &&
          RawDy > -TP_JUMP_GUARD_RAW && RawDy < TP_JUMP_GUARD_RAW) {
        Dx = TpScaleMotion (RawDx, &Tp->RemX);
        Dy = TpScaleMotion (RawDy, &Tp->RemY);
      } else {
        Tp->RemX = 0;
        Tp->RemY = 0;
      }
    } else {
      Tp->Active = TRUE;
      Tp->RemX   = 0;
      Tp->RemY   = 0;
    }
    Tp->LastX = PrimaryX;
    Tp->LastY = PrimaryY;
  } else {
    Tp->Active = FALSE;
    Tp->RemX   = 0;
    Tp->RemY   = 0;
  }

  // Tap-to-click detection (clean brief 1-finger tap without physical click)
  if (PhysDown) {
    Tp->TapCandidate = FALSE;
  } else if (ActiveCount == 1u) {
    if (Tp->PrevFingerCount == 0u) {
      Tp->TapCandidate = TRUE;
      Tp->TapStartMs   = NowMs;
      Tp->TapStartX    = PrimaryX;
      Tp->TapStartY    = PrimaryY;
    } else if (Tp->TapCandidate) {
      INT32  Dtx = (INT32)PrimaryX - (INT32)Tp->TapStartX;
      INT32  Dty = (INT32)PrimaryY - (INT32)Tp->TapStartY;
      if (NowMs - Tp->TapStartMs > TP_TAP_MAX_TIME_MS ||
          Dtx * Dtx + Dty * Dty > TP_TAP_MAX_DIST_RAW * TP_TAP_MAX_DIST_RAW) {
        Tp->TapCandidate = FALSE;
      }
    }
  } else if (ActiveCount == 0u) {
    if (Tp->TapCandidate && Tp->PrevFingerCount == 1u &&
        (NowMs - Tp->TapStartMs) <= TP_TAP_MAX_TIME_MS) {
      Events->TapPulse = TRUE;
    }
    Tp->TapCandidate = FALSE;
  } else {
    Tp->TapCandidate = FALSE;
  }

  Tp->PrevFingerCount = ActiveCount;
  Tp->PhysicalDown    = PhysDown;

  Events->HasPointerEvent = TRUE;
  Events->DeltaX         += Dx;
  Events->DeltaY         += Dy;
  Events->LeftDown        = PhysDown;
  return TRUE;
}

// ---------------------------------------------------------------------------
// Portable Apple FPWM Keyboard Backlight Cycle Formula
// ---------------------------------------------------------------------------
VOID
AuroraKblComputeCycles (
  UINT32  Brightness,
  UINT32  *OnCycles,
  UINT32  *OffCycles
  )
{
  UINT64  DutyNs;
  UINT64  On;

  if (Brightness > 255u) {
    Brightness = 255u;
  }
  DutyNs = ((UINT64)AURORA_KBL_PERIOD_NS * (UINT64)Brightness) / 255ULL;
  On     = (AURORA_KBL_CLOCK_HZ * DutyNs) / 1000000000ULL;
  if (On > AURORA_KBL_TOTAL_CYCLES) {
    On = AURORA_KBL_TOTAL_CYCLES;
  }
  if (OnCycles != NULL) {
    *OnCycles = (UINT32)On;
  }
  if (OffCycles != NULL) {
    *OffCycles = (UINT32)(AURORA_KBL_TOTAL_CYCLES - On);
  }
}

// ===========================================================================
// UEFI Hardware Driver Section (AArch64 Mu Runtime)
// ===========================================================================
#ifndef AURORA_BIOS_HOST_TEST

static UINT32
UefiNowMs (
  VOID
  )
{
  return (UINT32)(GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000000ULL);
}

// ---------------------------------------------------------------------------
// BlockIo Byte-Level Reader Wrapper for AuroraNtfsReadFile
// ---------------------------------------------------------------------------
typedef struct {
  EFI_BLOCK_IO_PROTOCOL  *BlkIo;
  UINT32                 BlockSize;
  UINT8                  *BounceBuf;
  UINTN                  BounceCap;
} UEFI_BLKIO_READ_CTX;

static BOOLEAN
UefiBlkIoReadBytes (
  VOID    *Context,
  UINT64  ByteOffset,
  UINTN   Length,
  VOID    *OutBuffer
  )
{
  UEFI_BLKIO_READ_CTX  *C = (UEFI_BLKIO_READ_CTX *)Context;
  UINT32               Bs;
  UINTN                MaxChunkBlocks;
  UINT8                *Dst = (UINT8 *)OutBuffer;
  UINTN                Done = 0;

  if (C == NULL || C->BlkIo == NULL || C->BlockSize == 0u || Length == 0u) {
    return FALSE;
  }
  Bs = C->BlockSize;
  MaxChunkBlocks = (65536u / Bs);
  if (MaxChunkBlocks == 0u) {
    MaxChunkBlocks = 1u;
  }

  while (Done < Length) {
    UINT64      CurOff      = ByteOffset + (UINT64)Done;
    UINT64      StartLba    = CurOff / Bs;
    UINTN       InBlockOff  = (UINTN)(CurOff % Bs);
    UINTN       Rem         = Length - Done;
    UINTN       NeedBlocks  = (InBlockOff + Rem + Bs - 1u) / Bs;
    UINTN       TakeBlocks  = (NeedBlocks < MaxChunkBlocks) ? NeedBlocks : MaxChunkBlocks;
    UINTN       ReadBytes   = TakeBlocks * (UINTN)Bs;
    UINTN       AvailBytes  = ReadBytes - InBlockOff;
    UINTN       CopyBytes   = (Rem < AvailBytes) ? Rem : AvailBytes;
    EFI_STATUS  Status;

    if (StartLba + (UINT64)TakeBlocks - 1ULL > C->BlkIo->Media->LastBlock) {
      return FALSE;
    }

    if (ReadBytes > C->BounceCap) {
      if (C->BounceBuf != NULL) {
        FreePool (C->BounceBuf);
      }
      C->BounceBuf = AllocatePool (ReadBytes);
      if (C->BounceBuf == NULL) {
        C->BounceCap = 0;
        return FALSE;
      }
      C->BounceCap = ReadBytes;
    }

    Status = C->BlkIo->ReadBlocks (
                         C->BlkIo,
                         C->BlkIo->Media->MediaId,
                         StartLba,
                         ReadBytes,
                         C->BounceBuf
                         );
    if (EFI_ERROR (Status)) {
      return FALSE;
    }
    CopyMem (Dst + Done, C->BounceBuf + InBlockOff, CopyBytes);
    Done += CopyBytes;
  }
  return TRUE;
}

// Try loading trackpad firmware (HIDF) from FAT ESP first, then Windows NTFS partition!
static BOOLEAN
LoadTrackpadFirmwareContainer (
  OUT UINT8   *OutBuf,
  IN  UINTN   MaxBytes,
  OUT UINTN   *OutLen,
  OUT char    *SourceOut,
  IN  UINTN   SourceCap
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       I;

  *OutLen = 0;
  AsciiStrCpyS (SourceOut, SourceCap, "NONE");

  // 1. Check FAT ESP volumes (\EFI\Aurora\tpmtfw.bin)
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (!EFI_ERROR (Status) && Handles != NULL) {
    for (I = 0; I < Count; I++) {
      EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs   = NULL;
      EFI_FILE_PROTOCOL                *Root = NULL;
      EFI_FILE_PROTOCOL                *File = NULL;
      UINTN                            ReadSz;

      if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) || Fs == NULL) {
        continue;
      }
      if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || Root == NULL) {
        continue;
      }
      if (!EFI_ERROR (Root->Open (Root, &File, L"\\EFI\\Aurora\\tpmtfw.bin", EFI_FILE_MODE_READ, 0)) && File != NULL) {
        ReadSz = MaxBytes;
        if (!EFI_ERROR (File->Read (File, &ReadSz, OutBuf)) &&
            ReadSz >= AURORA_MTP_FW_HDR_SIZE &&
            MtpRdLe32 (OutBuf) == AURORA_MTP_FW_MAGIC) {
          *OutLen = ReadSz;
          AsciiStrCpyS (SourceOut, SourceCap, "ESP");
          File->Close (File);
          Root->Close (Root);
          FreePool (Handles);
          return TRUE;
        }
        File->Close (File);
      }
      Root->Close (Root);
    }
    FreePool (Handles);
  }

  // 2. Scan logical BlockIo partitions for NTFS \Windows\System32\drivers\tpmtfw.bin
  Handles = NULL;
  Count   = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  DEBUG ((DEBUG_ERROR, "[AURORA MTP] Scanning %u BlockIo handles for NTFS tpmtfw.bin\r\n", (UINT32)Count));
  if (!EFI_ERROR (Status) && Handles != NULL) {
    UEFI_BLKIO_READ_CTX  RdCtx;
    ZeroMem (&RdCtx, sizeof (RdCtx));

    for (I = 0; I < Count; I++) {
      EFI_BLOCK_IO_PROTOCOL  *BlkIo = NULL;
      UINT64                 PartBytes;
      UINTN                  FoundLen = 0;
      UINT8                  BootSig[16];

      if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&BlkIo)) ||
          BlkIo == NULL || BlkIo->Media == NULL || !BlkIo->Media->MediaPresent) {
        continue;
      }
      if (!BlkIo->Media->LogicalPartition || BlkIo->Media->BlockSize == 0u || BlkIo->Media->BlockSize > 16384u) {
        continue;
      }
      PartBytes       = (BlkIo->Media->LastBlock + 1ULL) * (UINT64)BlkIo->Media->BlockSize;
      RdCtx.BlkIo     = BlkIo;
      RdCtx.BlockSize = BlkIo->Media->BlockSize;

      ZeroMem (BootSig, sizeof (BootSig));
      if (UefiBlkIoReadBytes (&RdCtx, 0ULL, sizeof (BootSig), BootSig) &&
          BootSig[3] == 'N' && BootSig[4] == 'T' && BootSig[5] == 'F' && BootSig[6] == 'S') {
        DEBUG ((
          DEBUG_ERROR,
          "[AURORA MTP] Found NTFS partition #%u (bs=%u size=%Lu MiB)\r\n",
          (UINT32)I,
          BlkIo->Media->BlockSize,
          PartBytes / (1024ULL * 1024ULL)
          ));
      }

      if (AuroraNtfsReadFile (
            UefiBlkIoReadBytes,
            &RdCtx,
            PartBytes,
            "Windows\\System32\\drivers\\tpmtfw.bin",
            OutBuf,
            MaxBytes,
            &FoundLen
            ) &&
          FoundLen >= AURORA_MTP_FW_HDR_SIZE &&
          MtpRdLe32 (OutBuf) == AURORA_MTP_FW_MAGIC) {
        *OutLen = FoundLen;
        AsciiStrCpyS (SourceOut, SourceCap, "NTFS");
        if (RdCtx.BounceBuf != NULL) {
          FreePool (RdCtx.BounceBuf);
        }
        FreePool (Handles);
        return TRUE;
      }

      // Fallback: auto-discover any HIDF *.bin trackpad firmware inside \Windows\System32\drivers
      {
        char  MatchedName[64];
        if (AuroraNtfsFindMagicBinInDir (
              UefiBlkIoReadBytes,
              &RdCtx,
              PartBytes,
              "Windows\\System32\\drivers",
              AURORA_MTP_FW_MAGIC,
              OutBuf,
              MaxBytes,
              &FoundLen,
              MatchedName,
              sizeof (MatchedName)
              )) {
          DEBUG ((DEBUG_ERROR, "[AURORA MTP] Auto-discovered HIDF firmware in drivers\\%a (%u bytes)\r\n",
                  MatchedName, (UINT32)FoundLen));
          *OutLen = FoundLen;
          AsciiStrCpyS (SourceOut, SourceCap, "NTFS");
          if (RdCtx.BounceBuf != NULL) {
            FreePool (RdCtx.BounceBuf);
          }
          FreePool (Handles);
          return TRUE;
        }
      }
    }

    if (RdCtx.BounceBuf != NULL) {
      FreePool (RdCtx.BounceBuf);
    }
    FreePool (Handles);
  }

  return FALSE;
}

// ---------------------------------------------------------------------------
// DockChannel MMIO TX / RX Helpers
// ---------------------------------------------------------------------------
static BOOLEAN
MtpTxPacket (
  IN AURORA_MTP_CONTEXT  *Ctx,
  IN const UINT8         *Packet,
  IN UINTN               Length
  )
{
  UINT32  Wait;
  UINTN   Off = 0;

  if (Length == 0u || Length > AURORA_DC_FIFO_SIZE) {
    return FALSE;
  }

  // Wait briefly for TX FIFO space
  for (Wait = 0; Wait < 200u; Wait++) {
    UINT32  FreeBytes = MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_TX_FREE));
    if (FreeBytes >= (UINT32)Length) {
      break;
    }
    MicroSecondDelay (500);
  }
  if (MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_TX_FREE)) < (UINT32)Length) {
    return FALSE;
  }

  while (Length - Off >= 4u) {
    MmioWrite32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_TX32), MtpRdLe32 (Packet + Off));
    Off += 4u;
  }
  while (Off < Length) {
    MmioWrite32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_TX8), (UINT32)Packet[Off]);
    Off++;
  }
  return TRUE;
}

static VOID
HandleInitPacket (
  IN OUT AURORA_MTP_CONTEXT     *Ctx,
  IN     const AURORA_MTP_PACKET *Pkt
  )
{
  const UINT8               *P = Pkt->Payload;
  UINT8                     Iface;
  UINTN                     NameLen = 0;
  BOOLEAN                   MorePackets;
  AURORA_MTP_ENDPOINT_INFO  *Ep = NULL;
  UINTN                     Off;
  UINTN                     Rem;

  if (Pkt->PayloadLength < 22u || P[0] != AURORA_MTP_EVENT_INIT || P[1] != 1u || P[2] != 0u) {
    return;
  }
  Iface = P[3];
  if (Iface == AURORA_MTP_IFACE_COMM || Iface >= AURORA_DC_MAX_IFACE) {
    return;
  }
  while (NameLen < 16u && P[4u + NameLen] != 0u) {
    NameLen++;
  }
  MorePackets = (P[20] != 0u);

  if (MtpStrEq (P + 4u, NameLen, "stm")) {
    Ep = &Ctx->Stm;
  } else if (MtpStrEq (P + 4u, NameLen, "keyboard")) {
    Ep = &Ctx->Keyboard;
  } else if (MtpStrEq (P + 4u, NameLen, "multi-touch")) {
    Ep = &Ctx->MultiTouch;
  } else {
    return;
  }

  if (!Ep->Present) {
    Ep->Present   = TRUE;
    Ep->Interface = Iface;
  }

  // Parse TLV init blocks starting at offset 22
  Off = 22u;
  Rem = Pkt->PayloadLength - 22u;
  while (Rem >= 4u) {
    UINT16  BlkType = MtpRdLe16 (P + Off);
    UINT16  BlkLen  = MtpRdLe16 (P + Off + 2u);
    if ((UINTN)BlkLen > Rem - 4u) {
      break;
    }
    if (BlkType == 0u && BlkLen > 0u) {  // HID Descriptor
      BOOLEAN  UsesIds = FALSE;
      if (AuroraMtpHidUsesReportIds (P + Off + 4u, BlkLen, &UsesIds)) {
        Ep->UsesReportIds = UsesIds;
      }
      Ep->DescLength = BlkLen;
    } else if (BlkType == 1u) {          // GPIO request (not present on J813)
      Ep->HasGpio = TRUE;
    } else if (BlkType == 2u) {          // Terminator
      break;
    }
    Off += 4u + (UINTN)BlkLen;
    Rem -= 4u + (UINTN)BlkLen;
  }

  if (!MorePackets) {
    Ep->InitComplete = TRUE;
    DEBUG ((
      DEBUG_ERROR,
      "[AURORA MTP] INIT iface=%u name=%.*a desc=%u reportIds=%u gpio=%u\r\n",
      Iface,
      (INT32)NameLen,
      (const char *)(P + 4u),
      Ep->DescLength,
      Ep->UsesReportIds ? 1u : 0u,
      Ep->HasGpio ? 1u : 0u
      ));
  }
}

static VOID
HandleDecodedPacket (
  IN OUT AURORA_MTP_CONTEXT        *Ctx,
  IN     const AURORA_MTP_PACKET   *Pkt,
  IN     UINT32                    NowMs,
  IN OUT AURORA_MTP_INPUT_EVENTS   *Events  OPTIONAL
  )
{
  Ctx->PacketsRx++;

  // Command ACK channel (0x11)
  if (Pkt->Channel == AURORA_DC_CHANNEL_CMD) {
    if (Ctx->AckWaiting &&
        Pkt->Interface == AURORA_MTP_IFACE_COMM &&
        Pkt->Sequence == Ctx->AckExpectedSeq &&
        Pkt->PayloadLength >= 1u &&
        Pkt->Payload[0] == Ctx->AckExpectedCmd) {
      Ctx->AckReceived   = TRUE;
      Ctx->AckReturnCode = Pkt->ReturnCode;
    }
    return;
  }

  // Control event on COMM interface (0x12, iface 0)
  if (Pkt->Interface == AURORA_MTP_IFACE_COMM) {
    if (Pkt->PayloadLength >= 1u) {
      if (Pkt->Payload[0] == AURORA_MTP_EVENT_INIT) {
        HandleInitPacket (Ctx, Pkt);
      } else if (Pkt->Payload[0] == AURORA_MTP_EVENT_READY && Pkt->PayloadLength >= 4u) {
        UINT8  ReadyIface = Pkt->Payload[1];
        DEBUG ((DEBUG_ERROR, "[AURORA MTP] EVENT_READY iface=%u\r\n", ReadyIface));
        if (Ctx->Stm.Present && Ctx->Stm.Interface == ReadyIface) {
          Ctx->Stm.Ready = TRUE;
        }
        if (Ctx->Keyboard.Present && Ctx->Keyboard.Interface == ReadyIface) {
          Ctx->Keyboard.Ready = TRUE;
        }
        if (Ctx->MultiTouch.Present && Ctx->MultiTouch.Interface == ReadyIface) {
          Ctx->MultiTouch.Ready = TRUE;
        }
      }
    }
    return;
  }

  // Live HID Input Reports (0x12, iface != 0)
  if (Pkt->ReportType == 0u && Pkt->RequestType == 0u && Pkt->PayloadLength > 0u) {
    if (Ctx->Keyboard.Present && Pkt->Interface == Ctx->Keyboard.Interface) {
      Ctx->KbReports++;
      if (Ctx->KbReports <= 2u) {
        DEBUG ((
          DEBUG_ERROR,
          "[AURORA MTP] KB report #%u len=%u b0=0x%02x b1=0x%02x b3=0x%02x\r\n",
          Ctx->KbReports,
          Pkt->PayloadLength,
          Pkt->Payload[0],
          Pkt->PayloadLength > 1u ? Pkt->Payload[1] : 0u,
          Pkt->PayloadLength > 3u ? Pkt->Payload[3] : 0u
          ));
      }
      if (Events != NULL) {
        AuroraMtpDecodeKeyboardReport (
          &Ctx->KbState,
          Pkt->Payload,
          Pkt->PayloadLength,
          Ctx->Keyboard.UsesReportIds,
          NowMs,
          Events
          );
      }
    } else if (Ctx->MultiTouch.Present && Pkt->Interface == Ctx->MultiTouch.Interface) {
      Ctx->TpReports++;
      if (Ctx->TpReports <= 2u) {
        DEBUG ((
          DEBUG_ERROR,
          "[AURORA MTP] TP report #%u len=%u b0=0x%02x\r\n",
          Ctx->TpReports,
          Pkt->PayloadLength,
          Pkt->Payload[0]
          ));
      }
      if (Events != NULL) {
        AuroraMtpDecodeTrackpadReport (
          &Ctx->TpState,
          Pkt->Payload,
          Pkt->PayloadLength,
          NowMs,
          Events
          );
      }
    }
  }
}

static VOID
MtpDrainRx (
  IN OUT AURORA_MTP_CONTEXT        *Ctx,
  IN     UINT32                    NowMs,
  IN OUT AURORA_MTP_INPUT_EVENTS   *Events  OPTIONAL
  )
{
  UINT32  Avail;
  UINT32  Budget;
  UINT32  Pass;

  // Drain up to 4 FIFO bursts per poll to avoid unbounded spin
  for (Pass = 0; Pass < 4u; Pass++) {
    Avail = MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_RX_COUNT));
    if (Avail == 0u) {
      break;
    }
    Budget = (Avail < AURORA_DC_FIFO_SIZE) ? Avail : AURORA_DC_FIFO_SIZE;

    while (Budget >= 4u) {
      UINT32             Val = MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_RX32));
      UINT32             B;
      AURORA_MTP_PACKET  Pkt;
      BOOLEAN            Ready = FALSE;

      for (B = 0; B < 4u; B++) {
        UINT8  Byte = (UINT8)((Val >> (B * 8u)) & 0xFFu);
        if (!AuroraMtpStreamPushByte (&Ctx->Stream, Byte, &Pkt, &Ready)) {
          Ctx->FramingErrors++;
        } else if (Ready) {
          HandleDecodedPacket (Ctx, &Pkt, NowMs, Events);
          AuroraMtpStreamConsume (&Ctx->Stream);
        }
      }
      Budget -= 4u;
    }
    while (Budget > 0u) {
      UINT32             Val  = MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_RX8));
      UINT8              Byte = (UINT8)((Val >> 8) & 0xFFu);
      AURORA_MTP_PACKET  Pkt;
      BOOLEAN            Ready = FALSE;

      if (!AuroraMtpStreamPushByte (&Ctx->Stream, Byte, &Pkt, &Ready)) {
        Ctx->FramingErrors++;
      } else if (Ready) {
        HandleDecodedPacket (Ctx, &Pkt, NowMs, Events);
        AuroraMtpStreamConsume (&Ctx->Stream);
      }
      Budget--;
    }
  }
}

static BOOLEAN
MtpSendCommCmdAndWaitAck (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     const UINT8         *CmdPayload,
  IN     UINTN               CmdLen,
  IN     UINT32              TimeoutMs
  )
{
  UINT8   PktBuf[128];
  UINTN   PktLen = 0;
  UINT8   Seq    = Ctx->TxSeq[AURORA_MTP_IFACE_COMM]++;
  UINT32  StartMs;

  if (!AuroraMtpBuildCommandPacket (
         Seq,
         AURORA_MTP_IFACE_COMM,
         2u,  // Feature report
         0u,  // Set report
         CmdPayload,
         CmdLen,
         PktBuf,
         sizeof (PktBuf),
         &PktLen
         )) {
    return FALSE;
  }

  Ctx->AckWaiting     = TRUE;
  Ctx->AckExpectedSeq = Seq;
  Ctx->AckExpectedCmd = CmdPayload[0];
  Ctx->AckReceived    = FALSE;
  Ctx->AckReturnCode  = 0xFFFFFFFFu;

  if (!MtpTxPacket (Ctx, PktBuf, PktLen)) {
    Ctx->AckWaiting = FALSE;
    return FALSE;
  }

  StartMs = UefiNowMs ();
  while ((UefiNowMs () - StartMs) < TimeoutMs) {
    MtpDrainRx (Ctx, UefiNowMs (), NULL);
    if (Ctx->AckReceived) {
      Ctx->AckWaiting = FALSE;
      return (Ctx->AckReturnCode == 0u);
    }
    MicroSecondDelay (1000);
  }
  Ctx->AckWaiting = FALSE;
  return FALSE;
}

static BOOLEAN
MtpSendEnableInterface (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     UINT8               TargetIface
  )
{
  UINT8  Cmd[2];
  Cmd[0] = AURORA_MTP_CMD_ENABLE;
  Cmd[1] = TargetIface;
  return MtpSendCommCmdAndWaitAck (Ctx, Cmd, sizeof (Cmd), 800u);
}

static BOOLEAN
MtpSendFirmwareCmd (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     UINT8               TargetIface,
  IN     UINT64              Dva,
  IN     UINT32              Size
  )
{
  UINT8  Cmd[16];
  Cmd[0] = AURORA_MTP_CMD_SEND_FW;
  Cmd[1] = 2u;
  Cmd[2] = 0u;
  Cmd[3] = TargetIface;
  MtpWrLe64 (Cmd + 4u, Dva);
  MtpWrLe32 (Cmd + 12u, Size);
  return MtpSendCommCmdAndWaitAck (Ctx, Cmd, sizeof (Cmd), 1200u);
}

static BOOLEAN
MtpSendResetInterface (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     UINT8               TargetIface,
  IN     UINT8               State,
  IN     UINT8               Method,
  IN     UINT8               Phase
  )
{
  UINT8  Cmd[9];
  Cmd[0] = AURORA_MTP_CMD_RESET;
  Cmd[1] = Method;
  Cmd[2] = TargetIface;
  Cmd[3] = State;
  if (Method == AURORA_MTP_POWER_DIRECT) {
    return MtpSendCommCmdAndWaitAck (Ctx, Cmd, 4u, 800u);
  }
  Cmd[4] = Phase;
  MtpWrLe32 (Cmd + 5u, 0u);
  return MtpSendCommCmdAndWaitAck (Ctx, Cmd, 9u, 800u);
}

BOOLEAN
AuroraMtpHwInit (
  OUT AURORA_MTP_CONTEXT  *Ctx
  )
{
  dt_node_t    *Chosen;
  dt_node_t    *DockNode;
  CONST UINT8  *DisabledProp;
  UINTN        PropLen = 0;
  UINT64       RegBase = 0;
  UINT64       RegSize = 0;
  UINT32       StartMs;
  UINT32       LastPacketMs;
  UINT32       LastCount;

  ZeroMem (Ctx, sizeof (*Ctx));
  AsciiStrCpyS (Ctx->FwSource, sizeof (Ctx->FwSource), "NONE");

  // Respect Aura's explicit disable flag if set
  Chosen = dt_get ("/chosen");
  if (Chosen != NULL) {
    DisabledProp = dt_node_prop (Chosen, AURA_MTP_DISABLED_PROPERTY, &PropLen);
    if (DisabledProp != NULL && PropLen == sizeof (UINT32) && MtpRdLe32 (DisabledProp) == 1u) {
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] Disabled by /chosen/%a\r\n", AURA_MTP_DISABLED_PROPERTY));
      return FALSE;
    }
  }

  // Locate /arm-io/dockchannel-mtp MMIO registers from live ADT
  DockNode = dt_get ("/arm-io/dockchannel-mtp");
  if (DockNode == NULL) {
    DEBUG ((DEBUG_ERROR, "[AURORA MTP] /arm-io/dockchannel-mtp absent in ADT\r\n"));
    return FALSE;
  }

  if (dt_node_reg (DockNode, 1, &RegBase, &RegSize) == 0 && RegSize >= 0x1000u) {
    Ctx->IrqBase = RegBase;
  }
  if (dt_node_reg (DockNode, 2, &RegBase, &RegSize) == 0 && RegSize >= 0x1000u) {
    Ctx->CfgBase = RegBase;
  }
  if (dt_node_reg (DockNode, 3, &RegBase, &RegSize) == 0 && RegSize >= 0x1000u) {
    Ctx->DataBase = RegBase;
  } else if (Ctx->CfgBase != 0ULL) {
    Ctx->DataBase = Ctx->CfgBase + 0x4000ULL;
  }

  if (Ctx->IrqBase == 0ULL || Ctx->CfgBase == 0ULL || Ctx->DataBase == 0ULL) {
    DEBUG ((DEBUG_ERROR, "[AURORA MTP] Incomplete dockchannel MMIO regs\r\n"));
    return FALSE;
  }

  Ctx->StagingVa   = AURA_J813_MTP_STAGING_BASE;
  Ctx->StagingDva  = AURORA_J813_MTP_STAGING_DVA;
  Ctx->StagingSize = (UINT32)AURA_J813_MTP_STAGING_SIZE;

  // Mask channel-1 IRQ while UEFI polls DockChannel directly
  {
    UINT32  Mask = MmioRead32 ((UINTN)(Ctx->IrqBase + AURORA_DC_IRQ_MASK));
    MmioWrite32 ((UINTN)(Ctx->IrqBase + AURORA_DC_IRQ_MASK), Mask & ~0x0Cu);
    MmioWrite32 ((UINTN)(Ctx->IrqBase + AURORA_DC_IRQ_FLAG), 0x0Cu);
    MmioWrite32 ((UINTN)(Ctx->CfgBase + AURORA_DC_CFG_RX_THRESH), 1u);
  }

  DEBUG ((
    DEBUG_ERROR,
    "[AURORA MTP] Probing DockChannel irq=0x%Lx cfg=0x%Lx data=0x%Lx rx=%u\r\n",
    Ctx->IrqBase,
    Ctx->CfgBase,
    Ctx->DataBase,
    MmioRead32 ((UINTN)(Ctx->DataBase + AURORA_DC_DATA_RX_COUNT))
    ));

  // Step 1: Drain initial INIT (0xF0) packets emitted by prebooted MTP RTKit
  StartMs      = UefiNowMs ();
  LastPacketMs = StartMs;
  LastCount    = 0;
  while ((UefiNowMs () - StartMs) < 900u) {
    MtpDrainRx (Ctx, UefiNowMs (), NULL);
    if (Ctx->PacketsRx != LastCount) {
      LastCount    = Ctx->PacketsRx;
      LastPacketMs = UefiNowMs ();
    }
    if (Ctx->Stm.InitComplete && Ctx->Keyboard.InitComplete && Ctx->MultiTouch.InitComplete) {
      break;
    }
    if ((Ctx->Stm.InitComplete || Ctx->Keyboard.InitComplete) &&
        (UefiNowMs () - LastPacketMs) >= 250u) {
      break;
    }
    MicroSecondDelay (2000);
  }

  if (!Ctx->Stm.Present && !Ctx->Keyboard.Present && !Ctx->MultiTouch.Present) {
    DEBUG ((DEBUG_ERROR, "[AURORA MTP] No INIT interfaces seen (rxPkts=%u errs=%u)\r\n",
            Ctx->PacketsRx, Ctx->FramingErrors));
    return FALSE;
  }

  Ctx->Available = TRUE;

  // Step 2: Enable STM first (required ordering barrier on Apple MTP firmware)
  if (Ctx->Stm.Present) {
    if (MtpSendEnableInterface (Ctx, Ctx->Stm.Interface)) {
      Ctx->Stm.Enabled = TRUE;
      StartMs = UefiNowMs ();
      while (!Ctx->Stm.Ready && (UefiNowMs () - StartMs) < 800u) {
        MtpDrainRx (Ctx, UefiNowMs (), NULL);
        MicroSecondDelay (2000);
      }
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] STM iface=%u enabled ready=%u\r\n",
              Ctx->Stm.Interface, Ctx->Stm.Ready ? 1u : 0u));
    } else {
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] STM iface=%u enable failed\r\n", Ctx->Stm.Interface));
    }
  }

  // Step 3: Enable built-in Keyboard (requires ZERO firmware!)
  if (Ctx->Keyboard.Present) {
    if (MtpSendEnableInterface (Ctx, Ctx->Keyboard.Interface)) {
      Ctx->Keyboard.Enabled = TRUE;
      Ctx->Keyboard.Ready   = TRUE;
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] KEYBOARD iface=%u LIVE!\r\n", Ctx->Keyboard.Interface));
    } else {
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] KEYBOARD iface=%u enable failed\r\n", Ctx->Keyboard.Interface));
    }
  }

  // Step 4: Load tpmtfw.bin (ESP or Windows NTFS) & bootstrap MultiTouch Trackpad!
  if (Ctx->MultiTouch.Present) {
    UINT8  *FwContainer = AllocatePool (AURORA_MTP_FW_MAX_CONTAINER);
    UINTN  FwLen        = 0;

    if (FwContainer != NULL &&
        LoadTrackpadFirmwareContainer (
          FwContainer,
          AURORA_MTP_FW_MAX_CONTAINER,
          &FwLen,
          Ctx->FwSource,
          sizeof (Ctx->FwSource)
          )) {
      UINTN  PayloadLen = 0;

      if (AuroraMtpPrepareFirmware (
            FwContainer,
            FwLen,
            Ctx->MultiTouch.Interface,
            (UINT8 *)(UINTN)Ctx->StagingVa,
            Ctx->StagingSize,
            &PayloadLen
            )) {
        WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Ctx->StagingVa, PayloadLen);
        MemoryFence ();
        Ctx->FwLoaded      = TRUE;
        Ctx->FwPayloadSize = (UINT32)PayloadLen;

        DEBUG ((
          DEBUG_ERROR,
          "[AURORA MTP] Trackpad FW loaded (%u bytes from %a) -> staging 0x%Lx (DVA 0x%Lx)\r\n",
          Ctx->FwPayloadSize,
          Ctx->FwSource,
          Ctx->StagingVa,
          Ctx->StagingDva
          ));

        // Exact M5 transactional MultiTouch bootstrap:
        // ENABLE -> SEND_FIRMWARE -> RESET(0,WILL) -> RESET(0,HAS) -> RESET(2,WILL) -> 50ms -> RESET(2,HAS) -> READY
        {
          UINT8    Method = Ctx->MultiTouch.HasGpio ? AURORA_MTP_POWER_DIRECT : AURORA_MTP_POWER_TRANSACT;
          BOOLEAN  Ok;

          Ok = MtpSendEnableInterface (Ctx, Ctx->MultiTouch.Interface);
          if (Ok) {
            Ok = MtpSendFirmwareCmd (Ctx, Ctx->MultiTouch.Interface, Ctx->StagingDva, Ctx->FwPayloadSize);
          }
          if (Ok) {
            Ok = MtpSendResetInterface (Ctx, Ctx->MultiTouch.Interface, 0u, Method, AURORA_MTP_PHASE_WILL);
          }
          if (Ok && Method == AURORA_MTP_POWER_TRANSACT) {
            Ok = MtpSendResetInterface (Ctx, Ctx->MultiTouch.Interface, 0u, Method, AURORA_MTP_PHASE_HAS);
          }
          if (Ok) {
            Ok = MtpSendResetInterface (Ctx, Ctx->MultiTouch.Interface, 2u, Method, AURORA_MTP_PHASE_WILL);
          }
          if (Ok && Method == AURORA_MTP_POWER_TRANSACT) {
            // Pump RX during 50 ms settle window
            UINT32  SettleStart = UefiNowMs ();
            while ((UefiNowMs () - SettleStart) < AURORA_MTP_POWER_SETTLE_MS) {
              MtpDrainRx (Ctx, UefiNowMs (), NULL);
              MicroSecondDelay (2000);
            }
            Ok = MtpSendResetInterface (Ctx, Ctx->MultiTouch.Interface, 2u, Method, AURORA_MTP_PHASE_HAS);
          }

          if (Ok) {
            Ctx->MultiTouch.Enabled = TRUE;
            StartMs = UefiNowMs ();
            while (!Ctx->MultiTouch.Ready && (UefiNowMs () - StartMs) < 1500u) {
              MtpDrainRx (Ctx, UefiNowMs (), NULL);
              MicroSecondDelay (2000);
            }
            DEBUG ((
              DEBUG_ERROR,
              "[AURORA MTP] TRACKPAD iface=%u bootstrap OK ready=%u!\r\n",
              Ctx->MultiTouch.Interface,
              Ctx->MultiTouch.Ready ? 1u : 0u
              ));
          } else {
            DEBUG ((DEBUG_ERROR, "[AURORA MTP] TRACKPAD iface=%u bootstrap cmd failed\r\n",
                    Ctx->MultiTouch.Interface));
          }
        }
      } else {
        DEBUG ((DEBUG_ERROR, "[AURORA MTP] Trackpad FW container invalid\r\n"));
      }
    } else {
      DEBUG ((DEBUG_ERROR, "[AURORA MTP] Trackpad FW (tpmtfw.bin) not found on ESP or NTFS; keyboard still active\r\n"));
    }

    if (FwContainer != NULL) {
      FreePool (FwContainer);
    }
  }

  Ctx->Initialized = (Ctx->Keyboard.Ready || Ctx->MultiTouch.Ready || Ctx->MultiTouch.Enabled);
  return Ctx->Initialized;
}

VOID
AuroraMtpHwPoll (
  IN OUT AURORA_MTP_CONTEXT  *Ctx,
  IN     UINT32              NowMs,
  IN OUT AURORA_BIOS_STATE   *BiosState
  )
{
  AURORA_MTP_INPUT_EVENTS  Events;
  UINT32                   I;

  if (Ctx == NULL || !Ctx->Available || BiosState == NULL) {
    return;
  }

  ZeroMem (&Events, sizeof (Events));
  MtpDrainRx (Ctx, NowMs, &Events);
  AuroraMtpTickKeyboardRepeat (&Ctx->KbState, NowMs, &Events);

  for (I = 0; I < Events.KeyCount; I++) {
    AuroraBiosHandleKey (BiosState, (AURORA_BIOS_KEY)Events.Keys[I]);
  }

  if (Events.HasPointerEvent) {
    if (Events.DeltaX != 0 || Events.DeltaY != 0 ||
        Events.LeftDown != BiosState->MouseLeftDown) {
      AuroraBiosHandleMouseDelta (BiosState, Events.DeltaX, Events.DeltaY, Events.LeftDown);
    }
  }
  if (Events.TapPulse) {
    // Clean synthetic tap-to-click: press then immediate release
    AuroraBiosHandleMouseDelta (BiosState, 0, 0, TRUE);
    AuroraBiosHandleMouseDelta (BiosState, 0, 0, FALSE);
  }

  // Update live telemetry snapshot
  BiosState->Info.MtpActive    = Ctx->Available;
  BiosState->Info.MtpKbReady   = Ctx->Keyboard.Ready;
  BiosState->Info.MtpTpReady   = (Ctx->MultiTouch.Ready || Ctx->TpReports > 0u);
  BiosState->Info.MtpFwLoaded  = Ctx->FwLoaded;
  BiosState->Info.MtpPacketsRx = Ctx->PacketsRx;
  BiosState->Info.MtpKbReports = Ctx->KbReports;
  BiosState->Info.MtpTpReports = Ctx->TpReports;
}

// ---------------------------------------------------------------------------
// Apple Silicon FPWM Keyboard Backlight Controller (J813 /arm-io/fpwm0)
// ---------------------------------------------------------------------------
#define PMGR_AUTO_ENABLE  (1u << 28)
#define PMGR_PARENT_OFF   (1u << 11)
#define PMGR_DEV_DISABLE  (1u << 10)
#define PMGR_WAS_CLKGATED (1u << 9)
#define PMGR_WAS_PWRGATED (1u << 8)
#define PMGR_PS_ACTUAL(v) (((v) >> 4) & 0xFu)
#define PMGR_PS_TARGET(v) ((v) & 0xFu)
#define PMGR_PS_ACTIVE    0xFu

static BOOLEAN
PmgrEnablePsReg (
  IN UINT64  PsAddr
  )
{
  UINT32  Val;
  UINT32  Iter;

  if (PsAddr == 0ULL) {
    return FALSE;
  }
  Val = MmioRead32 ((UINTN)PsAddr);
  if ((Val & PMGR_DEV_DISABLE) != 0u) {
    return FALSE;
  }
  if (PMGR_PS_ACTUAL (Val) == PMGR_PS_ACTIVE && PMGR_PS_TARGET (Val) == PMGR_PS_ACTIVE) {
    return TRUE;
  }

  Val &= ~(PMGR_AUTO_ENABLE | PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED | 0xFu);
  Val |= PMGR_PS_ACTIVE;
  MmioWrite32 ((UINTN)PsAddr, Val);

  for (Iter = 0; Iter < 2000u; Iter++) {
    Val = MmioRead32 ((UINTN)PsAddr);
    if (PMGR_PS_ACTUAL (Val) == PMGR_PS_ACTIVE) {
      return TRUE;
    }
    if ((Val & PMGR_PARENT_OFF) != 0u) {
      return FALSE;
    }
    MicroSecondDelay (10);
  }
  return FALSE;
}

// Resolve a PMGR device ID (from clock-gates) to its physical PS register address
// using /arm-io/pmgr's ps-groups (T8142/M5) or ps-regs (older Apple Silicon).
static BOOLEAN
ResolvePmgrPsRegFromAdt (
  IN  UINT16   DevId,
  OUT UINT64   *PsAddrOut,
  OUT UINT16   *ParentIdOut,
  OUT BOOLEAN  *IsVirtualOut
  )
{
  dt_node_t    *PmgrNode;
  CONST UINT8  *Devs;
  UINTN        DevsLen = 0;
  CONST UINT8  *Groups;
  UINTN        GroupsLen = 0;
  BOOLEAN      UseGroups = FALSE;
  UINTN        Off;

  *PsAddrOut    = 0ULL;
  *ParentIdOut  = 0u;
  *IsVirtualOut = FALSE;

  PmgrNode = dt_get ("/arm-io/pmgr");
  if (PmgrNode == NULL) {
    return FALSE;
  }
  Devs = dt_node_prop (PmgrNode, "devices", &DevsLen);
  if (Devs == NULL || DevsLen < 48u) {
    return FALSE;
  }

  Groups = dt_node_prop (PmgrNode, "ps-groups", &GroupsLen);
  if (Groups != NULL && GroupsLen >= 12u) {
    UseGroups = TRUE;
  } else {
    Groups = dt_node_prop (PmgrNode, "ps-regs", &GroupsLen);
    if (Groups == NULL || GroupsLen < 12u) {
      return FALSE;
    }
  }

  for (Off = 0; Off + 48u <= DevsLen; Off += 48u) {
    const UINT8  *Ent = Devs + Off;
    UINT16       Id2  = MtpRdLe16 (Ent + 26u);
    if (Id2 != DevId) {
      continue;
    }
    *IsVirtualOut = ((Ent[0] & 0x10u) != 0u);
    *ParentIdOut  = MtpRdLe16 (Ent + 4u);

    {
      UINT32  GrpIdx;
      UINT32  RegOff;
      UINT32  BaseRegIdx;
      UINT32  BaseRegOff;
      UINT64  PmgrBase = 0;

      if (UseGroups) {
        UINT32  Go = MtpRdLe32 (Ent + 16u);
        RegOff = Go & 0x00FFFFFFu;
        GrpIdx = (Go >> 24) & 0xFFu;
      } else {
        RegOff = ((UINT32)Ent[10]) << 3;
        GrpIdx = Ent[11];
      }

      if (((UINTN)GrpIdx + 1u) * 12u > GroupsLen) {
        return FALSE;
      }
      BaseRegIdx = MtpRdLe32 (Groups + (UINTN)GrpIdx * 12u);
      BaseRegOff = MtpRdLe32 (Groups + (UINTN)GrpIdx * 12u + 4u);
      if (dt_node_reg (PmgrNode, BaseRegIdx, &PmgrBase, NULL) != 0) {
        return FALSE;
      }
      *PsAddrOut = PmgrBase + (UINT64)BaseRegOff + (UINT64)RegOff;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
AuroraKblHwInit (
  OUT AURORA_KBL_CONTEXT  *Kbl,
  IN  UINT32              InitialBrightness
  )
{
  dt_node_t    *PwmNode;
  CONST UINT8  *Gates;
  UINTN        GatesLen = 0;
  UINT64       PwmBase  = 0;
  UINT64       PwmSize  = 0;
  UINT16       GateId;
  UINT64       LeafPs   = 0;
  UINT16       ParentId = 0;
  BOOLEAN      LeafVirt = FALSE;

  ZeroMem (Kbl, sizeof (*Kbl));

  PwmNode = dt_get ("/arm-io/fpwm0");
  if (PwmNode == NULL) {
    PwmNode = dt_get ("/arm-io/pwm0");
  }
  if (PwmNode == NULL) {
    DEBUG ((DEBUG_ERROR, "[AURORA KBL] No /arm-io/fpwm0 node in ADT\r\n"));
    return FALSE;
  }
  if (dt_node_reg (PwmNode, 0, &PwmBase, &PwmSize) != 0 || PwmBase == 0ULL || PwmSize < 0x20u) {
    DEBUG ((DEBUG_ERROR, "[AURORA KBL] /arm-io/fpwm0 reg[0] invalid\r\n"));
    return FALSE;
  }

  Gates = dt_node_prop (PwmNode, "clock-gates", &GatesLen);
  if (Gates != NULL && GatesLen >= 4u) {
    GateId = (UINT16)MtpRdLe32 (Gates);
    if (ResolvePmgrPsRegFromAdt (GateId, &LeafPs, &ParentId, &LeafVirt)) {
      // Enable non-virtual parent first if needed
      if (ParentId != 0u) {
        UINT64   ParentPs   = 0;
        UINT16   GrandId    = 0;
        BOOLEAN  ParentVirt = FALSE;
        if (ResolvePmgrPsRegFromAdt (ParentId, &ParentPs, &GrandId, &ParentVirt) &&
            !ParentVirt && ParentPs != 0ULL) {
          Kbl->ParentPsAddr = ParentPs;
          (VOID)PmgrEnablePsReg (ParentPs);
        }
      }
      Kbl->LeafPsAddr = LeafPs;
      if (!LeafVirt && !PmgrEnablePsReg (LeafPs)) {
        DEBUG ((DEBUG_ERROR, "[AURORA KBL] PMGR PS 0x%Lx enable failed\r\n", LeafPs));
        return FALSE;
      }
    }
  }

  Kbl->PwmBase   = PwmBase;
  Kbl->Available = TRUE;
  AuroraKblHwSetBrightness (Kbl, InitialBrightness);
  DEBUG ((
    DEBUG_ERROR,
    "[AURORA KBL] Keyboard Backlight LIVE at fpwm=0x%Lx ps=0x%Lx brightness=%u/255\r\n",
    Kbl->PwmBase,
    Kbl->LeafPsAddr,
    Kbl->CurrentBrightness
    ));
  return TRUE;
}

VOID
AuroraKblHwSetBrightness (
  IN OUT AURORA_KBL_CONTEXT  *Kbl,
  IN     UINT32              Brightness
  )
{
  UINT32  OnCycles  = 0;
  UINT32  OffCycles = 0;

  if (Kbl == NULL || !Kbl->Available || Kbl->PwmBase == 0ULL) {
    return;
  }
  if (Brightness > 255u) {
    Brightness = 255u;
  }

  if (Brightness == 0u) {
    MmioWrite32 ((UINTN)(Kbl->PwmBase + AURORA_KBL_FPWM_CTRL), AURORA_KBL_FPWM_CTRL_STOP);
  } else {
    AuroraKblComputeCycles (Brightness, &OnCycles, &OffCycles);
    // Apple FPWM hardware order: ON_CYCLES -> OFF_CYCLES -> CTRL (with UPDATE)
    MmioWrite32 ((UINTN)(Kbl->PwmBase + AURORA_KBL_FPWM_ON_CYCLES), OnCycles);
    MmioWrite32 ((UINTN)(Kbl->PwmBase + AURORA_KBL_FPWM_OFF_CYCLES), OffCycles);
    MmioWrite32 ((UINTN)(Kbl->PwmBase + AURORA_KBL_FPWM_CTRL), AURORA_KBL_FPWM_CTRL_RUN);
  }
  Kbl->CurrentBrightness = Brightness;
}

#endif  // !AURORA_BIOS_HOST_TEST

/** @file
  Comprehensive host unit tests (ASan/UBSan) for Aurora Silicon UEFI Setup:
  - Config CRC32 & validation
  - Gigabyte Easy Mode & Advanced Mode layout, mouse/keyboard interaction, live KBL action
  - DockChannel framing, MTP ACK padding quirk, HIDF firmware patching
  - Apple HID Keyboard usage translation + repeat + MultiTouch 0x75 delta & tap-to-click
  - FPWM keyboard backlight cycle calculation
  - Read-only NTFS reader over 4096-byte NVMe sectors (USA fixups, INDX, non-resident runs)
  - Optional PPM screenshot export for visual verification

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../AuroraBiosConfig.h"
#include "../AuroraMtpInput.h"
#include "../AuroraNtfsReader.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
WrLe16 (UINT8 *P, UINT16 V)
{
  P[0] = (UINT8)V;
  P[1] = (UINT8)(V >> 8);
}

static void
WrLe32 (UINT8 *P, UINT32 V)
{
  P[0] = (UINT8)V;
  P[1] = (UINT8)(V >> 8);
  P[2] = (UINT8)(V >> 16);
  P[3] = (UINT8)(V >> 24);
}

static void
WrLe64 (UINT8 *P, UINT64 V)
{
  WrLe32 (P, (UINT32)V);
  WrLe32 (P + 4, (UINT32)(V >> 32));
}

static void
TestConfigAndUi (
  const char  *PreviewDir
  )
{
  AURORA_BIOS_STATE  S;
  UINT32             *Fb;
  AURORA_BIOS_CONFIG Cfg;

  AuroraBiosConfigSetDefaults (&Cfg);
  assert (AuroraBiosConfigValidate (&Cfg));
  assert (Cfg.KbdBacklightLevel == 3u);
  assert (AuroraBiosKblLevelToPwm (Cfg.KbdBacklightLevel) == 192u);
  Cfg.FastBoot ^= 1u;
  assert (!AuroraBiosConfigValidate (&Cfg));
  AuroraBiosConfigFinalize (&Cfg);
  assert (AuroraBiosConfigValidate (&Cfg));

  AuroraBiosStateInit (&S, 2560, 1664);
  assert (S.Width == 1280 && S.Height == 800 && S.UiScale == 2 && S.TopInsetPx == 64);
  assert (S.Mode == AURORA_MODE_EASY);

  S.Info.CpuCores       = 10u;
  S.Info.TotalRamMiB    = 16384ULL;
  S.Info.GpuPoolActive  = TRUE;
  S.Info.PrimaryDiskGiB = 512ULL;
  S.Info.EspWritable    = TRUE;
  S.Info.MtpActive      = TRUE;
  S.Info.MtpKbReady     = TRUE;
  S.Info.MtpTpReady     = TRUE;
  S.Info.MtpFwLoaded    = TRUE;
  S.Info.KblActive      = TRUE;
  S.Info.MtpPacketsRx   = 184u;
  S.Info.MtpKbReports   = 12u;
  S.Info.MtpTpReports   = 148u;
  S.Info.UptimeSec      = 42u;
  strcpy (S.Info.MtpFwSource, "NTFS");

  S.BootEntryCount = 3u;
  strcpy (S.BootEntries[0].Description, "Windows Boot Manager");
  S.BootEntries[0].OptionNumber = 1u;
  strcpy (S.BootEntries[1].Description, "UEFI Internal Shell");
  S.BootEntries[1].OptionNumber = 2u;
  strcpy (S.BootEntries[2].Description, "EFI Apple NVMe Partition");
  S.BootEntries[2].OptionNumber = 3u;

  AuroraBiosBuildLayout (&S);
  assert (S.ControlCount >= 18);

  Fb = (UINT32 *)calloc ((size_t)S.Width * (size_t)S.Height, sizeof (UINT32));
  assert (Fb != NULL);

  // Render Easy Mode
  AuroraBiosRender (&S, Fb);
  AuroraBiosDrawCursor (Fb, S.Width, S.Height, 620, 410);

  if (PreviewDir != NULL) {
    char  Path[512];
    FILE  *F;
    snprintf (Path, sizeof (Path), "%s/easy_mode.ppm", PreviewDir);
    F = fopen (Path, "wb");
    if (F != NULL) {
      int  I;
      fprintf (F, "P6\n%d %d\n255\n", S.Width, S.Height);
      for (I = 0; I < S.Width * S.Height; I++) {
        UINT8  Rgb[3] = {
          (UINT8)((Fb[I] >> 16) & 0xFFu),
          (UINT8)((Fb[I] >> 8) & 0xFFu),
          (UINT8)(Fb[I] & 0xFFu)
        };
        fwrite (Rgb, 1, 3, F);
      }
      fclose (F);
    }
  }

  // Test switching to Advanced Mode via F2 key
  AuroraBiosHandleKey (&S, AURORA_KEY_F2);
  assert (S.Mode == AURORA_MODE_ADVANCED);
  assert (S.ActiveTab == AURORA_TAB_TWEAKER);

  // Navigate to Keyboard Backlight row (4th setting in Tweaker: index 13 + 3 = 16) and press Right
  S.FocusedCtrlIdx = 16;
  AuroraBiosHandleKey (&S, AURORA_KEY_RIGHT);
  assert (S.Config.KbdBacklightLevel == 4u);
  assert (S.PendingAction == AURORA_ACTION_SET_KBD_BACKLIGHT);
  assert (S.PendingActionArg == 255u);
  S.PendingAction = AURORA_ACTION_NONE;

  // Render Advanced Mode
  AuroraBiosRender (&S, Fb);
  AuroraBiosDrawCursor (Fb, S.Width, S.Height, 520, 310);

  if (PreviewDir != NULL) {
    char  Path[512];
    FILE  *F;
    snprintf (Path, sizeof (Path), "%s/advanced_mode.ppm", PreviewDir);
    F = fopen (Path, "wb");
    if (F != NULL) {
      int  I;
      fprintf (F, "P6\n%d %d\n255\n", S.Width, S.Height);
      for (I = 0; I < S.Width * S.Height; I++) {
        UINT8  Rgb[3] = {
          (UINT8)((Fb[I] >> 16) & 0xFFu),
          (UINT8)((Fb[I] >> 8) & 0xFFu),
          (UINT8)(Fb[I] & 0xFFu)
        };
        fwrite (Rgb, 1, 3, F);
      }
      fclose (F);
    }
  }

  // Test surgical dynamic-only rendering in Advanced + Easy Mode and hover SceneDirty
  {
    AURORA_DIRTY_RECT  DynRects[3];
    UINT32             DynCount;

    S.Info.UptimeSec = 75u;
    DynCount = AuroraBiosRenderDynamicOnly (&S, Fb, DynRects);
    assert (DynCount == 2u);
    assert (DynRects[0].Valid && DynRects[1].Valid);

    // Click back to Easy Mode tab
    AuroraBiosHandleMouseMove (&S, S.Controls[0].X + 40, S.Controls[0].Y + 15, TRUE);
    AuroraBiosHandleMouseMove (&S, S.Controls[0].X + 40, S.Controls[0].Y + 15, FALSE);
    assert (S.Mode == AURORA_MODE_EASY);

    // Verify hover sets SceneDirty so hovered buttons highlight immediately
    S.SceneDirty = FALSE;
    AuroraBiosHandleMouseMove (&S, S.Controls[1].X + 40, S.Controls[1].Y + 15, FALSE);
    assert (S.HoveredCtrlIdx == 1 && S.SceneDirty);

    // Verify Easy Mode dynamic-only update returns all 3 lightweight dirty rects
    DynCount = AuroraBiosRenderDynamicOnly (&S, Fb, DynRects);
    assert (DynCount == 3u);
    assert (DynRects[0].Valid && DynRects[1].Valid && DynRects[2].Valid);
  }

  free (Fb);
  printf ("  [PASS] Config CRC32, Easy Mode, Advanced Mode, live KBL action\n");
}

static void
TestMtpProtocolAndDecoders (
  void
  )
{
  // 1. Captured real Apple MTP ENABLE_INTERFACE ACK (non-zero alignment pad byte 0x02!)
  static const UINT8  CapturedAck[] = {
    0x08u, 0x11u, 0x0Cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x80u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0xB4u, 0x02u, 0x00u, 0x00u,
    0xC3u, 0xEBu, 0xF2u, 0xFFu
  };
  AURORA_MTP_STREAM        Stream;
  AURORA_MTP_PACKET        Pkt;
  BOOLEAN                  Ready = FALSE;
  size_t                   I;
  UINT8                    OutBuf[64];
  UINTN                    OutLen = 0;
  UINT8                    EnableCmd[2] = { AURORA_MTP_CMD_ENABLE, 2u };

  memset (&Stream, 0, sizeof (Stream));
  for (I = 0; I < sizeof (CapturedAck); I++) {
    assert (AuroraMtpStreamPushByte (&Stream, CapturedAck[I], &Pkt, &Ready));
    assert (Ready == (I + 1u == sizeof (CapturedAck)));
  }
  assert (Pkt.Channel == AURORA_DC_CHANNEL_CMD);
  assert (Pkt.PayloadLength == 1u && Pkt.Payload[0] == AURORA_MTP_CMD_ENABLE);
  assert (Pkt.ReturnCode == 0u);
  AuroraMtpStreamConsume (&Stream);

  // 2. Roundtrip command packet builder -> stream parser
  assert (AuroraMtpBuildCommandPacket (5u, 0u, 2u, 0u, EnableCmd, sizeof (EnableCmd), OutBuf, sizeof (OutBuf), &OutLen));
  assert (OutLen == 24u);
  for (I = 0; I < OutLen; I++) {
    assert (AuroraMtpStreamPushByte (&Stream, OutBuf[I], &Pkt, &Ready));
  }
  assert (Ready && Pkt.Sequence == 5u && Pkt.Payload[0] == AURORA_MTP_CMD_ENABLE && Pkt.Payload[1] == 2u);
  AuroraMtpStreamConsume (&Stream);

  // 3. HIDF Firmware container validation & interface-index patch
  {
    UINT8  Container[64];
    UINT8  Staging[64];
    UINTN  PayLen = 0;
    memset (Container, 0, sizeof (Container));
    WrLe32 (Container + 0, AURORA_MTP_FW_MAGIC);
    WrLe32 (Container + 4, AURORA_MTP_FW_VERSION);
    WrLe32 (Container + 8, 20u);
    WrLe32 (Container + 12, 16u);
    WrLe32 (Container + 16, 7u);
    Container[20 + 7] = 0xAAu;
    assert (AuroraMtpPrepareFirmware (Container, 36u, 3u, Staging, sizeof (Staging), &PayLen));
    assert (PayLen == 16u && Staging[7] == 3u);
  }

  // 4. Apple HID Keyboard report decode (Report ID 0x01 + Arrows + F2 + Repeat)
  {
    AURORA_MTP_KB_STATE      Kb;
    AURORA_MTP_INPUT_EVENTS  Ev;
    UINT8                    RptDown[9] = { 0x01u, 0x00u, 0x00u, 0x51u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };
    UINT8                    RptUp[9]   = { 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u };
    memset (&Kb, 0, sizeof (Kb));
    memset (&Ev, 0, sizeof (Ev));
    AuroraMtpDecodeKeyboardReport (&Kb, RptDown, sizeof (RptDown), TRUE, 100u, &Ev);
    assert (Ev.KeyCount == 1u && Ev.Keys[0] == AURORA_KEY_DOWN);

    // Repeat after 400 ms
    memset (&Ev, 0, sizeof (Ev));
    AuroraMtpTickKeyboardRepeat (&Kb, 500u, &Ev);
    assert (Ev.KeyCount == 1u && Ev.Keys[0] == AURORA_KEY_DOWN);

    // Release stops repeat
    memset (&Ev, 0, sizeof (Ev));
    AuroraMtpDecodeKeyboardReport (&Kb, RptUp, sizeof (RptUp), TRUE, 520u, &Ev);
    AuroraMtpTickKeyboardRepeat (&Kb, 1000u, &Ev);
    assert (Ev.KeyCount == 0u);
  }

  // 5. Apple MTP MultiTouch 0x75 Trackpad finger delta + inverted Y + tap-to-click
  {
    AURORA_MTP_TP_STATE      Tp;
    AURORA_MTP_INPUT_EVENTS  Ev;
    UINT8                    F1[1 + 38 + 30];
    UINT8                    F2[1 + 38 + 30];
    UINT8                    F3[1 + 38];
    memset (&Tp, 0, sizeof (Tp));
    memset (F1, 0, sizeof (F1));
    memset (F2, 0, sizeof (F2));
    memset (F3, 0, sizeof (F3));
    F1[0] = 0x75u; F1[1 + 22] = 1u; F1[1 + 23] = 0u;
    WrLe16 (F1 + 1 + 38 + 4, (UINT16)(INT16)100);
    WrLe16 (F1 + 1 + 38 + 6, (UINT16)(INT16)200);
    WrLe16 (F1 + 1 + 38 + 18, 400u);

    F2[0] = 0x75u; F2[1 + 22] = 1u; F2[1 + 23] = 0u;
    WrLe16 (F2 + 1 + 38 + 4, (UINT16)(INT16)160); // +60 / 4 -> +15 px
    WrLe16 (F2 + 1 + 38 + 6, (UINT16)(INT16)230); // +30 raw Y / 4 -> -7 px screen Y
    WrLe16 (F2 + 1 + 38 + 18, 400u);

    F3[0] = 0x75u; F3[1 + 22] = 0u; F3[1 + 23] = 0u;

    memset (&Ev, 0, sizeof (Ev));
    assert (AuroraMtpDecodeTrackpadReport (&Tp, F1, sizeof (F1), 1000u, &Ev));
    assert (Ev.DeltaX == 0 && Ev.DeltaY == 0);

    memset (&Ev, 0, sizeof (Ev));
    assert (AuroraMtpDecodeTrackpadReport (&Tp, F2, sizeof (F2), 1050u, &Ev));
    assert (Ev.DeltaX == 15 && Ev.DeltaY == -7 && !Ev.LeftDown);

    // Quick lift (< 220 ms, dist < 260) triggers tap-to-click pulse!
    memset (&Ev, 0, sizeof (Ev));
    assert (AuroraMtpDecodeTrackpadReport (&Tp, F3, sizeof (F3), 1090u, &Ev));
    assert (Ev.TapPulse);
  }

  // 6. FPWM Keyboard Backlight math
  {
    UINT32  On, Off;
    AuroraKblComputeCycles (192u, &On, &Off);
    assert (On + Off == 960u && On > 700u && On < 750u);
    AuroraKblComputeCycles (255u, &On, &Off);
    assert (On == 960u && Off == 0u);
  }

  // 7. UEFI -> Windows MTP Preboot Handoff record build/validate/tamper check
  {
    AURORA_MTP_CONTEXT          Ctx;
    AURORA_MTP_PREBOOT_HANDOFF  Handoff;
    memset (&Ctx, 0, sizeof (Ctx));
    Ctx.Stm.Ready        = TRUE;
    Ctx.Keyboard.Ready   = TRUE;
    Ctx.MultiTouch.Ready = TRUE;
    Ctx.TxSeq[0]         = 7u;
    Ctx.TxSeq[1]         = 3u;
    Ctx.CapturedInitLen  = sizeof (CapturedAck);
    memcpy (Ctx.CapturedInit, CapturedAck, sizeof (CapturedAck));
    assert (AuroraMtpBuildPrebootHandoff (&Ctx, &Handoff));
    assert (Handoff.Magic == AURORA_MTP_HANDOFF_MAGIC);
    assert (Handoff.Flags == (AURORA_MTP_HANDOFF_FLAG_STM_READY |
                              AURORA_MTP_HANDOFF_FLAG_KB_READY |
                              AURORA_MTP_HANDOFF_FLAG_MT_READY));
    assert (Handoff.TxSeq[0] == 7u && Handoff.TxSeq[1] == 3u);
    assert (AuroraMtpValidatePrebootHandoff (&Handoff));
    Handoff.InitStream[0] ^= 0xFFu;
    assert (!AuroraMtpValidatePrebootHandoff (&Handoff));
  }

  printf ("  [PASS] MTP DockChannel protocol, HIDF, Keyboard, Trackpad, FPWM KBL, OS Handoff\n");
}

// ---------------------------------------------------------------------------
// Synthetic 4096-Byte-Sector NTFS Disk Image Test for AuroraNtfsReadFile
// ---------------------------------------------------------------------------
typedef struct {
  UINT8   *Image;
  size_t  Size;
} MEM_DISK;

static BOOLEAN
MemDiskRead (
  VOID    *Ctx,
  UINT64  Off,
  UINTN   Len,
  VOID    *Out
  )
{
  MEM_DISK  *D = (MEM_DISK *)Ctx;
  if (Off + Len > D->Size) return FALSE;
  memcpy (Out, D->Image + Off, Len);
  return TRUE;
}

static void
InstallUsaFixup (
  UINT8   *Rec,
  UINT32  RecSize,
  UINT16  UsaOff,
  UINT16  Usn
  )
{
  UINT16  Count = (UINT16)(1u + RecSize / 512u);
  UINT16  I;
  WrLe16 (Rec + 4, UsaOff);
  WrLe16 (Rec + 6, Count);
  WrLe16 (Rec + UsaOff, Usn);
  for (I = 1; I < Count; I++) {
    UINT32  EndOff = (UINT32)I * 512u - 2u;
    Rec[UsaOff + I * 2u]      = Rec[EndOff];
    Rec[UsaOff + I * 2u + 1u] = Rec[EndOff + 1u];
    WrLe16 (Rec + EndOff, Usn);
  }
}

static void
AddIndexEntry (
  UINT8       *Dst,
  UINT32      *Pos,
  UINT64      MftIdx,
  const char  *Name
  )
{
  size_t  NLen = strlen (Name);
  UINT16  StreamLen = (UINT16)(66u + NLen * 2u);
  UINT16  EntryLen  = (UINT16)((16u + StreamLen + 7u) & ~7u);
  size_t  I;
  UINT8   *E = Dst + *Pos;

  memset (E, 0, EntryLen);
  WrLe64 (E + 0, MftIdx);
  WrLe16 (E + 8, EntryLen);
  WrLe16 (E + 10, StreamLen);
  WrLe16 (E + 12, 0u);
  E[16 + 64] = (UINT8)NLen;
  for (I = 0; I < NLen; I++) {
    WrLe16 (E + 16 + 66 + I * 2u, (UINT16)(UINT8)Name[I]);
  }
  *Pos += EntryLen;
}

static void
AddLastIndexEntry (
  UINT8   *Dst,
  UINT32  *Pos
  )
{
  UINT8  *E = Dst + *Pos;
  memset (E, 0, 16u);
  WrLe16 (E + 8, 16u);
  WrLe16 (E + 12, 0x0002u);  // Last entry flag
  *Pos += 16u;
}

static void
TestNtfsReader (
  void
  )
{
  // Create a 256 KiB synthetic NTFS partition (64 clusters x 4096 bytes)
  // Cluster 4..7: $MFT (16 records x 1024 bytes)
  // Cluster 10: INDX block for "Windows" -> "System32" -> "drivers" -> "tpmtfw.bin"
  // Cluster 20..21 + Cluster 25: non-resident 2-extent HIDF file (6000 bytes)
  const size_t  DiskSz = 64u * 4096u;
  UINT8         *Img   = (UINT8 *)calloc (DiskSz, 1u);
  MEM_DISK      Disk   = { Img, DiskSz };
  UINT8         *Mft   = Img + 4u * 4096u;
  UINT8         Expected[6000];
  UINT8         ReadBack[8192];
  UINTN         ReadLen = 0;
  size_t        I;

  assert (Img != NULL);

  // BPB at sector 0 (4096-byte sectors, 1 sector/cluster = 4096-byte clusters!)
  memcpy (Img + 3, "NTFS    ", 8);
  WrLe16 (Img + 0x0B, 4096u);
  Img[0x0D] = 1u;
  WrLe64 (Img + 0x30, 4ULL);  // MFT starts at LCN 4
  Img[0x40] = (UINT8)(INT8)(-10);  // 1024-byte MFT records
  Img[0x44] = 1u;                  // 4096-byte INDX blocks

  // MFT Record 0 ($MFT): non-resident $DATA spanning 4 clusters at LCN 4
  {
    UINT8  *R = Mft + 0 * 1024u;
    UINT8  *A = R + 56u;
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    WrLe32 (A + 0, 0x80u);
    WrLe32 (A + 4, 72u);
    A[8] = 1u;
    WrLe16 (A + 32, 64u);
    WrLe64 (A + 48, 16u * 1024u);
    A[64] = 0x11u; A[65] = 4u; A[66] = 4u; A[67] = 0u;
    WrLe32 (A + 72, 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x1234u);
  }

  // MFT Record 5 (Root "\"): resident $INDEX_ROOT containing "Windows" -> MFT 6
  {
    UINT8   *R   = Mft + 5u * 1024u;
    UINT8   *A   = R + 56u;
    UINT8   *Pay = A + 24u;
    UINT32  Pos  = 32u;
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    AddIndexEntry (Pay, &Pos, 6ULL, "Windows");
    AddLastIndexEntry (Pay, &Pos);
    WrLe32 (Pay + 16, 16u);          // entries offset from node header
    WrLe32 (Pay + 20, Pos - 16u);    // total length of index node
    WrLe32 (A + 0, 0x90u);
    WrLe32 (A + 4, (UINT32)((24u + Pos + 7u) & ~7u));
    A[8] = 0u;
    WrLe32 (A + 16, Pos);
    WrLe16 (A + 20, 24u);
    WrLe32 (A + ((24u + Pos + 7u) & ~7u), 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x2222u);
  }

  // MFT Record 6 ("Windows"): non-resident $INDEX_ALLOCATION at LCN 10 containing "System32" -> MFT 7
  {
    UINT8   *R   = Mft + 6u * 1024u;
    UINT8   *A   = R + 56u;
    UINT8   *Idx = Img + 10u * 4096u;
    UINT32  Pos  = 64u;  // 24 (INDX hdr) + 16 (node hdr) + 18 (USA 9*2) + 6 pad
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    WrLe32 (A + 0, 0xA0u);
    WrLe32 (A + 4, 72u);
    A[8] = 1u;
    WrLe16 (A + 32, 64u);
    WrLe64 (A + 48, 4096u);
    A[64] = 0x11u; A[65] = 1u; A[66] = 10u; A[67] = 0u;
    WrLe32 (A + 72, 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x3333u);

    memcpy (Idx, "INDX", 4);
    AddIndexEntry (Idx, &Pos, 7ULL, "System32");
    AddLastIndexEntry (Idx, &Pos);
    WrLe32 (Idx + 24, 40u);
    WrLe32 (Idx + 28, Pos - 24u);
    InstallUsaFixup (Idx, 4096u, 40u, 0x4444u);
  }

  // MFT Record 7 ("System32"): resident $INDEX_ROOT containing "drivers" -> MFT 8
  {
    UINT8   *R   = Mft + 7u * 1024u;
    UINT8   *A   = R + 56u;
    UINT8   *Pay = A + 24u;
    UINT32  Pos  = 32u;
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    AddIndexEntry (Pay, &Pos, 8ULL, "drivers");
    AddLastIndexEntry (Pay, &Pos);
    WrLe32 (Pay + 16, 16u);
    WrLe32 (Pay + 20, Pos - 16u);
    WrLe32 (A + 0, 0x90u);
    WrLe32 (A + 4, (UINT32)((24u + Pos + 7u) & ~7u));
    A[8] = 0u;
    WrLe32 (A + 16, Pos);
    WrLe16 (A + 20, 24u);
    WrLe32 (A + ((24u + Pos + 7u) & ~7u), 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x5555u);
  }

  // MFT Record 8 ("drivers"): resident $INDEX_ROOT containing "tpmtfw.bin" -> MFT 9
  {
    UINT8   *R   = Mft + 8u * 1024u;
    UINT8   *A   = R + 56u;
    UINT8   *Pay = A + 24u;
    UINT32  Pos  = 32u;
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    AddIndexEntry (Pay, &Pos, 9ULL, "tpmtfw.bin");
    AddLastIndexEntry (Pay, &Pos);
    WrLe32 (Pay + 16, 16u);
    WrLe32 (Pay + 20, Pos - 16u);
    WrLe32 (A + 0, 0x90u);
    WrLe32 (A + 4, (UINT32)((24u + Pos + 7u) & ~7u));
    A[8] = 0u;
    WrLe32 (A + 16, Pos);
    WrLe16 (A + 20, 24u);
    WrLe32 (A + ((24u + Pos + 7u) & ~7u), 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x6666u);
  }

  // Build expected 6000-byte payload split across LCN 20 (1 cluster = 4096 B) + LCN 25 (1 cluster, delta +5)
  for (I = 0; I < sizeof (Expected); I++) {
    Expected[I] = (UINT8)((I * 37u + 13u) & 0xFFu);
  }
  WrLe32 (Expected, AURORA_MTP_FW_MAGIC);
  memcpy (Img + 20u * 4096u, Expected, 4096u);
  memcpy (Img + 25u * 4096u, Expected + 4096u, sizeof (Expected) - 4096u);

  // MFT Record 9 ("tpmtfw.bin"): non-resident 2-run $DATA (LCN 20 len 1, then +5 = LCN 25 len 1)
  {
    UINT8  *R = Mft + 9u * 1024u;
    UINT8  *A = R + 56u;
    memcpy (R, "FILE", 4);
    WrLe16 (R + 20, 56u);
    WrLe32 (A + 0, 0x80u);
    WrLe32 (A + 4, 80u);
    A[8] = 1u;
    WrLe16 (A + 32, 64u);
    WrLe64 (A + 48, sizeof (Expected));
    A[64] = 0x11u; A[65] = 1u; A[66] = 20u;
    A[67] = 0x11u; A[68] = 1u; A[69] = 5u;  // delta +5 -> LCN 25
    A[70] = 0x00u;
    WrLe32 (A + 80, 0xFFFFFFFFu);
    InstallUsaFixup (R, 1024u, 48u, 0x7777u);
  }

  // Test case-insensitive nested lookup & multi-extent readback!
  assert (AuroraNtfsReadFile (
            MemDiskRead, &Disk, DiskSz,
            "windows\\SYSTEM32\\Drivers\\TPMTFW.BIN",
            ReadBack, sizeof (ReadBack), &ReadLen));
  assert (ReadLen == sizeof (Expected));
  assert (memcmp (ReadBack, Expected, sizeof (Expected)) == 0);

  free (Img);
  printf ("  [PASS] Read-only NTFS reader (4096B sectors, USA fixups, INDX, multi-run)\n");
}

int
main (
  int   argc,
  char  **argv
  )
{
  const char  *PreviewDir = (argc > 1) ? argv[1] : NULL;
  printf ("Running AuroraBios host test suite...\n");
  TestConfigAndUi (PreviewDir);
  TestMtpProtocolAndDecoders ();
  TestNtfsReader ();
  printf ("ALL AURORA BIOS HOST TESTS PASSED!\n");
  return 0;
}

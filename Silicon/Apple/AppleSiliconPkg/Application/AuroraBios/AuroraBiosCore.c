/** @file
  Aurora Silicon UEFI Setup - Core UI State, Gigabyte-Style Easy/Advanced
  Layout, Dirty-Rect Anti-Aliased Software Renderer & Input Controller.

  Pure C99 freestanding implementation (dual-compiled for AArch64 UEFI and
  host unit tests with ASan/UBSan).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AuroraBiosConfig.h"
#include "AuroraBiosFont.h"

#ifdef AURORA_BIOS_HOST_TEST
#include <stdio.h>
#include <string.h>
#else
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#endif

// ---------------------------------------------------------------------------
// Gigabyte-Inspired Molten Orange / Obsidian Palette (0x00RRGGBB)
// ---------------------------------------------------------------------------
#define COL_BLACK         0x00000000u
#define COL_BG_CARD       0x00050505u
#define COL_BG_ROW_ALT    0x000A0A0Au
#define COL_BG_ROW_HOV    0x001D1008u
#define COL_BG_ROW_FOC    0x00261409u
#define COL_BORDER        0x002C2C2Cu
#define COL_BORDER_HI     0x00464646u
#define COL_DIVIDER       0x001E1E1Eu
#define COL_ORANGE        0x00F26522u
#define COL_ORANGE_HI     0x00FF8533u
#define COL_ORANGE_DEEP   0x007A2906u
#define COL_ORANGE_DARK   0x00240C02u
#define COL_WHITE         0x00FFFFFFu
#define COL_TEXT_PRI      0x00E6E6E6u
#define COL_TEXT_SEC      0x00A6A6A6u
#define COL_TEXT_MUTED    0x006E6E6Eu
#define COL_GREEN         0x0022C55Eu
#define COL_AMBER         0x00F59E0Bu
#define COL_RED           0x00EF4444u

// Logical Control IDs
enum {
  CTRL_MODE_EASY = 10,
  CTRL_MODE_ADVANCED,

  CTRL_UTIL_HELP = 20,
  CTRL_UTIL_INPUT,
  CTRL_UTIL_DEFAULTS,
  CTRL_UTIL_SAVE_EXIT,
  CTRL_UTIL_SHELL,
  CTRL_UTIL_BOOT_OS,

  CTRL_ADV_TAB_BASE = 40, // +0..4

  // Easy Mode Quick-Link & Action Buttons
  CTRL_EASY_POWER_BTN = 60,
  CTRL_EASY_QA_SHELL,
  CTRL_EASY_QA_SYSINFO,
  CTRL_EASY_QA_BOOT,

  // Settings Controls (shared IDs across Easy & Advanced Mode!)
  CTRL_SET_PERF_PROFILE = 100,
  CTRL_SET_DIRECT_SCANOUT,
  CTRL_SET_GPU_POOL,
  CTRL_SET_KBD_BACKLIGHT,
  CTRL_SET_SCALE_MODE,
  CTRL_SET_MOUSE_SPEED,
  CTRL_SET_FAST_BOOT,
  CTRL_SET_SPLASH_LOGO,
  CTRL_SET_DEFAULT_TARGET,
  CTRL_SET_BOOT_TIMEOUT,
  CTRL_SET_SERIAL_DEBUG,
  CTRL_SET_USB_AUTO,
  CTRL_SET_EXT_USB_BOOT,
  CTRL_SET_ENTER_ONCE,

  // Action Buttons
  CTRL_BTN_BOOT_UP = 200,
  CTRL_BTN_BOOT_DOWN,
  CTRL_BTN_BOOT_NOW,
  CTRL_BTN_LAUNCH_SHELL,
  CTRL_BTN_SCAN_USB,
  CTRL_BTN_SAVE_EXIT,
  CTRL_BTN_SAVE_ONLY,
  CTRL_BTN_DISCARD_EXIT,
  CTRL_BTN_LOAD_DEFAULTS,
  CTRL_BTN_SAVE_REBOOT,
  CTRL_BTN_POWER_OFF,

  CTRL_BOOT_ROW_BASE = 300
};

// ---------------------------------------------------------------------------
// Portable String / Memory Helpers
// ---------------------------------------------------------------------------
static VOID
BiosZero (
  VOID   *Ptr,
  UINTN  Size
  )
{
#ifdef AURORA_BIOS_HOST_TEST
  memset (Ptr, 0, Size);
#else
  ZeroMem (Ptr, Size);
#endif
}

static VOID
BiosCopy (
  VOID        *Dst,
  const VOID  *Src,
  UINTN       Size
  )
{
#ifdef AURORA_BIOS_HOST_TEST
  memcpy (Dst, Src, Size);
#else
  CopyMem (Dst, Src, Size);
#endif
}

static VOID
BiosStrCpy (
  char        *Dst,
  UINTN       DstSize,
  const char  *Src
  )
{
  UINTN  I = 0;

  if (Dst == NULL || DstSize == 0) {
    return;
  }
  if (Src != NULL) {
    while (I + 1 < DstSize && Src[I] != '\0') {
      Dst[I] = Src[I];
      I++;
    }
  }
  Dst[I] = '\0';
}

static VOID
BiosFmtU32 (
  char    *Dst,
  UINTN   DstSize,
  UINT32  Val
  )
{
  char   Tmp[16];
  UINTN  N = 0;
  UINTN  I = 0;

  if (DstSize == 0) {
    return;
  }
  if (Val == 0) {
    Tmp[N++] = '0';
  } else {
    while (Val > 0 && N < sizeof (Tmp)) {
      Tmp[N++] = (char)('0' + (Val % 10u));
      Val /= 10u;
    }
  }
  while (N > 0 && I + 1 < DstSize) {
    Dst[I++] = Tmp[--N];
  }
  Dst[I] = '\0';
}

static VOID
BiosFmtI32 (
  char   *Dst,
  UINTN  DstSize,
  INT32  Val
  )
{
  if (DstSize < 2) {
    if (DstSize == 1) Dst[0] = '\0';
    return;
  }
  if (Val < 0) {
    Dst[0] = '-';
    BiosFmtU32 (Dst + 1, DstSize - 1, (UINT32)(-(INT64)Val));
  } else {
    BiosFmtU32 (Dst, DstSize, (UINT32)Val);
  }
}

static VOID
BiosStrCat (
  char        *Dst,
  UINTN       DstSize,
  const char  *Src
  )
{
  UINTN  Len = 0;
  UINTN  I   = 0;

  if (Dst == NULL || Src == NULL || DstSize == 0) {
    return;
  }
  while (Len < DstSize && Dst[Len] != '\0') {
    Len++;
  }
  while (Len + 1 < DstSize && Src[I] != '\0') {
    Dst[Len++] = Src[I++];
  }
  if (Len < DstSize) {
    Dst[Len] = '\0';
  }
}

static VOID
BiosFmt2D (
  char    *Dst,
  UINT32  V
  )
{
  Dst[0] = (char)('0' + ((V / 10u) % 10u));
  Dst[1] = (char)('0' + (V % 10u));
  Dst[2] = '\0';
}

// ---------------------------------------------------------------------------
// Backlight Level Mapping & Configuration CRC32
// ---------------------------------------------------------------------------
UINT32
AuroraBiosKblLevelToPwm (
  UINT8  Level
  )
{
  static const UINT32  Table[5] = { 0u, 64u, 128u, 192u, 255u };
  return (Level < 5u) ? Table[Level] : 192u;
}

UINT32
AuroraBiosCrc32 (
  const VOID  *Data,
  UINTN       Length
  )
{
  const UINT8  *Bytes = (const UINT8 *)Data;
  UINT32       Crc    = 0xFFFFFFFFu;
  UINTN        I;
  UINT32       Bit;

  for (I = 0; I < Length; I++) {
    Crc ^= (UINT32)Bytes[I];
    for (Bit = 0; Bit < 8u; Bit++) {
      if ((Crc & 1u) != 0u) {
        Crc = (Crc >> 1) ^ 0xEDB88320u;
      } else {
        Crc >>= 1;
      }
    }
  }
  return ~Crc;
}

VOID
AuroraBiosConfigSetDefaults (
  AURORA_BIOS_CONFIG  *Cfg
  )
{
  BiosZero (Cfg, sizeof (*Cfg));
  Cfg->Magic              = AURORA_BIOS_CONFIG_MAGIC;
  Cfg->Version            = AURORA_BIOS_CONFIG_VERSION;
  Cfg->PerformanceProfile = AURORA_PROFILE_BALANCED;
  Cfg->BootTimeoutSec     = 3u;
  Cfg->FastBoot           = 0u;
  Cfg->BootSplashLogo     = 1u;
  Cfg->DefaultBootTarget  = AURORA_BOOT_TARGET_OS;
  Cfg->GpuLocalPoolEnable = 1u;
  Cfg->ConsoleScaleMode   = 2u;
  Cfg->MouseSpeed         = 5u;
  Cfg->SerialDebugLevel   = AURORA_DEBUG_INFO;
  Cfg->UsbHidAutoConnect  = 0u;
  Cfg->ExternalUsbBoot    = 0u;
  Cfg->EnterBiosOnce      = 0u;
  Cfg->KbdBacklightLevel  = 3u;  // 75% crisp keyboard glow by default
  Cfg->DirectScanout      = 1u;
  AuroraBiosConfigFinalize (Cfg);
}

VOID
AuroraBiosConfigFinalize (
  AURORA_BIOS_CONFIG  *Cfg
  )
{
  Cfg->Magic   = AURORA_BIOS_CONFIG_MAGIC;
  Cfg->Version = AURORA_BIOS_CONFIG_VERSION;
  Cfg->Crc32   = 0u;
  Cfg->Crc32   = AuroraBiosCrc32 (Cfg, AURORA_OFFSET_OF (AURORA_BIOS_CONFIG, Crc32));
}

BOOLEAN
AuroraBiosConfigValidate (
  const AURORA_BIOS_CONFIG  *Cfg
  )
{
  UINT32  Expected;

  if (Cfg == NULL) {
    return FALSE;
  }
  if (Cfg->Magic != AURORA_BIOS_CONFIG_MAGIC || Cfg->Version != AURORA_BIOS_CONFIG_VERSION) {
    return FALSE;
  }
  Expected = AuroraBiosCrc32 (Cfg, AURORA_OFFSET_OF (AURORA_BIOS_CONFIG, Crc32));
  if (Expected != Cfg->Crc32) {
    return FALSE;
  }
  if (Cfg->PerformanceProfile > AURORA_PROFILE_QUIET ||
      Cfg->BootTimeoutSec > 10u ||
      Cfg->DefaultBootTarget > AURORA_BOOT_TARGET_BIOS ||
      Cfg->ConsoleScaleMode < 1u || Cfg->ConsoleScaleMode > 2u ||
      Cfg->MouseSpeed < 1u || Cfg->MouseSpeed > 10u ||
      Cfg->SerialDebugLevel > AURORA_DEBUG_VERBOSE ||
      Cfg->KbdBacklightLevel > 4u) {
    return FALSE;
  }
  return TRUE;
}

UINT32
AuroraBiosCountModified (
  const AURORA_BIOS_STATE  *State
  )
{
  UINT32                     Count = 0;
  const AURORA_BIOS_CONFIG   *A    = &State->Config;
  const AURORA_BIOS_CONFIG   *B    = &State->SavedConfig;

  if (A->PerformanceProfile != B->PerformanceProfile) Count++;
  if (A->BootTimeoutSec != B->BootTimeoutSec)         Count++;
  if (A->FastBoot != B->FastBoot)                     Count++;
  if (A->BootSplashLogo != B->BootSplashLogo)         Count++;
  if (A->DefaultBootTarget != B->DefaultBootTarget)   Count++;
  if (A->GpuLocalPoolEnable != B->GpuLocalPoolEnable) Count++;
  if (A->ConsoleScaleMode != B->ConsoleScaleMode)     Count++;
  if (A->MouseSpeed != B->MouseSpeed)                 Count++;
  if (A->SerialDebugLevel != B->SerialDebugLevel)     Count++;
  if (A->UsbHidAutoConnect != B->UsbHidAutoConnect)   Count++;
  if (A->ExternalUsbBoot != B->ExternalUsbBoot)       Count++;
  if (A->EnterBiosOnce != B->EnterBiosOnce)           Count++;
  if (A->KbdBacklightLevel != B->KbdBacklightLevel)   Count++;
  if (A->DirectScanout != B->DirectScanout)           Count++;
  if (State->BootOrderDirty)                          Count++;
  return Count;
}

// ---------------------------------------------------------------------------
// Dirty Rect & State Management
// ---------------------------------------------------------------------------
static VOID
MarkDirtyRect (
  AURORA_BIOS_STATE  *State,
  INT32              X0,
  INT32              Y0,
  INT32              X1,
  INT32              Y1
  )
{
  if (X0 < 0) X0 = 0;
  if (Y0 < 0) Y0 = 0;
  if (X1 > State->Width)  X1 = State->Width;
  if (Y1 > State->Height) Y1 = State->Height;
  if (X0 >= X1 || Y0 >= Y1) {
    return;
  }
  if (!State->Dirty.Valid) {
    State->Dirty.Valid = TRUE;
    State->Dirty.MinX  = X0;
    State->Dirty.MinY  = Y0;
    State->Dirty.MaxX  = X1;
    State->Dirty.MaxY  = Y1;
  } else {
    if (X0 < State->Dirty.MinX) State->Dirty.MinX = X0;
    if (Y0 < State->Dirty.MinY) State->Dirty.MinY = Y0;
    if (X1 > State->Dirty.MaxX) State->Dirty.MaxX = X1;
    if (Y1 > State->Dirty.MaxY) State->Dirty.MaxY = Y1;
  }
}

static VOID
MarkFullDirty (
  AURORA_BIOS_STATE  *State
  )
{
  State->FullRedrawNeeded = TRUE;
  MarkDirtyRect (State, 0, 0, State->Width, State->Height);
}

VOID
AuroraBiosSetStatus (
  AURORA_BIOS_STATE  *State,
  const char         *Message,
  UINT32             ColorRgb
  )
{
  BiosStrCpy (State->StatusMsg, sizeof (State->StatusMsg), Message);
  State->StatusColor = ColorRgb;
  MarkFullDirty (State);
}

VOID
AuroraBiosStateInit (
  AURORA_BIOS_STATE  *State,
  INT32              GopWidth,
  INT32              GopHeight
  )
{
  BiosZero (State, sizeof (*State));

  // On Apple Silicon notched Retina panels (e.g. 2560x1664 = 64px notch + 2560x1600,
  // 3024x1964 = 74px notch + 3024x1890, 3456x2234 = 74px notch + 3456x2160), keep the
  // notch strip pure black so the entire BIOS header sits cleanly below the notch!
  State->TopInsetPx = 0;
  if (GopWidth == 2560 && GopHeight == 1664) {
    State->TopInsetPx = 64;
  } else if ((GopWidth == 3024 && GopHeight == 1964) ||
             (GopWidth == 3456 && GopHeight == 2234)) {
    State->TopInsetPx = 74;
  } else if (GopHeight > 1600 && (GopHeight % 100) != 0 && (GopHeight % 64) == 0) {
    State->TopInsetPx = 64;
  }

  {
    INT32  UsableH = GopHeight - State->TopInsetPx;
    if (GopWidth >= 2000 && UsableH >= 1200) {
      State->UiScale = 2;
      State->Width   = GopWidth / 2;
      State->Height  = UsableH / 2;
    } else {
      State->UiScale = 1;
      State->Width   = (GopWidth >= 640) ? GopWidth : 1280;
      State->Height  = (UsableH >= 480) ? UsableH : 800;
    }
  }

  AuroraBiosConfigSetDefaults (&State->Config);
  BiosCopy (&State->SavedConfig, &State->Config, sizeof (State->Config));

  State->Info.GopWidth  = (UINT32)GopWidth;
  State->Info.GopHeight = (UINT32)GopHeight;
  State->Info.GopBpp    = 32u;
  BiosStrCpy (State->Info.PlatformName, sizeof (State->Info.PlatformName), "Apple Mac17,3 (J813)");
  BiosStrCpy (State->Info.SocName, sizeof (State->Info.SocName), "Apple M5 (T8142) 10-Core");
  BiosStrCpy (State->Info.FirmwareVersion, sizeof (State->Info.FirmwareVersion), "Aurora Mu 2026");
  BiosStrCpy (State->Info.MtpFwSource, sizeof (State->Info.MtpFwSource), "NONE");

  State->Mode           = AURORA_MODE_EASY;
  State->ActiveTab      = AURORA_TAB_TWEAKER;
  State->FocusedCtrlIdx = -1;
  State->HoveredCtrlIdx = -1;
  State->MouseX         = State->Width / 2;
  State->MouseY         = State->Height / 2;
  State->PrevMouseX     = State->MouseX;
  State->PrevMouseY     = State->MouseY;
  State->MouseVisible   = TRUE;

  AuroraBiosSetStatus (
    State,
    "Ready  |  Use Trackpad / Mouse or Keyboard (F2: Easy/Advanced Mode, F10: Save & Exit)",
    COL_TEXT_SEC
    );
  AuroraBiosBuildLayout (State);
  MarkFullDirty (State);
}

// ---------------------------------------------------------------------------
// Layout Builder (Gigabyte 1:1 Easy Mode & Advanced Mode Geometry)
// ---------------------------------------------------------------------------
static VOID
AddCtrl (
  AURORA_BIOS_STATE  *State,
  INT32              X,
  INT32              Y,
  INT32              W,
  INT32              H,
  AURORA_CTRL_KIND   Kind,
  UINT32             Id,
  UINT32             Param
  )
{
  if (State->ControlCount >= AURORA_BIOS_MAX_CONTROLS) {
    return;
  }
  AURORA_UI_RECT  *R = &State->Controls[State->ControlCount++];
  R->X     = X;
  R->Y     = Y;
  R->W     = W;
  R->H     = H;
  R->Kind  = Kind;
  R->Id    = Id;
  R->Param = Param;
}

VOID
AuroraBiosBuildLayout (
  AURORA_BIOS_STATE  *State
  )
{
  INT32  W = State->Width;
  INT32  H = State->Height;
  INT32  U;

  State->ControlCount = 0;

  // 1. Top slanted Mode Tabs: EASY MODE / ADVANCED MODE (1:1 Gigabyte header)
  {
    INT32  TabW   = 226;
    INT32  TabH   = 38;
    INT32  StartX = (W / 2) - 48;
    AddCtrl (State, StartX, 5, TabW, TabH, AURORA_CTRL_MODE_TAB, CTRL_MODE_EASY, AURORA_MODE_EASY);
    AddCtrl (State, StartX + TabW + 12, 5, TabW, TabH, AURORA_CTRL_MODE_TAB, CTRL_MODE_ADVANCED, AURORA_MODE_ADVANCED);
  }

  // 2. Utility Action Strip (6 metallic beveled buttons across full width)
  {
    static const UINT32  UtilIds[6] = {
      CTRL_UTIL_HELP, CTRL_UTIL_INPUT, CTRL_UTIL_DEFAULTS,
      CTRL_UTIL_SAVE_EXIT, CTRL_UTIL_SHELL, CTRL_UTIL_BOOT_OS
    };
    INT32  Gap  = 12;
    INT32  BtnW = (W - 48 - 5 * Gap) / 6;
    for (U = 0; U < 6; U++) {
      AddCtrl (State, 24 + U * (BtnW + Gap), 66, BtnW, 28, AURORA_CTRL_UTIL_BTN, UtilIds[U], (UINT32)U);
    }
  }

  if (State->Mode == AURORA_MODE_EASY) {
    // Left Column: Memory & Display dropdown controls (bottom half of left col)
    INT32  LeftW   = 316;
    INT32  DropY   = H - 220;
    INT32  RowH    = 32;
    AddCtrl (State, 34, DropY + 0 * RowH, LeftW - 20, 26, AURORA_CTRL_CHOICE, CTRL_SET_PERF_PROFILE, 0);
    AddCtrl (State, 34, DropY + 1 * RowH, LeftW - 20, 26, AURORA_CTRL_CHOICE, CTRL_SET_KBD_BACKLIGHT, 0);
    AddCtrl (State, 34, DropY + 2 * RowH, LeftW - 20, 26, AURORA_CTRL_CHOICE, CTRL_SET_GPU_POOL, 0);
    AddCtrl (State, 34, DropY + 3 * RowH, LeftW - 20, 26, AURORA_CTRL_CHOICE, CTRL_SET_SCALE_MODE, 0);
    AddCtrl (State, 34, DropY + 4 * RowH, LeftW - 20, 26, AURORA_CTRL_CHOICE, CTRL_SET_MOUSE_SPEED, 0);

    // Top-Right Card: Aurora PerfDrive (5 Gigabyte orange toggle switches)
    {
      INT32  PdX = W - 324;
      INT32  PdW = 300;
      INT32  PdY = 152;
      AddCtrl (State, PdX + 10, PdY + 0 * 29, PdW - 20, 24, AURORA_CTRL_TOGGLE, CTRL_SET_DIRECT_SCANOUT, 0);
      AddCtrl (State, PdX + 10, PdY + 1 * 29, PdW - 20, 24, AURORA_CTRL_TOGGLE, CTRL_SET_GPU_POOL, 0);
      AddCtrl (State, PdX + 10, PdY + 2 * 29, PdW - 20, 24, AURORA_CTRL_TOGGLE, CTRL_SET_FAST_BOOT, 0);
      AddCtrl (State, PdX + 10, PdY + 3 * 29, PdW - 20, 24, AURORA_CTRL_TOGGLE, CTRL_SET_SPLASH_LOGO, 0);
      AddCtrl (State, PdX + 10, PdY + 4 * 29, PdW - 20, 24, AURORA_CTRL_TOGGLE, CTRL_SET_EXT_USB_BOOT, 0);
    }

    // Middle Row: Power & Thermal button + Boot Sequence rows
    {
      INT32  MidX  = 360;
      INT32  MidY  = 338;
      INT32  MidH  = 196;
      INT32  Col1W = 300;
      INT32  BootX = W - 324;
      INT32  BootW = 300;
      UINT32 MaxVis = (State->BootEntryCount < 4u) ? State->BootEntryCount : 4u;
      UINT32 B;

      AddCtrl (State, MidX + 16, MidY + MidH - 58, Col1W - 32, 40, AURORA_CTRL_BUTTON, CTRL_EASY_POWER_BTN, 0);
      for (B = 0; B < MaxVis; B++) {
        AddCtrl (State, BootX + 10, MidY + 72 + (INT32)B * 28, BootW - 20, 24, AURORA_CTRL_BOOT_ROW, CTRL_BOOT_ROW_BASE + B, B);
      }
    }

    // Bottom Wide Card: Quick Access (3 header buttons + 2x3 quick controls)
    {
      INT32  QaX   = 360;
      INT32  QaW   = W - QaX - 24;
      INT32  QaY   = 568;
      INT32  ColW  = (QaW - 40) / 3;
      INT32  BtnY  = QaY + 14;
      INT32  Row1Y = QaY + 66;
      INT32  Row2Y = QaY + 114;

      AddCtrl (State, QaX + 12 + 0 * (ColW + 8), BtnY, ColW, 28, AURORA_CTRL_BUTTON, CTRL_EASY_QA_SHELL, 0);
      AddCtrl (State, QaX + 12 + 1 * (ColW + 8), BtnY, ColW, 28, AURORA_CTRL_BUTTON, CTRL_EASY_QA_SYSINFO, 0);
      AddCtrl (State, QaX + 12 + 2 * (ColW + 8), BtnY, ColW, 28, AURORA_CTRL_BUTTON, CTRL_EASY_QA_BOOT, 0);

      // Row 1 controls
      AddCtrl (State, QaX + 12 + 0 * (ColW + 8), Row1Y, ColW, 28, AURORA_CTRL_TOGGLE, CTRL_SET_ENTER_ONCE, 0);
      AddCtrl (State, QaX + 12 + 1 * (ColW + 8), Row1Y, ColW, 28, AURORA_CTRL_CHOICE, CTRL_SET_DEFAULT_TARGET, 0);
      AddCtrl (State, QaX + 12 + 2 * (ColW + 8), Row1Y, ColW, 28, AURORA_CTRL_CHOICE, CTRL_SET_SERIAL_DEBUG, 0);

      // Row 2 controls
      AddCtrl (State, QaX + 12 + 0 * (ColW + 8), Row2Y, ColW, 28, AURORA_CTRL_TOGGLE, CTRL_SET_USB_AUTO, 0);
      AddCtrl (State, QaX + 12 + 1 * (ColW + 8), Row2Y, ColW, 28, AURORA_CTRL_CHOICE, CTRL_SET_BOOT_TIMEOUT, 0);
      AddCtrl (State, QaX + 12 + 2 * (ColW + 8), Row2Y, ColW, 28, AURORA_CTRL_CHOICE, CTRL_SET_KBD_BACKLIGHT, 1);
    }
  } else {
    // ADVANCED MODE: 5 horizontal Gigabyte sub-tabs
    INT32  SubY   = 106;
    INT32  SubW   = 156;
    INT32  PaneX  = 24;
    INT32  PaneW  = W - 384;
    INT32  RowY   = 166;
    INT32  RowH   = 42;

    for (U = 0; U < (INT32)AURORA_TAB_COUNT; U++) {
      AddCtrl (State, 24 + U * (SubW + 8), SubY, SubW, 30, AURORA_CTRL_TAB, CTRL_ADV_TAB_BASE + (UINT32)U, (UINT32)U);
    }

    switch (State->ActiveTab) {
      case AURORA_TAB_TWEAKER:
        AddCtrl (State, PaneX + 12, RowY + 0 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_PERF_PROFILE, 0);
        AddCtrl (State, PaneX + 12, RowY + 1 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_DIRECT_SCANOUT, 0);
        AddCtrl (State, PaneX + 12, RowY + 2 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_GPU_POOL, 0);
        AddCtrl (State, PaneX + 12, RowY + 3 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_KBD_BACKLIGHT, 0);
        AddCtrl (State, PaneX + 12, RowY + 4 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_SCALE_MODE, 0);
        AddCtrl (State, PaneX + 12, RowY + 5 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_MOUSE_SPEED, 0);
        AddCtrl (State, PaneX + 12, RowY + 6 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_SPLASH_LOGO, 0);
        break;

      case AURORA_TAB_SETTINGS:
        AddCtrl (State, PaneX + 12, RowY + 0 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_SERIAL_DEBUG, 0);
        AddCtrl (State, PaneX + 12, RowY + 1 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_USB_AUTO, 0);
        AddCtrl (State, PaneX + 12, RowY + 2 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_EXT_USB_BOOT, 0);
        AddCtrl (State, PaneX + 12, RowY + 3 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_FAST_BOOT, 0);
        AddCtrl (State, PaneX + 12, RowY + 4 * RowH, PaneW - 24, 34, AURORA_CTRL_TOGGLE, CTRL_SET_ENTER_ONCE, 0);
        AddCtrl (State, PaneX + 12, RowY + 5 * RowH + 8, 280, 34, AURORA_CTRL_BUTTON, CTRL_BTN_SCAN_USB, 0);
        AddCtrl (State, PaneX + 308, RowY + 5 * RowH + 8, 280, 34, AURORA_CTRL_BUTTON, CTRL_BTN_LOAD_DEFAULTS, 0);
        break;

      case AURORA_TAB_SYSINFO:
        AddCtrl (State, PaneX + 12, H - 112, 240, 34, AURORA_CTRL_BUTTON, CTRL_BTN_SCAN_USB, 0);
        AddCtrl (State, PaneX + 268, H - 112, 240, 34, AURORA_CTRL_BUTTON, CTRL_BTN_LAUNCH_SHELL, 0);
        break;

      case AURORA_TAB_BOOT: {
        UINT32  MaxVis = (State->BootEntryCount < 7u) ? State->BootEntryCount : 7u;
        UINT32  B;
        AddCtrl (State, PaneX + 12, RowY + 0 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_DEFAULT_TARGET, 0);
        AddCtrl (State, PaneX + 12, RowY + 1 * RowH, PaneW - 24, 34, AURORA_CTRL_CHOICE, CTRL_SET_BOOT_TIMEOUT, 0);
        for (B = 0; B < MaxVis; B++) {
          AddCtrl (State, PaneX + 12, RowY + 104 + (INT32)B * 36, PaneW - 24, 30, AURORA_CTRL_BOOT_ROW, CTRL_BOOT_ROW_BASE + B, B);
        }
        AddCtrl (State, PaneX + 12, H - 112, 160, 34, AURORA_CTRL_BUTTON, CTRL_BTN_BOOT_UP, 0);
        AddCtrl (State, PaneX + 184, H - 112, 160, 34, AURORA_CTRL_BUTTON, CTRL_BTN_BOOT_DOWN, 0);
        AddCtrl (State, PaneX + 356, H - 112, 220, 34, AURORA_CTRL_BUTTON, CTRL_BTN_BOOT_NOW, 0);
        AddCtrl (State, PaneX + 588, H - 112, 200, 34, AURORA_CTRL_BUTTON, CTRL_BTN_LAUNCH_SHELL, 0);
        break;
      }

      case AURORA_TAB_EXIT:
        AddCtrl (State, PaneX + 12, RowY + 0 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_SAVE_EXIT, 0);
        AddCtrl (State, PaneX + 12, RowY + 1 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_SAVE_ONLY, 0);
        AddCtrl (State, PaneX + 12, RowY + 2 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_DISCARD_EXIT, 0);
        AddCtrl (State, PaneX + 12, RowY + 3 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_LOAD_DEFAULTS, 0);
        AddCtrl (State, PaneX + 12, RowY + 4 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_SAVE_REBOOT, 0);
        AddCtrl (State, PaneX + 12, RowY + 5 * 48, PaneW - 24, 38, AURORA_CTRL_BUTTON, CTRL_BTN_POWER_OFF, 0);
        break;

      default:
        break;
    }
  }

  if (State->FocusedCtrlIdx >= State->ControlCount) {
    State->FocusedCtrlIdx = (State->ControlCount > 8) ? 8 : (State->ControlCount - 1);
  }
}

// ---------------------------------------------------------------------------
// Control Activation & Adjustment
// ---------------------------------------------------------------------------
static VOID
SwapBootEntries (
  AURORA_BIOS_STATE  *State,
  UINT32             A,
  UINT32             B
  )
{
  AURORA_BOOT_ENTRY  Tmp;

  if (A >= State->BootEntryCount || B >= State->BootEntryCount || A == B) {
    return;
  }
  BiosCopy (&Tmp, &State->BootEntries[A], sizeof (Tmp));
  BiosCopy (&State->BootEntries[A], &State->BootEntries[B], sizeof (Tmp));
  BiosCopy (&State->BootEntries[B], &Tmp, sizeof (Tmp));
  State->SelectedBootIdx = B;
  State->BootOrderDirty  = TRUE;
  State->PendingAction   = AURORA_ACTION_REORDER_BOOT;
  AuroraBiosSetStatus (State, "Boot sequence updated (Press F10 to Save & Exit)", COL_ORANGE_HI);
}

static VOID
ActivateControl (
  AURORA_BIOS_STATE  *State,
  INT32              CtrlIdx,
  INT32              Dir
  )
{
  if (CtrlIdx < 0 || CtrlIdx >= State->ControlCount) {
    return;
  }
  AURORA_UI_RECT  *R = &State->Controls[CtrlIdx];

  switch (R->Id) {
    case CTRL_MODE_EASY:
      if (State->Mode != AURORA_MODE_EASY) {
        State->Mode           = AURORA_MODE_EASY;
        State->FocusedCtrlIdx = 8;
        State->HoveredCtrlIdx = -1;
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      }
      return;

    case CTRL_MODE_ADVANCED:
      if (State->Mode != AURORA_MODE_ADVANCED) {
        State->Mode           = AURORA_MODE_ADVANCED;
        State->FocusedCtrlIdx = 13;
        State->HoveredCtrlIdx = -1;
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      }
      return;

    case CTRL_UTIL_HELP:
      State->ShowHelpModal = !State->ShowHelpModal;
      MarkFullDirty (State);
      return;

    case CTRL_UTIL_INPUT:
      State->Mode      = AURORA_MODE_ADVANCED;
      State->ActiveTab = AURORA_TAB_SYSINFO;
      AuroraBiosBuildLayout (State);
      AuroraBiosSetStatus (State, "Viewing live Hardware, MTP Input & Backlight status", COL_ORANGE_HI);
      return;

    case CTRL_UTIL_DEFAULTS:
    case CTRL_BTN_LOAD_DEFAULTS:
      AuroraBiosConfigSetDefaults (&State->Config);
      State->PendingAction    = AURORA_ACTION_SET_KBD_BACKLIGHT;
      State->PendingActionArg = AuroraBiosKblLevelToPwm (State->Config.KbdBacklightLevel);
      AuroraBiosSetStatus (State, "Loaded Optimized Defaults (F10 to Save & Exit)", COL_AMBER);
      return;

    case CTRL_UTIL_SAVE_EXIT:
    case CTRL_BTN_SAVE_EXIT:
      AuroraBiosConfigFinalize (&State->Config);
      State->PendingAction = AURORA_ACTION_SAVE_AND_BOOT;
      AuroraBiosSetStatus (State, "Saving settings and booting OS...", COL_GREEN);
      return;

    case CTRL_UTIL_SHELL:
    case CTRL_EASY_QA_SHELL:
    case CTRL_BTN_LAUNCH_SHELL:
      State->PendingAction = AURORA_ACTION_LAUNCH_SHELL;
      AuroraBiosSetStatus (State, "Launching UEFI Shell...", COL_ORANGE_HI);
      return;

    case CTRL_UTIL_BOOT_OS:
    case CTRL_BTN_DISCARD_EXIT:
      State->PendingAction = AURORA_ACTION_DISCARD_AND_BOOT;
      AuroraBiosSetStatus (State, "Exiting setup and booting OS...", COL_TEXT_PRI);
      return;

    case CTRL_EASY_POWER_BTN:
      State->Mode      = AURORA_MODE_ADVANCED;
      State->ActiveTab = AURORA_TAB_TWEAKER;
      AuroraBiosBuildLayout (State);
      MarkFullDirty (State);
      return;

    case CTRL_EASY_QA_SYSINFO:
      State->Mode      = AURORA_MODE_ADVANCED;
      State->ActiveTab = AURORA_TAB_SYSINFO;
      AuroraBiosBuildLayout (State);
      MarkFullDirty (State);
      return;

    case CTRL_EASY_QA_BOOT:
      State->Mode      = AURORA_MODE_ADVANCED;
      State->ActiveTab = AURORA_TAB_BOOT;
      AuroraBiosBuildLayout (State);
      MarkFullDirty (State);
      return;

    case CTRL_SET_PERF_PROFILE:
      State->Config.PerformanceProfile = (State->Config.PerformanceProfile + (Dir < 0 ? 2u : 1u)) % 3u;
      AuroraBiosSetStatus (State, "Performance Mode updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_DIRECT_SCANOUT:
      State->Config.DirectScanout = (State->Config.DirectScanout == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "Direct Scanout preference updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_GPU_POOL:
      State->Config.GpuLocalPoolEnable = (State->Config.GpuLocalPoolEnable == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "GPU Local Pool preference updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_KBD_BACKLIGHT: {
      INT32  Lvl = (INT32)State->Config.KbdBacklightLevel + (Dir < 0 ? -1 : 1);
      if (Lvl < 0) Lvl = 4;
      if (Lvl > 4) Lvl = 0;
      State->Config.KbdBacklightLevel = (UINT8)Lvl;
      State->PendingAction            = AURORA_ACTION_SET_KBD_BACKLIGHT;
      State->PendingActionArg         = AuroraBiosKblLevelToPwm (State->Config.KbdBacklightLevel);
      AuroraBiosSetStatus (State, "Keyboard Backlight brightness applied live", COL_ORANGE_HI);
      return;
    }

    case CTRL_SET_SCALE_MODE:
      State->Config.ConsoleScaleMode = (State->Config.ConsoleScaleMode == 2u) ? 1u : 2u;
      AuroraBiosSetStatus (State, "HiDPI Display Scale updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_MOUSE_SPEED: {
      INT32  Spd = (INT32)State->Config.MouseSpeed + (Dir < 0 ? -1 : 1);
      if (Spd < 1)  Spd = 10;
      if (Spd > 10) Spd = 1;
      State->Config.MouseSpeed = (UINT8)Spd;
      AuroraBiosSetStatus (State, "Pointer sensitivity updated", COL_ORANGE_HI);
      return;
    }

    case CTRL_SET_FAST_BOOT:
      State->Config.FastBoot = (State->Config.FastBoot == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "Fast Boot updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_SPLASH_LOGO:
      State->Config.BootSplashLogo = (State->Config.BootSplashLogo == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "Boot Splash Logo updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_DEFAULT_TARGET:
      State->Config.DefaultBootTarget = (UINT8)((State->Config.DefaultBootTarget + (Dir < 0 ? 2u : 1u)) % 3u);
      AuroraBiosSetStatus (State, "Default Boot Target updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_BOOT_TIMEOUT: {
      INT32  V = (INT32)State->Config.BootTimeoutSec + (Dir < 0 ? -1 : 1);
      if (V < 0)  V = 10;
      if (V > 10) V = 0;
      State->Config.BootTimeoutSec = (UINT32)V;
      AuroraBiosSetStatus (State, "Boot Timeout updated", COL_ORANGE_HI);
      return;
    }

    case CTRL_SET_SERIAL_DEBUG:
      State->Config.SerialDebugLevel = (UINT8)((State->Config.SerialDebugLevel + (Dir < 0 ? 2u : 1u)) % 3u);
      AuroraBiosSetStatus (State, "Serial Debug verbosity updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_USB_AUTO:
      State->Config.UsbHidAutoConnect = (State->Config.UsbHidAutoConnect == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "USB HID Auto-Scan preference updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_EXT_USB_BOOT:
      State->Config.ExternalUsbBoot = (State->Config.ExternalUsbBoot == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "External USB Boot preference updated", COL_ORANGE_HI);
      return;

    case CTRL_SET_ENTER_ONCE:
      State->Config.EnterBiosOnce = (State->Config.EnterBiosOnce == 0u) ? 1u : 0u;
      AuroraBiosSetStatus (State, "One-Shot Enter Setup on next boot toggled", COL_ORANGE_HI);
      return;

    case CTRL_BTN_BOOT_UP:
      if (State->SelectedBootIdx > 0u) {
        SwapBootEntries (State, State->SelectedBootIdx, State->SelectedBootIdx - 1u);
      }
      return;

    case CTRL_BTN_BOOT_DOWN:
      if (State->SelectedBootIdx + 1u < State->BootEntryCount) {
        SwapBootEntries (State, State->SelectedBootIdx, State->SelectedBootIdx + 1u);
      }
      return;

    case CTRL_BTN_BOOT_NOW:
      if (State->SelectedBootIdx < State->BootEntryCount) {
        State->PendingAction    = AURORA_ACTION_BOOT_OPTION;
        State->PendingActionArg = State->SelectedBootIdx;
        AuroraBiosSetStatus (State, "Booting selected UEFI entry...", COL_GREEN);
      }
      return;

    case CTRL_BTN_SCAN_USB:
      State->PendingAction = AURORA_ACTION_SCAN_USB_HID;
      AuroraBiosSetStatus (State, "Scanning input protocols...", COL_ORANGE_HI);
      return;

    case CTRL_BTN_SAVE_ONLY:
      AuroraBiosConfigFinalize (&State->Config);
      State->PendingAction = AURORA_ACTION_SAVE_CONFIG;
      return;

    case CTRL_BTN_SAVE_REBOOT:
      AuroraBiosConfigFinalize (&State->Config);
      State->PendingAction = AURORA_ACTION_SAVE_AND_REBOOT;
      AuroraBiosSetStatus (State, "Saving settings and rebooting...", COL_AMBER);
      return;

    case CTRL_BTN_POWER_OFF:
      State->PendingAction = AURORA_ACTION_POWER_OFF;
      AuroraBiosSetStatus (State, "Powering off system...", COL_RED);
      return;

    default:
      if (R->Kind == AURORA_CTRL_TAB) {
        State->ActiveTab      = (AURORA_BIOS_TAB)(R->Param % AURORA_TAB_COUNT);
        State->FocusedCtrlIdx = 13;
        State->HoveredCtrlIdx = -1;
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      } else if (R->Kind == AURORA_CTRL_BOOT_ROW) {
        UINT32  Idx = R->Param;
        if (Idx < State->BootEntryCount) {
          if (State->SelectedBootIdx == Idx &&
              State->LastClickBootIdx == Idx &&
              (State->TickCount - State->LastClickTick) <= 45u) {
            State->PendingAction    = AURORA_ACTION_BOOT_OPTION;
            State->PendingActionArg = Idx;
            AuroraBiosSetStatus (State, "Booting selected entry...", COL_GREEN);
          } else {
            State->SelectedBootIdx  = Idx;
            State->LastClickBootIdx = Idx;
            State->LastClickTick    = State->TickCount;
            MarkFullDirty (State);
          }
        }
      }
      return;
  }
}

static INT32
HitTestControl (
  const AURORA_BIOS_STATE  *State,
  INT32                    X,
  INT32                    Y
  )
{
  INT32  I;
  for (I = 0; I < State->ControlCount; I++) {
    const AURORA_UI_RECT  *R = &State->Controls[I];
    if (X >= R->X && X < R->X + R->W && Y >= R->Y && Y < R->Y + R->H) {
      return I;
    }
  }
  return -1;
}

VOID
AuroraBiosHandleKey (
  AURORA_BIOS_STATE  *State,
  AURORA_BIOS_KEY    Key
  )
{
  if (Key == AURORA_KEY_NONE) {
    return;
  }
  State->Info.KeyPresses++;
  State->Info.LastKeyCode = (UINT32)Key;

  if (State->ShowHelpModal) {
    State->ShowHelpModal = FALSE;
    MarkFullDirty (State);
    return;
  }

  switch (Key) {
    case AURORA_KEY_F1:
      State->ShowHelpModal = TRUE;
      MarkFullDirty (State);
      return;

    case AURORA_KEY_F2:
      State->Mode           = (State->Mode == AURORA_MODE_EASY) ? AURORA_MODE_ADVANCED : AURORA_MODE_EASY;
      State->FocusedCtrlIdx = (State->Mode == AURORA_MODE_EASY) ? 8 : 13;
      State->HoveredCtrlIdx = -1;
      AuroraBiosBuildLayout (State);
      MarkFullDirty (State);
      return;

    case AURORA_KEY_F3:
    case AURORA_KEY_F4:
    case AURORA_KEY_F5:
      State->Mode      = AURORA_MODE_ADVANCED;
      State->ActiveTab = (AURORA_BIOS_TAB)((Key - AURORA_KEY_F3 + 2) % AURORA_TAB_COUNT);
      State->FocusedCtrlIdx = 13;
      AuroraBiosBuildLayout (State);
      MarkFullDirty (State);
      return;

    case AURORA_KEY_F7:
    case AURORA_KEY_F9:
      AuroraBiosConfigSetDefaults (&State->Config);
      State->PendingAction    = AURORA_ACTION_SET_KBD_BACKLIGHT;
      State->PendingActionArg = AuroraBiosKblLevelToPwm (State->Config.KbdBacklightLevel);
      AuroraBiosSetStatus (State, "Loaded Optimized Defaults (Press F10 to Save & Exit)", COL_AMBER);
      return;

    case AURORA_KEY_F10:
      AuroraBiosConfigFinalize (&State->Config);
      State->PendingAction = AURORA_ACTION_SAVE_AND_BOOT;
      AuroraBiosSetStatus (State, "Saving settings and booting OS...", COL_GREEN);
      return;

    case AURORA_KEY_F11:
      State->PendingAction = AURORA_ACTION_LAUNCH_SHELL;
      AuroraBiosSetStatus (State, "Launching UEFI Shell...", COL_ORANGE_HI);
      return;

    case AURORA_KEY_ESC:
      if (State->Mode == AURORA_MODE_ADVANCED && State->ActiveTab != AURORA_TAB_EXIT) {
        State->ActiveTab = AURORA_TAB_EXIT;
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      } else {
        State->PendingAction = AURORA_ACTION_DISCARD_AND_BOOT;
        AuroraBiosSetStatus (State, "Exiting setup...", COL_TEXT_PRI);
      }
      return;

    case AURORA_KEY_TAB:
    case AURORA_KEY_DOWN: {
      INT32  Start = (State->Mode == AURORA_MODE_EASY) ? 8 : 13;
      if (State->ControlCount <= Start) Start = 0;
      if (State->FocusedCtrlIdx < Start || State->FocusedCtrlIdx + 1 >= State->ControlCount) {
        State->FocusedCtrlIdx = Start;
      } else {
        State->FocusedCtrlIdx++;
      }
      MarkFullDirty (State);
      return;
    }

    case AURORA_KEY_SHIFT_TAB:
    case AURORA_KEY_UP: {
      INT32  Start = (State->Mode == AURORA_MODE_EASY) ? 8 : 13;
      if (State->ControlCount <= Start) Start = 0;
      if (State->FocusedCtrlIdx <= Start) {
        State->FocusedCtrlIdx = State->ControlCount - 1;
      } else {
        State->FocusedCtrlIdx--;
      }
      MarkFullDirty (State);
      return;
    }

    case AURORA_KEY_LEFT:
      if (State->Mode == AURORA_MODE_ADVANCED &&
          (State->FocusedCtrlIdx < 13 || State->Controls[State->FocusedCtrlIdx].Kind == AURORA_CTRL_TAB)) {
        State->ActiveTab = (AURORA_BIOS_TAB)((State->ActiveTab + AURORA_TAB_COUNT - 1) % AURORA_TAB_COUNT);
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      } else if (State->FocusedCtrlIdx >= 0 && State->FocusedCtrlIdx < State->ControlCount) {
        AURORA_CTRL_KIND  K = State->Controls[State->FocusedCtrlIdx].Kind;
        if (K == AURORA_CTRL_CHOICE || K == AURORA_CTRL_TOGGLE) {
          ActivateControl (State, State->FocusedCtrlIdx, -1);
        } else if (State->Mode == AURORA_MODE_ADVANCED) {
          State->ActiveTab = (AURORA_BIOS_TAB)((State->ActiveTab + AURORA_TAB_COUNT - 1) % AURORA_TAB_COUNT);
          AuroraBiosBuildLayout (State);
          MarkFullDirty (State);
        }
      }
      return;

    case AURORA_KEY_RIGHT:
      if (State->Mode == AURORA_MODE_ADVANCED &&
          (State->FocusedCtrlIdx < 13 || State->Controls[State->FocusedCtrlIdx].Kind == AURORA_CTRL_TAB)) {
        State->ActiveTab = (AURORA_BIOS_TAB)((State->ActiveTab + 1) % AURORA_TAB_COUNT);
        AuroraBiosBuildLayout (State);
        MarkFullDirty (State);
      } else if (State->FocusedCtrlIdx >= 0 && State->FocusedCtrlIdx < State->ControlCount) {
        AURORA_CTRL_KIND  K = State->Controls[State->FocusedCtrlIdx].Kind;
        if (K == AURORA_CTRL_CHOICE || K == AURORA_CTRL_TOGGLE) {
          ActivateControl (State, State->FocusedCtrlIdx, +1);
        } else if (State->Mode == AURORA_MODE_ADVANCED) {
          State->ActiveTab = (AURORA_BIOS_TAB)((State->ActiveTab + 1) % AURORA_TAB_COUNT);
          AuroraBiosBuildLayout (State);
          MarkFullDirty (State);
        }
      }
      return;

    case AURORA_KEY_PLUS:
      ActivateControl (State, State->FocusedCtrlIdx, +1);
      return;

    case AURORA_KEY_MINUS:
      ActivateControl (State, State->FocusedCtrlIdx, -1);
      return;

    case AURORA_KEY_ENTER:
    case AURORA_KEY_SPACE:
      if (State->FocusedCtrlIdx >= 0) {
        ActivateControl (State, State->FocusedCtrlIdx, +1);
      }
      return;

    default:
      return;
  }
}

VOID
AuroraBiosHandleMouseMove (
  AURORA_BIOS_STATE  *State,
  INT32              NewX,
  INT32              NewY,
  BOOLEAN            LeftDown
  )
{
  INT32    Hit;
  BOOLEAN  Clicked;

  if (NewX < 0) NewX = 0;
  if (NewY < 0) NewY = 0;
  if (NewX >= State->Width)  NewX = State->Width - 1;
  if (NewY >= State->Height) NewY = State->Height - 1;

  if (NewX != State->MouseX || NewY != State->MouseY) {
    // Cursor erase/stamp is handled directly by the host blitter as two tiny
    // independent 20x24 patches (avoiding giant bounding-box merges on fast swipes!).
    State->PrevMouseX = State->MouseX;
    State->PrevMouseY = State->MouseY;
    State->MouseX     = NewX;
    State->MouseY     = NewY;
  }

  State->MouseVisible      = TRUE;
  Clicked                  = (LeftDown && !State->MouseLeftDown);
  State->MousePrevLeftDown = State->MouseLeftDown;
  State->MouseLeftDown     = LeftDown;

  if (State->ShowHelpModal) {
    if (Clicked) {
      State->ShowHelpModal = FALSE;
      MarkFullDirty (State);
    }
    return;
  }

  Hit = HitTestControl (State, NewX, NewY);
  if (Hit != State->HoveredCtrlIdx) {
    if (State->HoveredCtrlIdx >= 0 && State->HoveredCtrlIdx < State->ControlCount) {
      AURORA_UI_RECT  *Old = &State->Controls[State->HoveredCtrlIdx];
      MarkDirtyRect (State, Old->X - 6, Old->Y - 4, Old->X + Old->W + 6, Old->Y + Old->H + 4);
    }
    if (Hit >= 0 && Hit < State->ControlCount) {
      AURORA_UI_RECT  *Cur = &State->Controls[Hit];
      MarkDirtyRect (State, Cur->X - 6, Cur->Y - 4, Cur->X + Cur->W + 6, Cur->Y + Cur->H + 4);
    }
    State->HoveredCtrlIdx = Hit;
    State->SceneDirty     = TRUE;
    if (State->Mode == AURORA_MODE_ADVANCED) {
      MarkDirtyRect (State, State->Width - 360, 470, State->Width - 20, State->Height - 50);
    }
  }

  if (Clicked) {
    State->Info.MouseClicks++;
    if (Hit >= 0) {
      State->FocusedCtrlIdx = Hit;
      ActivateControl (State, Hit, +1);
    }
  }
}

VOID
AuroraBiosHandleMouseDelta (
  AURORA_BIOS_STATE  *State,
  INT32              DeltaX,
  INT32              DeltaY,
  BOOLEAN            LeftDown
  )
{
  INT32  Mult = (INT32)State->Config.MouseSpeed;
  INT32  Sx;
  INT32  Sy;

  if (Mult < 1)  Mult = 1;
  if (Mult > 10) Mult = 10;

  State->Info.LastRawDx = DeltaX;
  State->Info.LastRawDy = DeltaY;

  Sx = (DeltaX * (3 + Mult)) / 8;
  Sy = (DeltaY * (3 + Mult)) / 8;
  if (DeltaX != 0 && Sx == 0) Sx = (DeltaX > 0) ? 1 : -1;
  if (DeltaY != 0 && Sy == 0) Sy = (DeltaY > 0) ? 1 : -1;

  AuroraBiosHandleMouseMove (State, State->MouseX + Sx, State->MouseY + Sy, LeftDown);
}

// ===========================================================================
// High-Craft Gigabyte-Style Software Renderer (Anti-Aliased Fonts & Gradients)
// ===========================================================================
static UINT32
BlendRgb (
  UINT32  Bg,
  UINT32  Fg,
  UINT32  Alpha255
  )
{
  UINT32  Inv = 255u - Alpha255;
  UINT32  R   = (((Bg >> 16) & 0xFFu) * Inv + ((Fg >> 16) & 0xFFu) * Alpha255) / 255u;
  UINT32  G   = (((Bg >> 8) & 0xFFu) * Inv + ((Fg >> 8) & 0xFFu) * Alpha255) / 255u;
  UINT32  B   = ((Bg & 0xFFu) * Inv + (Fg & 0xFFu) * Alpha255) / 255u;
  return (R << 16) | (G << 8) | B;
}

static VOID
PutPx (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y,
  UINT32  Col
  )
{
  if (X >= 0 && X < W && Y >= 0 && Y < H) {
    Buf[Y * W + X] = Col;
  }
}

static VOID
FillRect (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y,
  INT32   RW,
  INT32   RH,
  UINT32  Col
  )
{
  INT32  X0 = (X < 0) ? 0 : X;
  INT32  Y0 = (Y < 0) ? 0 : Y;
  INT32  X1 = (X + RW > W) ? W : (X + RW);
  INT32  Y1 = (Y + RH > H) ? H : (Y + RH);
  INT32  Py;
  INT32  Px;

  for (Py = Y0; Py < Y1; Py++) {
    UINT32  *Row = Buf + Py * W;
    for (Px = X0; Px < X1; Px++) {
      Row[Px] = Col;
    }
  }
}

static VOID
FillGradV (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y,
  INT32   RW,
  INT32   RH,
  UINT32  TopCol,
  UINT32  BotCol
  )
{
  INT32  Py;
  if (RH <= 0 || RW <= 0) return;
  for (Py = 0; Py < RH; Py++) {
    UINT32  A   = (RH > 1) ? ((UINT32)Py * 255u) / (UINT32)(RH - 1) : 0u;
    UINT32  Col = BlendRgb (TopCol, BotCol, A);
    FillRect (Buf, W, H, X, Y + Py, RW, 1, Col);
  }
}

static VOID
DrawRectBorder (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y,
  INT32   RW,
  INT32   RH,
  UINT32  Col
  )
{
  FillRect (Buf, W, H, X, Y, RW, 1, Col);
  FillRect (Buf, W, H, X, Y + RH - 1, RW, 1, Col);
  FillRect (Buf, W, H, X, Y, 1, RH, Col);
  FillRect (Buf, W, H, X + RW - 1, Y, 1, RH, Col);
}

// Gigabyte Signature Molten-Orange Horizontal Light Streak
static VOID
DrawOrangeLightBar (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   CenterY,
  INT32   HalfThickness
  )
{
  INT32  Dy;
  INT32  Px;

  for (Dy = -HalfThickness; Dy <= HalfThickness; Dy++) {
    INT32   Y    = CenterY + Dy;
    INT32   Dist = (Dy < 0) ? -Dy : Dy;
    UINT32  VertStrength;
    if (Y < 0 || Y >= H) continue;
    // Sharp bright core at Dist==0, exponential warm orange falloff
    if (Dist == 0) {
      VertStrength = 255u;
    } else if (Dist == 1) {
      VertStrength = 175u;
    } else if (Dist == 2) {
      VertStrength = 105u;
    } else {
      INT32  Rem = HalfThickness - Dist + 1;
      VertStrength = (Rem > 0) ? (UINT32)((Rem * 70) / HalfThickness) : 0u;
    }

    for (Px = 0; Px < W; Px++) {
      // Subtle edge fade at extreme left/right 10%
      UINT32  HScale = 255u;
      if (Px < W / 10) {
        HScale = 70u + (UINT32)(Px * 185) / (UINT32)(W / 10);
      } else if (Px > W - W / 10) {
        HScale = 70u + (UINT32)((W - 1 - Px) * 185) / (UINT32)(W / 10);
      }
      UINT32  Alpha = (VertStrength * HScale) / 255u;
      UINT32  Tint  = (Dist == 0) ? 0x00FF8E3Cu : COL_ORANGE;
      Buf[Y * W + Px] = BlendRgb (Buf[Y * W + Px], Tint, Alpha);
    }
  }
}

// Anti-Aliased 4-Bit Proportional Text Measurement & Drawing
static INT32
MeasureText (
  const AURORA_FONT_FACE  *Face,
  const char              *Str
  )
{
  INT32  W = 0;
  if (Str == NULL || Face == NULL) return 0;
  while (*Str != '\0') {
    UINT8  C = (UINT8)*Str++;
    if (C < Face->FirstChar || C > Face->LastChar) C = ' ';
    W += Face->Glyphs[C - Face->FirstChar].Advance;
  }
  return W;
}

static VOID
DrawTextFace (
  UINT32                  *Buf,
  INT32                   W,
  INT32                   H,
  const AURORA_FONT_FACE  *Face,
  INT32                   X,
  INT32                   Y,
  UINT32                  Col,
  const char              *Str
  )
{
  INT32  CurX = X;
  if (Str == NULL || Face == NULL) return;

  while (*Str != '\0') {
    UINT8                    C = (UINT8)*Str++;
    const AURORA_GLYPH_META  *G;
    INT32                    Gy;
    INT32                    Gx;
    UINT32                   RowBytes;

    if (C < Face->FirstChar || C > Face->LastChar) {
      C = ' ';
    }
    G = &Face->Glyphs[C - Face->FirstChar];
    if (G->Width > 0u && G->Height > 0u) {
      RowBytes = ((UINT32)G->Width + 1u) / 2u;
      for (Gy = 0; Gy < (INT32)G->Height; Gy++) {
        INT32        Py  = Y + (INT32)G->BearingY + Gy;
        const UINT8  *Rp = Face->Bitmap + G->BitmapOffset + (UINT32)Gy * RowBytes;
        if (Py < 0 || Py >= H) continue;
        for (Gx = 0; Gx < (INT32)G->Width; Gx++) {
          UINT8   Byte  = Rp[Gx >> 1];
          UINT32  Nib   = ((Gx & 1) == 0) ? (UINT32)(Byte >> 4) : (UINT32)(Byte & 0x0Fu);
          INT32   Px    = CurX + (INT32)G->BearingX + Gx;
          if (Nib != 0u && Px >= 0 && Px < W) {
            if (Nib == 15u) {
              Buf[Py * W + Px] = Col;
            } else {
              Buf[Py * W + Px] = BlendRgb (Buf[Py * W + Px], Col, Nib * 17u);
            }
          }
        }
      }
    }
    CurX += (INT32)G->Advance;
  }
}

static VOID
DrawTextCentered (
  UINT32                  *Buf,
  INT32                   W,
  INT32                   H,
  const AURORA_FONT_FACE  *Face,
  INT32                   Rx,
  INT32                   Ry,
  INT32                   Rw,
  INT32                   Rh,
  UINT32                  Col,
  const char              *Str
  )
{
  INT32  Tw = MeasureText (Face, Str);
  INT32  Tx = Rx + (Rw - Tw) / 2;
  INT32  Ty = Ry + (Rh - (INT32)Face->LineHeight) / 2;
  DrawTextFace (Buf, W, H, Face, Tx, Ty, Col, Str);
}

// ---------------------------------------------------------------------------
// Gigabyte Slanted Trapezoid Mode Tab (EASY MODE / ADVANCED MODE)
// ---------------------------------------------------------------------------
static VOID
DrawSlantedModeTab (
  UINT32         *Buf,
  INT32          W,
  INT32          H,
  INT32          X,
  INT32          Y,
  INT32          TW,
  INT32          TH,
  BOOLEAN        Active,
  BOOLEAN        Hovered,
  const char     *Label
  )
{
  INT32  Row;
  INT32  Slant = 18;

  for (Row = 0; Row < TH; Row++) {
    // Slant bottom-left to top-right (EXACT Gigabyte angled slash tabs!)
    INT32   Shift = ((TH - 1 - Row) * Slant) / (TH > 1 ? TH - 1 : 1);
    INT32   Lx    = X + Shift;
    INT32   Rx    = X + TW - Slant + Shift;
    UINT32  FillCol;
    UINT32  EdgeCol;

    if (Active) {
      UINT32  G = (Row < TH / 2) ? (UINT32)((TH / 2 - Row) * 255 / (TH / 2)) : (UINT32)((Row - TH / 2) * 255 / (TH / 2));
      FillCol = BlendRgb (0x000D0603u, 0x00331405u, G);
      EdgeCol = COL_ORANGE_HI;
    } else if (Hovered) {
      FillCol = BlendRgb (0x002C2C2Cu, 0x001A1A1Au, (UINT32)(Row * 255 / TH));
      EdgeCol = COL_ORANGE;
    } else {
      FillCol = BlendRgb (0x002A2A2Au, 0x00161616u, (UINT32)(Row * 255 / TH));
      EdgeCol = 0x00404040u;
    }

    FillRect (Buf, W, H, Lx, Y + Row, Rx - Lx, 1, FillCol);
    if (Row == 0 || Row == TH - 1) {
      FillRect (Buf, W, H, Lx, Y + Row, Rx - Lx, 1, EdgeCol);
    } else {
      PutPx (Buf, W, H, Lx, Y + Row, EdgeCol);
      PutPx (Buf, W, H, Lx + 1, Y + Row, BlendRgb (FillCol, EdgeCol, 120u));
      PutPx (Buf, W, H, Rx - 1, Y + Row, EdgeCol);
      PutPx (Buf, W, H, Rx - 2, Y + Row, BlendRgb (FillCol, EdgeCol, 120u));
    }
  }

  DrawTextCentered (
    Buf, W, H, &mAuroraFontBold, X, Y, TW, TH,
    Active ? COL_ORANGE_HI : (Hovered ? COL_WHITE : 0x00929292u),
    Label
    );
}

// ---------------------------------------------------------------------------
// Bold Geometric White Brand Wordmark ("AURORA" - 1:1 weight to GIGABYTE)
// ---------------------------------------------------------------------------
static VOID
DrawAuroraLogo (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y
  )
{
  // Crisp geometric 22px-tall clean modern block letters for "AURORA"
  // A: [0..18], U: [24..42], R: [48..66], O: [72..92], R: [98..116], A: [122..140]
  INT32  S = 4;  // Stroke thickness
  INT32  LH = 20;

  // 'A'
  FillRect (Buf, W, H, X + 2, Y, 14, S, COL_WHITE);
  FillRect (Buf, W, H, X, Y + 2, S, LH - 2, COL_WHITE);
  FillRect (Buf, W, H, X + 14, Y + 2, S, LH - 2, COL_WHITE);
  FillRect (Buf, W, H, X + S, Y + 10, 10, 3, COL_WHITE);

  // 'U'
  {
    INT32  Ux = X + 24;
    FillRect (Buf, W, H, Ux, Y, S, LH - 2, COL_WHITE);
    FillRect (Buf, W, H, Ux + 14, Y, S, LH - 2, COL_WHITE);
    FillRect (Buf, W, H, Ux + 2, Y + LH - S, 14, S, COL_WHITE);
  }

  // 'R'
  {
    INT32  Rx = X + 48;
    FillRect (Buf, W, H, Rx, Y, S, LH, COL_WHITE);
    FillRect (Buf, W, H, Rx, Y, 15, S, COL_WHITE);
    FillRect (Buf, W, H, Rx + 14, Y + 2, S, 7, COL_WHITE);
    FillRect (Buf, W, H, Rx, Y + 9, 15, 3, COL_WHITE);
    FillRect (Buf, W, H, Rx + 11, Y + 12, 4, 4, COL_WHITE);
    FillRect (Buf, W, H, Rx + 14, Y + 15, 4, 5, COL_WHITE);
  }

  // 'O' (subtle molten orange accent dot in center? No, keep pure crisp white like GIGABYTE!)
  {
    INT32  Ox = X + 72;
    FillRect (Buf, W, H, Ox + 2, Y, 16, S, COL_WHITE);
    FillRect (Buf, W, H, Ox + 2, Y + LH - S, 16, S, COL_WHITE);
    FillRect (Buf, W, H, Ox, Y + 2, S, LH - 4, COL_WHITE);
    FillRect (Buf, W, H, Ox + 16, Y + 2, S, LH - 4, COL_WHITE);
  }

  // 'R'
  {
    INT32  Rx = X + 98;
    FillRect (Buf, W, H, Rx, Y, S, LH, COL_WHITE);
    FillRect (Buf, W, H, Rx, Y, 15, S, COL_WHITE);
    FillRect (Buf, W, H, Rx + 14, Y + 2, S, 7, COL_WHITE);
    FillRect (Buf, W, H, Rx, Y + 9, 15, 3, COL_WHITE);
    FillRect (Buf, W, H, Rx + 11, Y + 12, 4, 4, COL_WHITE);
    FillRect (Buf, W, H, Rx + 14, Y + 15, 4, 5, COL_WHITE);
  }

  // 'A'
  {
    INT32  Ax = X + 122;
    FillRect (Buf, W, H, Ax + 2, Y, 14, S, COL_WHITE);
    FillRect (Buf, W, H, Ax, Y + 2, S, LH - 2, COL_WHITE);
    FillRect (Buf, W, H, Ax + 14, Y + 2, S, LH - 2, COL_WHITE);
    FillRect (Buf, W, H, Ax + S, Y + 10, 10, 3, COL_WHITE);
  }
}

// ---------------------------------------------------------------------------
// Gigabyte Widgets: Metallic Button, Orange Toggle Switch, Dropdown Box, Icons
// ---------------------------------------------------------------------------
static VOID
DrawMetallicButton (
  UINT32      *Buf,
  INT32       W,
  INT32       H,
  INT32       X,
  INT32       Y,
  INT32       BW,
  INT32       BH,
  BOOLEAN     Hovered,
  BOOLEAN     Focused,
  UINT32      TextCol,
  const char  *Label
  )
{
  UINT32  TopC = Hovered ? 0x00422412u : (Focused ? 0x003B2110u : 0x00343434u);
  UINT32  BotC = Hovered ? 0x001D0E06u : (Focused ? 0x00180C05u : 0x000F0F0Fu);
  UINT32  BdrC = (Hovered || Focused) ? COL_ORANGE : 0x00383838u;

  FillGradV (Buf, W, H, X + 1, Y + 1, BW - 2, BH - 2, TopC, BotC);
  DrawRectBorder (Buf, W, H, X, Y, BW, BH, BdrC);
  // Crisp metallic top highlight
  FillRect (Buf, W, H, X + 1, Y + 1, BW - 2, 1, (Hovered || Focused) ? COL_ORANGE_HI : 0x00525252u);
  DrawTextCentered (Buf, W, H, &mAuroraFontBody, X, Y, BW, BH, TextCol, Label);
}

// Gigabyte Signature Metallic Toggle Switch (Orange left track when ON, brushed silver thumb)
static VOID
DrawGigabyteToggle (
  UINT32   *Buf,
  INT32    W,
  INT32    H,
  INT32    X,
  INT32    Y,
  BOOLEAN  On
  )
{
  INT32  TW = 46;
  INT32  TH = 18;
  INT32  ThumbW = 22;
  INT32  ThumbX = On ? (X + TW - ThumbW - 1) : (X + 1);

  // Outer recessed bezel
  FillRect (Buf, W, H, X, Y, TW, TH, 0x00141414u);
  DrawRectBorder (Buf, W, H, X, Y, TW, TH, 0x00383838u);

  if (On) {
    // Glowing molten-orange active track behind thumb
    FillGradV (Buf, W, H, X + 1, Y + 1, TW - ThumbW - 2, TH - 2, COL_ORANGE_HI, 0x00B83D08u);
  } else {
    FillRect (Buf, W, H, X + ThumbW + 1, Y + 1, TW - ThumbW - 2, TH - 2, 0x00202020u);
  }

  // Brushed silver-metallic sliding thumb
  FillGradV (Buf, W, H, ThumbX, Y + 1, ThumbW, TH - 2, 0x00D8D8D8u, 0x007C7C7Cu);
  DrawRectBorder (Buf, W, H, ThumbX, Y + 1, ThumbW, TH - 2, 0x00E8E8E8u);
}

// Gigabyte Metallic Dropdown Selector Box with Right Chevron Button
static VOID
DrawGigabyteDropdown (
  UINT32      *Buf,
  INT32       W,
  INT32       H,
  INT32       X,
  INT32       Y,
  INT32       DW,
  INT32       DH,
  BOOLEAN     Hovered,
  BOOLEAN     Focused,
  const char  *Value
  )
{
  INT32   ArrW = 20;
  UINT32  TopC = (Hovered || Focused) ? 0x00462A18u : 0x003A3A3Au;
  UINT32  BotC = (Hovered || Focused) ? 0x0026140Au : 0x00242424u;
  UINT32  BdrC = (Hovered || Focused) ? COL_ORANGE : 0x00484848u;

  FillGradV (Buf, W, H, X + 1, Y + 1, DW - 2, DH - 2, TopC, BotC);
  DrawRectBorder (Buf, W, H, X, Y, DW, DH, BdrC);
  // Right chevron compartment
  FillRect (Buf, W, H, X + DW - ArrW, Y + 1, 1, DH - 2, 0x004E4E4Eu);
  // Draw crisp down-pointing triangle chevron
  {
    INT32  Cx = X + DW - ArrW / 2;
    INT32  Cy = Y + DH / 2 - 2;
    INT32  Row;
    for (Row = 0; Row < 5; Row++) {
      FillRect (Buf, W, H, Cx - (4 - Row), Cy + Row, (4 - Row) * 2 + 1, 1, (Hovered || Focused) ? COL_ORANGE_HI : 0x009C9C9Cu);
    }
  }
  DrawTextFace (Buf, W, H, &mAuroraFontSmall, X + 8, Y + (DH - (INT32)mAuroraFontSmall.LineHeight) / 2, COL_WHITE, Value);
}

// Crisp Fan/Turbine Icon (mirrors Gigabyte Smart Fan 6 icon)
static VOID
DrawFanIcon (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   Cx,
  INT32   Cy,
  INT32   R,
  UINT32  Col
  )
{
  INT32  Y;
  INT32  X;
  for (Y = -R; Y <= R; Y++) {
    for (X = -R; X <= R; X++) {
      INT32  D2 = X * X + Y * Y;
      if (D2 >= (R - 2) * (R - 2) && D2 <= R * R) {
        PutPx (Buf, W, H, Cx + X, Cy + Y, Col);
      } else if (D2 <= 5) {
        PutPx (Buf, W, H, Cx + X, Cy + Y, Col);
      } else if (D2 < (R - 2) * (R - 2)) {
        // 4 curved blades
        BOOLEAN  Blade = ((X > 0 && Y >= -2 && Y <= 2) ||
                          (X < 0 && Y >= -2 && Y <= 2) ||
                          (Y > 0 && X >= -2 && X <= 2) ||
                          (Y < 0 && X >= -2 && X <= 2));
        if (Blade) {
          PutPx (Buf, W, H, Cx + X, Cy + Y, BlendRgb (COL_BLACK, Col, 210u));
        }
      }
    }
  }
}

// Crisp Storage Drive Icon (1:1 mirror of Gigabyte Boot Sequence HDD/SSD icon)
static VOID
DrawDriveIcon (
  UINT32  *Buf,
  INT32   W,
  INT32   H,
  INT32   X,
  INT32   Y
  )
{
  // Crisp white 26x32 drive chassis with dark circular platter & diagonal arm
  FillRect (Buf, W, H, X + 1, Y, 24, 32, 0x00EAEAEAu);
  FillRect (Buf, W, H, X, Y + 1, 26, 30, 0x00EAEAEAu);
  // Corner screw dots
  PutPx (Buf, W, H, X + 3, Y + 3, COL_BLACK);
  PutPx (Buf, W, H, X + 22, Y + 3, COL_BLACK);
  PutPx (Buf, W, H, X + 3, Y + 28, COL_BLACK);
  PutPx (Buf, W, H, X + 22, Y + 28, COL_BLACK);
  // Dark circular platter centered at (X+13, Y+13), R=8
  {
    INT32  Dy;
    INT32  Dx;
    for (Dy = -8; Dy <= 8; Dy++) {
      for (Dx = -8; Dx <= 8; Dx++) {
        INT32  D2 = Dx * Dx + Dy * Dy;
        if (D2 <= 64 && D2 >= 9) {
          PutPx (Buf, W, H, X + 13 + Dx, Y + 13 + Dy, 0x00161616u);
        }
      }
    }
  }
  // Actuator arm
  FillRect (Buf, W, H, X + 5, Y + 23, 12, 3, 0x00161616u);
  FillRect (Buf, W, H, X + 11, Y + 18, 4, 6, 0x00161616u);
}

// Card Frame with Gigabyte Floating Title Above Top Border
static VOID
DrawGigabyteCard (
  UINT32      *Buf,
  INT32       W,
  INT32       H,
  INT32       X,
  INT32       Y,
  INT32       CW,
  INT32       CH,
  const char  *Title
  )
{
  if (Title != NULL && Title[0] != '\0') {
    DrawTextFace (Buf, W, H, &mAuroraFontBody, X + 2, Y - 22, 0x00BDBDBDu, Title);
  }
  FillRect (Buf, W, H, X, Y, CW, CH, COL_BG_CARD);
  DrawRectBorder (Buf, W, H, X, Y, CW, CH, COL_BORDER);
}

// ---------------------------------------------------------------------------
// Setting Label & Value Formatters
// ---------------------------------------------------------------------------
static const char *
GetControlLabel (
  UINT32  Id
  )
{
  switch (Id) {
    case CTRL_SET_PERF_PROFILE:   return "Performance Mode";
    case CTRL_SET_DIRECT_SCANOUT: return "Direct Scanout";
    case CTRL_SET_GPU_POOL:       return "GPU Local Pool";
    case CTRL_SET_KBD_BACKLIGHT:  return "Keyboard Backlight";
    case CTRL_SET_SCALE_MODE:     return "Display HiDPI Scale";
    case CTRL_SET_MOUSE_SPEED:    return "Pointer Sensitivity";
    case CTRL_SET_FAST_BOOT:      return "Fast Boot";
    case CTRL_SET_SPLASH_LOGO:    return "Boot Splash Logo";
    case CTRL_SET_DEFAULT_TARGET: return "Default Boot Target";
    case CTRL_SET_BOOT_TIMEOUT:   return "Boot Timeout";
    case CTRL_SET_SERIAL_DEBUG:   return "Serial Debug Level";
    case CTRL_SET_USB_AUTO:       return "USB HID Auto-Scan";
    case CTRL_SET_EXT_USB_BOOT:   return "External USB Boot";
    case CTRL_SET_ENTER_ONCE:     return "One-Shot Setup";
    default:                      return "";
  }
}

static VOID
GetControlValueStr (
  const AURORA_BIOS_STATE  *State,
  UINT32                   Id,
  char                     *Out,
  UINTN                    OutSize
  )
{
  const AURORA_BIOS_CONFIG  *C = &State->Config;

  switch (Id) {
    case CTRL_SET_PERF_PROFILE:
      BiosStrCpy (Out, OutSize,
        C->PerformanceProfile == AURORA_PROFILE_PERFORMANCE ? "Performance" :
        (C->PerformanceProfile == AURORA_PROFILE_QUIET ? "Quiet" : "Balanced"));
      return;
    case CTRL_SET_DIRECT_SCANOUT:
      BiosStrCpy (Out, OutSize, C->DirectScanout ? "Enabled" : "Disabled");
      return;
    case CTRL_SET_GPU_POOL:
      BiosStrCpy (Out, OutSize, C->GpuLocalPoolEnable ? "Enabled" : "Disabled");
      return;
    case CTRL_SET_KBD_BACKLIGHT: {
      static const char  *Lvls[5] = { "Off (0%)", "Low (25%)", "Med (50%)", "High (75%)", "Max (100%)" };
      BiosStrCpy (Out, OutSize, Lvls[C->KbdBacklightLevel < 5u ? C->KbdBacklightLevel : 3u]);
      return;
    }
    case CTRL_SET_SCALE_MODE:
      BiosStrCpy (Out, OutSize, C->ConsoleScaleMode == 2u ? "2x Native" : "1x Raw");
      return;
    case CTRL_SET_MOUSE_SPEED: {
      char  Num[8];
      BiosFmtU32 (Num, sizeof (Num), C->MouseSpeed);
      BiosStrCpy (Out, OutSize, "Level ");
      BiosStrCat (Out, OutSize, Num);
      return;
    }
    case CTRL_SET_FAST_BOOT:
      BiosStrCpy (Out, OutSize, C->FastBoot ? "Enabled" : "Disabled");
      return;
    case CTRL_SET_SPLASH_LOGO:
      BiosStrCpy (Out, OutSize, C->BootSplashLogo ? "Enabled" : "Disabled");
      return;
    case CTRL_SET_DEFAULT_TARGET:
      BiosStrCpy (Out, OutSize,
        C->DefaultBootTarget == AURORA_BOOT_TARGET_SHELL ? "UEFI Shell" :
        (C->DefaultBootTarget == AURORA_BOOT_TARGET_BIOS ? "UEFI Setup" : "Windows OS"));
      return;
    case CTRL_SET_BOOT_TIMEOUT: {
      char  Num[8];
      BiosFmtU32 (Num, sizeof (Num), C->BootTimeoutSec);
      BiosStrCpy (Out, OutSize, Num);
      BiosStrCat (Out, OutSize, " sec");
      return;
    }
    case CTRL_SET_SERIAL_DEBUG:
      BiosStrCpy (Out, OutSize,
        C->SerialDebugLevel == AURORA_DEBUG_VERBOSE ? "Verbose" :
        (C->SerialDebugLevel == AURORA_DEBUG_ERRORS ? "Errors Only" : "Standard"));
      return;
    case CTRL_SET_USB_AUTO:
      BiosStrCpy (Out, OutSize, C->UsbHidAutoConnect ? "Auto" : "On-Demand");
      return;
    case CTRL_SET_EXT_USB_BOOT:
      BiosStrCpy (Out, OutSize, C->ExternalUsbBoot ? "Enabled" : "Disabled");
      return;
    case CTRL_SET_ENTER_ONCE:
      BiosStrCpy (Out, OutSize, C->EnterBiosOnce ? "Armed" : "Off");
      return;
    default:
      Out[0] = '\0';
      return;
  }
}

static BOOLEAN
GetControlToggleBool (
  const AURORA_BIOS_STATE  *State,
  UINT32                   Id
  )
{
  switch (Id) {
    case CTRL_SET_DIRECT_SCANOUT: return State->Config.DirectScanout != 0u;
    case CTRL_SET_GPU_POOL:       return State->Config.GpuLocalPoolEnable != 0u;
    case CTRL_SET_FAST_BOOT:      return State->Config.FastBoot != 0u;
    case CTRL_SET_SPLASH_LOGO:    return State->Config.BootSplashLogo != 0u;
    case CTRL_SET_USB_AUTO:       return State->Config.UsbHidAutoConnect != 0u;
    case CTRL_SET_EXT_USB_BOOT:   return State->Config.ExternalUsbBoot != 0u;
    case CTRL_SET_ENTER_ONCE:     return State->Config.EnterBiosOnce != 0u;
    default:                      return FALSE;
  }
}

static const char *
GetItemHelpText (
  UINT32  Id
  )
{
  switch (Id) {
    case CTRL_SET_PERF_PROFILE:
      return "Select Apple M5 power and thermal policy (Balanced, Performance, or Quiet).";
    case CTRL_SET_DIRECT_SCANOUT:
      return "Enable zero-copy direct scanout for DWM primary surfaces on DCP display.";
    case CTRL_SET_GPU_POOL:
      return "Reserve 512 MB contiguous unified memory pool for native AGX G17G WDDM driver.";
    case CTRL_SET_KBD_BACKLIGHT:
      return "Adjust built-in MacBook keyboard white LED backlight live via SoC FPWM0.";
    case CTRL_SET_SCALE_MODE:
      return "Choose 2x HiDPI scaling (crisp on 2560x1664 Retina) or raw 1x framebuffer mode.";
    case CTRL_SET_MOUSE_SPEED:
      return "Adjust Trackpad and USB Mouse pointer speed (Level 1 = slow, Level 10 = fast).";
    case CTRL_SET_FAST_BOOT:
      return "Skip non-essential boot delays and proceed directly to primary OS loader.";
    case CTRL_SET_SPLASH_LOGO:
      return "Display Aurora Silicon firmware splash banner during UEFI initialization.";
    case CTRL_SET_DEFAULT_TARGET:
      return "Choose whether normal startup boots Windows OS, UEFI Shell, or UEFI Setup.";
    case CTRL_SET_BOOT_TIMEOUT:
      return "Seconds to wait for setup hotkey before booting default OS loader.";
    case CTRL_SET_SERIAL_DEBUG:
      return "Control serial debug logging verbosity during UEFI boot and driver init.";
    case CTRL_SET_USB_AUTO:
      return "Automatically scan USB Type-C HID controllers on UEFI Setup entry.";
    case CTRL_SET_EXT_USB_BOOT:
      return "Allow booting UEFI loaders from external USB storage devices.";
    case CTRL_SET_ENTER_ONCE:
      return "One-shot flag: enter Aurora UEFI Setup automatically on the next restart.";
    case CTRL_BTN_SAVE_EXIT:
      return "Write modified settings to UEFI NVRAM + ESP:\\EFI\\Aurora\\bios.cfg and boot OS.";
    case CTRL_BTN_DISCARD_EXIT:
      return "Discard unsaved changes and continue booting the configured OS.";
    default:
      return "Use Trackpad/Mouse to click controls, or Arrow keys + Enter. Press F2 to switch Easy / Advanced Mode.";
  }
}

// ---------------------------------------------------------------------------
// Easy Mode Renderer (1:1 Gigabyte Reference Layout)
// ---------------------------------------------------------------------------
static VOID
RenderEasyMode (
  const AURORA_BIOS_STATE  *State,
  UINT32                   *Buf
  )
{
  INT32  W = State->Width;
  INT32  H = State->Height;
  INT32  I;
  char   Tmp[64];
  char   Num[24];

  // =========================================================================
  // LEFT COLUMN: Card 1 "Information" + Card 2 "Memory & Display"
  // =========================================================================
  {
    INT32  Lx = 24;
    INT32  Lw = 316;

    // Card 1: Information (Y = 128..354)
    DrawGigabyteCard (Buf, W, H, Lx, 128, Lw, 226, "Information");
    {
      INT32  Ky = 144;
      INT32  Vy = 28;
      INT32  Vx = Lx + 96;

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 0 * Vy, COL_TEXT_MUTED, "Model:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,      Ky + 0 * Vy, COL_TEXT_PRI, State->Info.PlatformName);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 1 * Vy, COL_TEXT_MUTED, "BIOS Ver.:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,      Ky + 1 * Vy, COL_TEXT_PRI, State->Info.FirmwareVersion);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 2 * Vy, COL_TEXT_MUTED, "CPU:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,      Ky + 2 * Vy, COL_TEXT_PRI, State->Info.SocName);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 3 * Vy, COL_TEXT_MUTED, "Memory:");
      BiosFmtU32 (Num, sizeof (Num), (UINT32)((State->Info.TotalRamMiB + 512ULL) / 1024ULL));
      BiosStrCpy (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), " GB Unified LPDDR5X");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,      Ky + 3 * Vy, COL_TEXT_PRI, Tmp);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 4 * Vy, COL_TEXT_MUTED, "GPU:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,      Ky + 4 * Vy, COL_TEXT_PRI, "Apple G17G C0 (M5)");

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 5 * Vy, COL_TEXT_MUTED, "Input:");
      if (State->Info.MtpKbReady && State->Info.MtpTpReady) {
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Ky + 5 * Vy, COL_GREEN, "MTP KB + Trackpad LIVE");
      } else if (State->Info.MtpKbReady) {
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Ky + 5 * Vy, COL_ORANGE_HI, "MTP Keyboard LIVE");
      } else {
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Ky + 5 * Vy, COL_TEXT_SEC, "UEFI ConIn / Pointer");
      }

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Ky + 6 * Vy, COL_TEXT_MUTED, "Kbd LED:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Ky + 6 * Vy,
        State->Info.KblActive ? COL_GREEN : COL_TEXT_MUTED,
        State->Info.KblActive ? "FPWM0 Active" : "Unavailable");
    }

    // Card 2: Memory & Display (mirrors Gigabyte DRAM Status card, Y = 390..H-52)
    DrawGigabyteCard (Buf, W, H, Lx, 390, Lw, H - 52 - 390, "Memory & Display");
    {
      INT32  Sy = 406;
      INT32  Vx = Lx + 106;

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Sy + 0 * 24, COL_TEXT_MUTED, "UNIFIED_0");
      BiosFmtU32 (Num, sizeof (Num), (UINT32)State->Info.TotalRamMiB);
      BiosStrCpy (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), " MB Active");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Sy + 0 * 24, COL_TEXT_PRI, Tmp);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Sy + 1 * 24, COL_TEXT_MUTED, "GPU_POOL");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Sy + 1 * 24,
        State->Info.GpuPoolActive ? COL_ORANGE_HI : COL_TEXT_MUTED,
        State->Info.GpuPoolActive ? "512 MB Reserved" : "Disabled");

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Sy + 2 * 24, COL_TEXT_MUTED, "DISPLAY");
      BiosFmtU32 (Tmp, sizeof (Tmp), State->Info.GopWidth);
      BiosStrCat (Tmp, sizeof (Tmp), "x");
      BiosFmtU32 (Num, sizeof (Num), State->Info.GopHeight);
      BiosStrCat (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), " 32bpp");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Sy + 2 * 24, COL_TEXT_PRI, Tmp);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Lx + 12, Sy + 3 * 24, COL_TEXT_MUTED, "MTP_BUS");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Sy + 3 * 24,
        State->Info.MtpActive ? COL_GREEN : COL_TEXT_MUTED,
        State->Info.MtpActive ? "DockChannel CH1" : "Inactive");

      FillRect (Buf, W, H, Lx + 10, H - 234, Lw - 20, 1, COL_DIVIDER);
    }
  }

  // =========================================================================
  // CENTER-TOP: Floating Hero Telemetry (Pure Black, NO Border - 1:1 Gigabyte!)
  // =========================================================================
  {
    INT32  C1 = 384;
    INT32  C2 = 592;
    INT32  C3 = 790;
    INT32  R1 = 128;
    INT32  R2 = 224;

    // Row 1 Col 1: CPU Cores
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C1, R1, COL_TEXT_SEC, "CPU Cores");
    BiosFmtU32 (Num, sizeof (Num), State->Info.CpuCores > 0u ? State->Info.CpuCores : 10u);
    DrawTextFace (Buf, W, H, &mAuroraFontHero, C1, R1 + 18, COL_WHITE, Num);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C1 + MeasureText (&mAuroraFontHero, Num) + 6, R1 + 32, COL_TEXT_SEC, "ARMv9");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C1, R1 + 52, COL_TEXT_MUTED, "Apple M5 T8142");

    // Row 1 Col 2: Display Resolution
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C2, R1, COL_TEXT_SEC, "Display");
    BiosFmtU32 (Tmp, sizeof (Tmp), State->Info.GopWidth);
    BiosStrCat (Tmp, sizeof (Tmp), "x");
    BiosFmtU32 (Num, sizeof (Num), State->Info.GopHeight);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    DrawTextFace (Buf, W, H, &mAuroraFontHero, C2, R1 + 18, COL_WHITE, Tmp);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C2, R1 + 52, COL_TEXT_MUTED, "DCP Retina 60Hz");

    // Row 1 Col 3: Unified RAM
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C3, R1, COL_TEXT_SEC, "Memory");
    BiosFmtU32 (Num, sizeof (Num), (UINT32)((State->Info.TotalRamMiB + 512ULL) / 1024ULL));
    BiosStrCat (Num, sizeof (Num), ".0");
    DrawTextFace (Buf, W, H, &mAuroraFontHero, C3, R1 + 18, COL_WHITE, Num);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C3 + MeasureText (&mAuroraFontHero, Num) + 6, R1 + 32, COL_TEXT_SEC, "GB");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C3, R1 + 52, COL_TEXT_MUTED, "LPDDR5X Unified");

    // Row 2 Col 1: GPU Local Pool
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C1, R2, COL_TEXT_SEC, "GPU Local Pool");
    DrawTextFace (Buf, W, H, &mAuroraFontHero, C1, R2 + 18, COL_WHITE, State->Info.GpuPoolActive ? "512.0" : "0.0");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C1 + MeasureText (&mAuroraFontHero, State->Info.GpuPoolActive ? "512.0" : "0.0") + 6, R2 + 32, COL_TEXT_SEC, "MB");

    // Row 2 Col 2: Input Rate
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C2, R2, COL_TEXT_SEC, "Input Poll Rate");
    DrawTextFace (Buf, W, H, &mAuroraFontHero, C2, R2 + 18, COL_WHITE, "1000");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C2 + MeasureText (&mAuroraFontHero, "1000") + 6, R2 + 32, COL_TEXT_SEC, "Hz");

    // Row 2 Col 3: Live Uptime (ticks visibly every second!)
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, C3, R2, COL_TEXT_SEC, "Setup Uptime");
    {
      char  Mm[4];
      char  Ss[4];
      BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 100u);
      BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
      BiosStrCpy (Tmp, sizeof (Tmp), Mm);
      BiosStrCat (Tmp, sizeof (Tmp), ":");
      BiosStrCat (Tmp, sizeof (Tmp), Ss);
      DrawTextFace (Buf, W, H, &mAuroraFontHero, C3, R2 + 18, COL_WHITE, Tmp);
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, C3 + MeasureText (&mAuroraFontHero, Tmp) + 6, R2 + 32, COL_ORANGE_HI, "LIVE");
    }
  }

  // =========================================================================
  // TOP-RIGHT CARD: "Aurora PerfDrive" (1:1 Mirror of GIGABYTE PerfDrive!)
  // =========================================================================
  {
    INT32  Px = W - 324;
    INT32  Py = 114;
    INT32  Pw = 300;
    INT32  Ph = 188;

    FillRect (Buf, W, H, Px, Py, Pw, Ph, COL_BG_CARD);
    DrawRectBorder (Buf, W, H, Px, Py, Pw, Ph, COL_BORDER);
    // Signature glowing orange top header strip
    FillGradV (Buf, W, H, Px + 1, Py + 1, Pw - 2, 30, 0x003D1806u, 0x00140702u);
    FillRect (Buf, W, H, Px, Py, Pw, 2, COL_ORANGE);
    FillRect (Buf, W, H, Px + 1, Py + 31, Pw - 2, 1, 0x005C250Au);
    DrawTextCentered (Buf, W, H, &mAuroraFontBold, Px, Py + 1, Pw, 30, COL_ORANGE_HI, "Aurora PerfDrive");
  }

  // =========================================================================
  // MIDDLE ROW: 3 Side-by-Side Cards (Power & Thermal | Peripherals | Boot Sequence)
  // =========================================================================
  {
    INT32  MidY  = 338;
    INT32  MidH  = 196;
    INT32  Col1X = 360;
    INT32  Col1W = 300;
    INT32  Col2X = 674;
    INT32  Col2W = W - 324 - 14 - Col2X;
    INT32  Col3X = W - 324;
    INT32  Col3W = 300;

    // Card A: Power & Thermal (mirrors Smart Fan 6)
    DrawGigabyteCard (Buf, W, H, Col1X, MidY, Col1W, MidH, "Power & Thermal");
    DrawFanIcon (Buf, W, H, Col1X + 34, MidY + 34, 11, COL_TEXT_PRI);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col1X + 54, MidY + 20, COL_TEXT_MUTED, "PROFILE");
    GetControlValueStr (State, CTRL_SET_PERF_PROFILE, Tmp, sizeof (Tmp));
    DrawTextFace (Buf, W, H, &mAuroraFontBody,  Col1X + 54, MidY + 38, COL_WHITE, Tmp);

    DrawFanIcon (Buf, W, H, Col1X + 178, MidY + 34, 11, State->Info.KblActive ? COL_ORANGE_HI : COL_TEXT_MUTED);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col1X + 198, MidY + 20, COL_TEXT_MUTED, "KBD LED");
    GetControlValueStr (State, CTRL_SET_KBD_BACKLIGHT, Tmp, sizeof (Tmp));
    DrawTextFace (Buf, W, H, &mAuroraFontBody,  Col1X + 198, MidY + 38, COL_WHITE, Tmp);

    // Card B: Peripherals (mirrors Peripherals)
    DrawGigabyteCard (Buf, W, H, Col2X, MidY, Col2W, MidH, "Peripherals");
    {
      INT32  Py = MidY + 18;
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 14, Py + 0 * 28, COL_TEXT_MUTED, "NVME:");
      BiosFmtU32 (Num, sizeof (Num), (UINT32)State->Info.PrimaryDiskGiB);
      BiosStrCpy (Tmp, sizeof (Tmp), "Apple ANS2 (");
      BiosStrCat (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), " GB)");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 0 * 28, COL_TEXT_PRI, Tmp);

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 14, Py + 1 * 28, COL_TEXT_MUTED, "GPU:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 1 * 28, COL_TEXT_PRI, "AGX G17G C0 Direct");

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 14, Py + 2 * 28, COL_TEXT_MUTED, "KBD:");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 2 * 28,
        State->Info.MtpKbReady ? COL_GREEN : COL_TEXT_SEC,
        State->Info.MtpKbReady ? "MTP Built-in LIVE" : "UEFI ConIn");

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 14, Py + 3 * 28, COL_TEXT_MUTED, "TPAD:");
      if (State->Info.MtpTpReady) {
        BiosStrCpy (Tmp, sizeof (Tmp), "MultiTouch LIVE (");
        BiosStrCat (Tmp, sizeof (Tmp), State->Info.MtpFwSource);
        BiosStrCat (Tmp, sizeof (Tmp), ")");
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 3 * 28, COL_GREEN, Tmp);
      } else {
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 3 * 28, COL_AMBER, "FW Not Found");
      }

      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 14, Py + 4 * 28, COL_TEXT_MUTED, "RX:");
      BiosFmtU32 (Tmp, sizeof (Tmp), State->Info.MtpPacketsRx);
      BiosStrCat (Tmp, sizeof (Tmp), " pkts (");
      BiosFmtU32 (Num, sizeof (Num), State->Info.MtpKbReports);
      BiosStrCat (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), "K / ");
      BiosFmtU32 (Num, sizeof (Num), State->Info.MtpTpReports);
      BiosStrCat (Tmp, sizeof (Tmp), Num);
      BiosStrCat (Tmp, sizeof (Tmp), "T)");
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col2X + 64, Py + 4 * 28, COL_TEXT_SEC, Tmp);
    }

    // Card C: Boot Sequence (mirrors Boot Sequence with drive icon)
    DrawGigabyteCard (Buf, W, H, Col3X, MidY, Col3W, MidH, "Boot Sequence");
    DrawDriveIcon (Buf, W, H, Col3X + 18, MidY + 18);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Col3X + 56, MidY + 20, COL_TEXT_MUTED, "Primary Boot Device");
    if (State->BootEntryCount > 0u) {
      DrawTextFace (Buf, W, H, &mAuroraFontBody, Col3X + 56, MidY + 38, COL_WHITE, State->BootEntries[0].Description);
    } else {
      DrawTextFace (Buf, W, H, &mAuroraFontBody, Col3X + 56, MidY + 38, COL_TEXT_MUTED, "No UEFI Entries");
    }
    FillRect (Buf, W, H, Col3X + 10, MidY + 62, Col3W - 20, 1, COL_DIVIDER);
  }

  // =========================================================================
  // BOTTOM WIDE CARD: "Quick Access" (1:1 Mirror of Gigabyte Quick Access!)
  // =========================================================================
  {
    INT32  QaX = 360;
    INT32  QaW = W - QaX - 24;
    INT32  QaY = 568;
    INT32  QaH = H - 52 - QaY;
    DrawGigabyteCard (Buf, W, H, QaX, QaY, QaW, QaH, "Quick Access");
  }

  // =========================================================================
  // Render Interactive Controls Registered for Easy Mode
  // =========================================================================
  for (I = 0; I < State->ControlCount; I++) {
    const AURORA_UI_RECT  *R = &State->Controls[I];
    BOOLEAN               Hov = (I == State->HoveredCtrlIdx);
    BOOLEAN               Foc = (I == State->FocusedCtrlIdx);

    if (R->Kind == AURORA_CTRL_MODE_TAB || R->Kind == AURORA_CTRL_UTIL_BTN) {
      continue;  // Drawn in shared header
    }

    if (R->Kind == AURORA_CTRL_TOGGLE) {
      BOOLEAN  On = GetControlToggleBool (State, R->Id);
      if (Hov || Foc) {
        FillRect (Buf, W, H, R->X - 4, R->Y - 1, R->W + 8, R->H + 2, Foc ? COL_BG_ROW_FOC : COL_BG_ROW_HOV);
        FillRect (Buf, W, H, R->X - 4, R->Y - 1, 2, R->H + 2, COL_ORANGE);
      }
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, R->X + 2, R->Y + (R->H - (INT32)mAuroraFontSmall.LineHeight) / 2,
        (Hov || Foc) ? COL_WHITE : (On ? COL_TEXT_PRI : COL_TEXT_SEC),
        GetControlLabel (R->Id));
      DrawGigabyteToggle (Buf, W, H, R->X + R->W - 46, R->Y + (R->H - 18) / 2, On);
    } else if (R->Kind == AURORA_CTRL_CHOICE) {
      INT32  DropW = (R->W > 260) ? 116 : 108;
      if (Hov || Foc) {
        FillRect (Buf, W, H, R->X - 4, R->Y - 1, R->W + 8, R->H + 2, Foc ? COL_BG_ROW_FOC : COL_BG_ROW_HOV);
      }
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, R->X + 2, R->Y + (R->H - (INT32)mAuroraFontSmall.LineHeight) / 2,
        (Hov || Foc) ? COL_WHITE : COL_TEXT_PRI,
        GetControlLabel (R->Id));
      GetControlValueStr (State, R->Id, Tmp, sizeof (Tmp));
      DrawGigabyteDropdown (Buf, W, H, R->X + R->W - DropW, R->Y + 2, DropW, R->H - 4, Hov, Foc, Tmp);
    } else if (R->Kind == AURORA_CTRL_BUTTON) {
      const char  *Lbl = "Action";
      UINT32      TCol = (Hov || Foc) ? COL_WHITE : COL_TEXT_PRI;
      if (R->Id == CTRL_EASY_POWER_BTN) {
        Lbl  = "Power & Thermal Setup";
        TCol = COL_ORANGE_HI;
      } else if (R->Id == CTRL_EASY_QA_SHELL) {
        Lbl = "UEFI Shell";
      } else if (R->Id == CTRL_EASY_QA_SYSINFO) {
        Lbl = "System & Input Info";
      } else if (R->Id == CTRL_EASY_QA_BOOT) {
        Lbl = "Boot Priority Setup";
      }
      DrawMetallicButton (Buf, W, H, R->X, R->Y, R->W, R->H, Hov, Foc, TCol, Lbl);
    } else if (R->Kind == AURORA_CTRL_BOOT_ROW) {
      UINT32   Idx = R->Param;
      BOOLEAN  Sel = (Idx == State->SelectedBootIdx);
      if (Sel || Hov || Foc) {
        FillRect (Buf, W, H, R->X, R->Y, R->W, R->H, Sel ? COL_BG_ROW_FOC : COL_BG_ROW_HOV);
        FillRect (Buf, W, H, R->X, R->Y, 3, R->H, Sel ? COL_ORANGE_HI : COL_ORANGE);
      }
      if (Idx < State->BootEntryCount) {
        BiosFmtU32 (Num, sizeof (Num), Idx + 1u);
        BiosStrCpy (Tmp, sizeof (Tmp), Num);
        BiosStrCat (Tmp, sizeof (Tmp), ". ");
        BiosStrCat (Tmp, sizeof (Tmp), State->BootEntries[Idx].Description);
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, R->X + 10, R->Y + 4,
          Sel ? COL_WHITE : COL_TEXT_SEC, Tmp);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Advanced Mode Renderer (Authentic Gigabyte Advanced Mode 1:1)
// ---------------------------------------------------------------------------
static VOID
RenderAdvancedMode (
  const AURORA_BIOS_STATE  *State,
  UINT32                   *Buf
  )
{
  static const char  *SubTabNames[AURORA_TAB_COUNT] = {
    "Tweaker", "Settings", "System Info.", "Boot", "Save & Exit"
  };
  INT32  W     = State->Width;
  INT32  H     = State->Height;
  INT32  PaneX = 24;
  INT32  PaneY = 148;
  INT32  PaneW = W - 384;
  INT32  PaneH = H - 52 - PaneY;
  INT32  SideX = PaneX + PaneW + 16;
  INT32  SideW = W - SideX - 24;
  INT32  I;
  char   Tmp[80];
  char   Num[24];

  // Sub-tab bar underline
  FillRect (Buf, W, H, 24, 136, W - 48, 1, COL_BORDER);

  // Main Left Settings Container
  FillRect (Buf, W, H, PaneX, PaneY, PaneW, PaneH, COL_BG_CARD);
  DrawRectBorder (Buf, W, H, PaneX, PaneY, PaneW, PaneH, COL_BORDER);

  // Right Sidebar: Hardware Monitor + Item Help
  DrawGigabyteCard (Buf, W, H, SideX, PaneY + 22, SideW, 286, "Hardware Monitor");
  {
    INT32  Hy = PaneY + 36;
    INT32  Vx = SideX + 132;

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 0 * 28, COL_TEXT_MUTED, "Platform");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 0 * 28, COL_TEXT_PRI, "Mac17,3 (J813)");

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 1 * 28, COL_TEXT_MUTED, "Processor");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 1 * 28, COL_TEXT_PRI, "Apple M5 10-Core");

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 2 * 28, COL_TEXT_MUTED, "Unified Memory");
    BiosFmtU32 (Num, sizeof (Num), (UINT32)((State->Info.TotalRamMiB + 512ULL) / 1024ULL));
    BiosStrCpy (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), " GB");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 2 * 28, COL_TEXT_PRI, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 3 * 28, COL_TEXT_MUTED, "GPU Local Pool");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 3 * 28,
      State->Info.GpuPoolActive ? COL_ORANGE_HI : COL_TEXT_MUTED,
      State->Info.GpuPoolActive ? "512 MB Active" : "Off");

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 4 * 28, COL_TEXT_MUTED, "MTP Keyboard");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 4 * 28,
      State->Info.MtpKbReady ? COL_GREEN : COL_TEXT_SEC,
      State->Info.MtpKbReady ? "LIVE" : "Fallback");

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 5 * 28, COL_TEXT_MUTED, "MTP Trackpad");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 5 * 28,
      State->Info.MtpTpReady ? COL_GREEN : COL_AMBER,
      State->Info.MtpTpReady ? "LIVE" : "FW Missing");

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 6 * 28, COL_TEXT_MUTED, "Kbd Backlight");
    GetControlValueStr (State, CTRL_SET_KBD_BACKLIGHT, Tmp, sizeof (Tmp));
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 6 * 28,
      State->Info.KblActive ? COL_ORANGE_HI : COL_TEXT_MUTED, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 7 * 28, COL_TEXT_MUTED, "Unsaved Edits");
    BiosFmtU32 (Num, sizeof (Num), AuroraBiosCountModified (State));
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx,         Hy + 7 * 28,
      AuroraBiosCountModified (State) > 0u ? COL_AMBER : COL_GREEN, Num);

    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Hy + 8 * 28, COL_TEXT_MUTED, "Setup Uptime");
    {
      char  Mm[4];
      char  Ss[4];
      BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 100u);
      BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
      BiosStrCpy (Tmp, sizeof (Tmp), Mm);
      BiosStrCat (Tmp, sizeof (Tmp), ":");
      BiosStrCat (Tmp, sizeof (Tmp), Ss);
      DrawTextFace (Buf, W, H, &mAuroraFontSmall, Vx, Hy + 8 * 28, COL_WHITE, Tmp);
    }
  }

  // Right Bottom Sidebar: Contextual Item Help
  {
    INT32   HelpY  = PaneY + 340;
    INT32   HelpH  = H - 52 - HelpY;
    UINT32  HelpId = 0;

    if (State->HoveredCtrlIdx >= 0 && State->HoveredCtrlIdx < State->ControlCount) {
      HelpId = State->Controls[State->HoveredCtrlIdx].Id;
    } else if (State->FocusedCtrlIdx >= 0 && State->FocusedCtrlIdx < State->ControlCount) {
      HelpId = State->Controls[State->FocusedCtrlIdx].Id;
    }
    DrawGigabyteCard (Buf, W, H, SideX, HelpY, SideW, HelpH, "Item Help");
    FillRect (Buf, W, H, SideX + 1, HelpY + 1, SideW - 2, 2, COL_ORANGE);

    // Simple word-wrap for contextual help inside sidebar
    {
      const char  *Msg = GetItemHelpText (HelpId);
      char        Line[48];
      INT32       Ly   = HelpY + 18;
      UINTN       Pos  = 0;

      while (Msg[Pos] != '\0' && Ly + 20 < HelpY + HelpH - 54) {
        UINTN  Len = 0;
        UINTN  LastSpace = 0;
        while (Msg[Pos + Len] != '\0' && Len < 40u) {
          if (Msg[Pos + Len] == ' ') LastSpace = Len;
          Len++;
        }
        if (Msg[Pos + Len] != '\0' && LastSpace > 0u) {
          Len = LastSpace;
        }
        BiosCopy (Line, Msg + Pos, Len);
        Line[Len] = '\0';
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, Ly, COL_TEXT_PRI, Line);
        Ly += 20;
        Pos += Len;
        while (Msg[Pos] == ' ') Pos++;
      }
    }

    FillRect (Buf, W, H, SideX + 12, HelpY + HelpH - 52, SideW - 24, 1, COL_DIVIDER);
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, HelpY + HelpH - 42, COL_TEXT_MUTED, "Arrows / Mouse: Select & Change");
    DrawTextFace (Buf, W, H, &mAuroraFontSmall, SideX + 14, HelpY + HelpH - 24, COL_TEXT_MUTED, "F2: Easy Mode   |   F10: Save & Exit");
  }

  // If System Info tab is open, draw full live hardware + input diagnostics table
  if (State->ActiveTab == AURORA_TAB_SYSINFO) {
    INT32  Sy = PaneY + 20;
    INT32  Step = 30;
    INT32  Vx = PaneX + 260;

    DrawTextFace (Buf, W, H, &mAuroraFontBold, PaneX + 18, Sy, COL_ORANGE_HI, "System & Native Input Diagnostics");
    Sy += 34;

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 0 * Step, COL_TEXT_SEC, "Platform / Board");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 0 * Step, COL_WHITE, State->Info.PlatformName);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 1 * Step, COL_TEXT_SEC, "Processor / SoC");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 1 * Step, COL_WHITE, State->Info.SocName);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 2 * Step, COL_TEXT_SEC, "UEFI Firmware Build");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 2 * Step, COL_WHITE, State->Info.FirmwareVersion);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 3 * Step, COL_TEXT_SEC, "Graphics Output (GOP)");
    BiosFmtU32 (Tmp, sizeof (Tmp), State->Info.GopWidth);
    BiosStrCat (Tmp, sizeof (Tmp), " x ");
    BiosFmtU32 (Num, sizeof (Num), State->Info.GopHeight);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), "  (32 bpp, 2x HiDPI)");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 3 * Step, COL_WHITE, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 4 * Step, COL_TEXT_SEC, "NVMe Storage / ESP");
    BiosFmtU32 (Num, sizeof (Num), (UINT32)State->Info.PrimaryDiskGiB);
    BiosStrCpy (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), " GB Apple ANS2  |  ESP: ");
    BiosStrCat (Tmp, sizeof (Tmp), State->Info.EspWritable ? "Writable" : "Read-Only");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 4 * Step, COL_WHITE, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 5 * Step, COL_TEXT_SEC, "Apple MTP Keyboard");
    BiosStrCpy (Tmp, sizeof (Tmp), State->Info.MtpKbReady ? "ACTIVE  (" : "INACTIVE  (");
    BiosFmtU32 (Num, sizeof (Num), State->Info.MtpKbReports);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), " reports, ");
    BiosFmtU32 (Num, sizeof (Num), State->Info.KeyPresses);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), " key events)");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 5 * Step, State->Info.MtpKbReady ? COL_GREEN : COL_AMBER, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 6 * Step, COL_TEXT_SEC, "Apple MTP Trackpad");
    BiosStrCpy (Tmp, sizeof (Tmp), State->Info.MtpTpReady ? "ACTIVE  (FW: " : "INACTIVE  (FW: ");
    BiosStrCat (Tmp, sizeof (Tmp), State->Info.MtpFwSource);
    BiosStrCat (Tmp, sizeof (Tmp), ", ");
    BiosFmtU32 (Num, sizeof (Num), State->Info.MtpTpReports);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), " reports)");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 6 * Step, State->Info.MtpTpReady ? COL_GREEN : COL_AMBER, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 7 * Step, COL_TEXT_SEC, "Keyboard Backlight (FPWM0)");
    GetControlValueStr (State, CTRL_SET_KBD_BACKLIGHT, Tmp, sizeof (Tmp));
    BiosStrCat (Tmp, sizeof (Tmp), State->Info.KblActive ? "  (Hardware Active)" : "  (Unavailable)");
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 7 * Step, State->Info.KblActive ? COL_ORANGE_HI : COL_TEXT_MUTED, Tmp);

    DrawTextFace (Buf, W, H, &mAuroraFontBody, PaneX + 18, Sy + 8 * Step, COL_TEXT_SEC, "Live Pointer Telemetry");
    BiosStrCpy (Tmp, sizeof (Tmp), "Pos (");
    BiosFmtI32 (Num, sizeof (Num), State->MouseX);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), ", ");
    BiosFmtI32 (Num, sizeof (Num), State->MouseY);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), ")  |  Delta (");
    BiosFmtI32 (Num, sizeof (Num), State->Info.LastRawDx);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), ", ");
    BiosFmtI32 (Num, sizeof (Num), State->Info.LastRawDy);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    BiosStrCat (Tmp, sizeof (Tmp), ")  |  Clicks: ");
    BiosFmtU32 (Num, sizeof (Num), State->Info.MouseClicks);
    BiosStrCat (Tmp, sizeof (Tmp), Num);
    DrawTextFace (Buf, W, H, &mAuroraFontBody, Vx,         Sy + 8 * Step, COL_TEXT_PRI, Tmp);
  } else if (State->ActiveTab == AURORA_TAB_BOOT) {
    DrawTextFace (Buf, W, H, &mAuroraFontBold, PaneX + 18, PaneY + 100, COL_ORANGE_HI, "UEFI Boot Option Priorities (Double-Click to Boot)");
  }

  // Render all registered Advanced Mode controls
  for (I = 0; I < State->ControlCount; I++) {
    const AURORA_UI_RECT  *R = &State->Controls[I];
    BOOLEAN               Hov = (I == State->HoveredCtrlIdx);
    BOOLEAN               Foc = (I == State->FocusedCtrlIdx);

    if (R->Kind == AURORA_CTRL_MODE_TAB || R->Kind == AURORA_CTRL_UTIL_BTN) {
      continue;
    }

    if (R->Kind == AURORA_CTRL_TAB) {
      BOOLEAN  Act = ((AURORA_BIOS_TAB)R->Param == State->ActiveTab);
      if (Act) {
        FillGradV (Buf, W, H, R->X, R->Y, R->W, R->H, 0x00241005u, 0x00100702u);
        FillRect (Buf, W, H, R->X, R->Y + R->H - 2, R->W, 2, COL_ORANGE_HI);
      } else if (Hov) {
        FillRect (Buf, W, H, R->X, R->Y, R->W, R->H, 0x00181818u);
      }
      DrawTextCentered (Buf, W, H, &mAuroraFontBold, R->X, R->Y, R->W, R->H,
        Act ? COL_ORANGE_HI : (Hov ? COL_WHITE : 0x008A8A8Au),
        SubTabNames[R->Param % AURORA_TAB_COUNT]);
    } else if (R->Kind == AURORA_CTRL_TOGGLE) {
      BOOLEAN  On = GetControlToggleBool (State, R->Id);
      FillRect (Buf, W, H, R->X, R->Y, R->W, R->H, (Foc || Hov) ? (Foc ? COL_BG_ROW_FOC : COL_BG_ROW_HOV) : COL_BG_ROW_ALT);
      if (Foc || Hov) {
        FillRect (Buf, W, H, R->X, R->Y, 3, R->H, COL_ORANGE_HI);
      }
      DrawTextFace (Buf, W, H, &mAuroraFontBody, R->X + 16, R->Y + (R->H - (INT32)mAuroraFontBody.LineHeight) / 2,
        (Foc || Hov) ? COL_WHITE : COL_TEXT_PRI, GetControlLabel (R->Id));
      DrawGigabyteToggle (Buf, W, H, R->X + R->W - 60, R->Y + (R->H - 18) / 2, On);
    } else if (R->Kind == AURORA_CTRL_CHOICE) {
      FillRect (Buf, W, H, R->X, R->Y, R->W, R->H, (Foc || Hov) ? (Foc ? COL_BG_ROW_FOC : COL_BG_ROW_HOV) : COL_BG_ROW_ALT);
      if (Foc || Hov) {
        FillRect (Buf, W, H, R->X, R->Y, 3, R->H, COL_ORANGE_HI);
      }
      DrawTextFace (Buf, W, H, &mAuroraFontBody, R->X + 16, R->Y + (R->H - (INT32)mAuroraFontBody.LineHeight) / 2,
        (Foc || Hov) ? COL_WHITE : COL_TEXT_PRI, GetControlLabel (R->Id));
      GetControlValueStr (State, R->Id, Tmp, sizeof (Tmp));
      DrawGigabyteDropdown (Buf, W, H, R->X + R->W - 180, R->Y + 4, 166, R->H - 8, Hov, Foc, Tmp);
    } else if (R->Kind == AURORA_CTRL_BOOT_ROW) {
      UINT32   Idx = R->Param;
      BOOLEAN  Sel = (Idx == State->SelectedBootIdx);
      FillRect (Buf, W, H, R->X, R->Y, R->W, R->H, (Sel || Foc || Hov) ? (Sel ? COL_BG_ROW_FOC : COL_BG_ROW_HOV) : COL_BG_ROW_ALT);
      if (Sel || Foc) {
        FillRect (Buf, W, H, R->X, R->Y, 3, R->H, COL_ORANGE_HI);
      }
      if (Idx < State->BootEntryCount) {
        BiosFmtU32 (Num, sizeof (Num), Idx + 1u);
        BiosStrCpy (Tmp, sizeof (Tmp), "Boot Option #");
        BiosStrCat (Tmp, sizeof (Tmp), Num);
        DrawTextFace (Buf, W, H, &mAuroraFontSmall, R->X + 14, R->Y + 7, COL_TEXT_MUTED, Tmp);
        DrawTextFace (Buf, W, H, &mAuroraFontBody, R->X + 140, R->Y + 6, Sel ? COL_WHITE : COL_TEXT_PRI, State->BootEntries[Idx].Description);
      }
    } else if (R->Kind == AURORA_CTRL_BUTTON) {
      const char  *Lbl  = "Action";
      UINT32      TCol  = (Hov || Foc) ? COL_WHITE : COL_TEXT_PRI;
      switch (R->Id) {
        case CTRL_BTN_BOOT_UP:      Lbl = "Move Up"; break;
        case CTRL_BTN_BOOT_DOWN:    Lbl = "Move Down"; break;
        case CTRL_BTN_BOOT_NOW:     Lbl = "Boot Selected Entry"; TCol = COL_ORANGE_HI; break;
        case CTRL_BTN_LAUNCH_SHELL: Lbl = "Launch UEFI Shell"; break;
        case CTRL_BTN_SCAN_USB:     Lbl = "Re-Scan Input Devices"; break;
        case CTRL_BTN_LOAD_DEFAULTS:Lbl = "Load Optimized Defaults (F7)"; TCol = COL_AMBER; break;
        case CTRL_BTN_SAVE_EXIT:    Lbl = "Save & Exit Setup (F10)"; TCol = COL_ORANGE_HI; break;
        case CTRL_BTN_SAVE_ONLY:    Lbl = "Save Configuration to NVRAM + ESP"; break;
        case CTRL_BTN_DISCARD_EXIT: Lbl = "Discard Changes & Exit Setup"; break;
        case CTRL_BTN_SAVE_REBOOT:  Lbl = "Save Changes & Cold Reboot"; break;
        case CTRL_BTN_POWER_OFF:    Lbl = "Power Off System"; TCol = COL_RED; break;
        default: break;
      }
      DrawMetallicButton (Buf, W, H, R->X, R->Y, R->W, R->H, Hov, Foc, TCol, Lbl);
    }
  }
}

// ---------------------------------------------------------------------------
// Help Modal Overlay (F1)
// ---------------------------------------------------------------------------
static VOID
RenderHelpModal (
  const AURORA_BIOS_STATE  *State,
  UINT32                   *Buf
  )
{
  INT32  W  = State->Width;
  INT32  H  = State->Height;
  INT32  MW = 560;
  INT32  MH = 340;
  INT32  MX = (W - MW) / 2;
  INT32  MY = (H - MH) / 2;
  INT32  Y  = MY + 56;

  // Dim backdrop
  {
    INT32  Py;
    INT32  Px;
    for (Py = 0; Py < H; Py++) {
      UINT32  *Row = Buf + Py * W;
      for (Px = 0; Px < W; Px++) {
        Row[Px] = ((Row[Px] >> 1) & 0x007F7F7Fu);
      }
    }
  }

  FillRect (Buf, W, H, MX, MY, MW, MH, 0x000A0A0Au);
  DrawRectBorder (Buf, W, H, MX, MY, MW, MH, COL_ORANGE);
  FillGradV (Buf, W, H, MX + 1, MY + 1, MW - 2, 36, 0x003D1806u, 0x00160702u);
  DrawTextCentered (Buf, W, H, &mAuroraFontBold, MX, MY + 1, MW, 36, COL_ORANGE_HI, "Aurora UEFI Setup - Controls & Shortcuts");

  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 0 * 30, COL_WHITE, "Trackpad / Mouse : Move cursor, click or tap-to-click any control");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 1 * 30, COL_WHITE, "F2               : Switch between Easy Mode and Advanced Mode");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 2 * 30, COL_WHITE, "Up / Down / Tab  : Select setting or button");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 3 * 30, COL_WHITE, "Left / Right / Enter : Toggle or cycle setting value");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 4 * 30, COL_WHITE, "F7               : Load Optimized Defaults");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 5 * 30, COL_WHITE, "F10              : Save Settings & Boot OS");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 6 * 30, COL_WHITE, "F11              : Launch UEFI Shell");
  DrawTextFace (Buf, W, H, &mAuroraFontBody, MX + 28, Y + 7 * 30, COL_WHITE, "ESC              : Discard & Exit Setup");

  DrawTextCentered (Buf, W, H, &mAuroraFontSmall, MX, MY + MH - 36, MW, 24, COL_ORANGE_HI, "Press any key or click anywhere to close");
}

// ---------------------------------------------------------------------------
// Complete Frame Renderer
// ---------------------------------------------------------------------------
VOID
AuroraBiosRender (
  AURORA_BIOS_STATE  *State,
  UINT32             *BackBuffer
  )
{
  static const char  *UtilLabels[6] = {
    "Help (F1)", "Input Info", "Load Defaults (F7)",
    "Save & Exit (F10)", "UEFI Shell (F11)", "Boot OS (ESC)"
  };
  INT32  W = State->Width;
  INT32  H = State->Height;
  INT32  I;

  // 1. Pure obsidian-black background (1:1 Gigabyte canvas)
  FillRect (BackBuffer, W, H, 0, 0, W, H, COL_BLACK);

  // Subtle dark warm metallic header background above orange streak
  FillGradV (BackBuffer, W, H, 0, 0, W, 50, 0x0016100Cu, COL_BLACK);

  // 2. Top-left bold white AURORA wordmark
  DrawAuroraLogo (BackBuffer, W, H, 28, 13);

  // 3. Signature top horizontal molten-orange light bar
  DrawOrangeLightBar (BackBuffer, W, H, 51, 6);

  // 4. Slanted EASY MODE / ADVANCED MODE tabs (Controls[0..1])
  for (I = 0; I < State->ControlCount; I++) {
    const AURORA_UI_RECT  *R = &State->Controls[I];
    if (R->Kind == AURORA_CTRL_MODE_TAB) {
      BOOLEAN  Act = ((AURORA_BIOS_MODE)R->Param == State->Mode);
      BOOLEAN  Hov = (I == State->HoveredCtrlIdx);
      DrawSlantedModeTab (
        BackBuffer, W, H, R->X, R->Y, R->W, R->H, Act, Hov,
        (R->Param == AURORA_MODE_EASY) ? "EASY MODE" : "ADVANCED MODE"
        );
    } else if (R->Kind == AURORA_CTRL_UTIL_BTN) {
      BOOLEAN  Hov = (I == State->HoveredCtrlIdx);
      BOOLEAN  Foc = (I == State->FocusedCtrlIdx);
      const char  *Lbl = UtilLabels[R->Param % 6u];
      if (R->Param == 1u && State->Info.MtpKbReady) {
        Lbl = State->Info.MtpTpReady ? "MTP: KB+TP LIVE" : "MTP: KB LIVE";
      }
      DrawMetallicButton (
        BackBuffer, W, H, R->X, R->Y, R->W, R->H, Hov, Foc,
        (R->Param == 1u && State->Info.MtpKbReady) ? COL_GREEN : ((Hov || Foc) ? COL_WHITE : COL_TEXT_PRI),
        Lbl
        );
    }
  }

  // Top-right clock & live status indicator
  {
    char  Clk[16];
    char  Hh[4];
    char  Mm[4];
    char  Ss[4];
    if (State->Info.ClockHour != 0u || State->Info.ClockMin != 0u || State->Info.ClockSec != 0u) {
      BiosFmt2D (Hh, State->Info.ClockHour);
      BiosFmt2D (Mm, State->Info.ClockMin);
      BiosFmt2D (Ss, State->Info.ClockSec);
    } else {
      BiosFmt2D (Hh, (State->Info.UptimeSec / 3600u) % 24u);
      BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 60u);
      BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
    }
    BiosStrCpy (Clk, sizeof (Clk), Hh);
    BiosStrCat (Clk, sizeof (Clk), ":");
    BiosStrCat (Clk, sizeof (Clk), Mm);
    BiosStrCat (Clk, sizeof (Clk), ":");
    BiosStrCat (Clk, sizeof (Clk), Ss);
    // Live blinking heartbeat dot every second
    FillRect (BackBuffer, W, H, W - 102, 20, 6, 6, (State->Info.UptimeSec & 1u) ? COL_ORANGE_HI : COL_ORANGE_DEEP);
    DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, W - 90, 16, COL_TEXT_SEC, Clk);
  }

  // 5. Render Active Mode Content
  if (State->Mode == AURORA_MODE_EASY) {
    RenderEasyMode (State, BackBuffer);
  } else {
    RenderAdvancedMode (State, BackBuffer);
  }

  // 6. Bottom Status Bar + Signature Molten-Orange Bottom Edge Streak
  DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, 24, H - 36, State->StatusColor, State->StatusMsg);
  DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, W - 404, H - 36, COL_TEXT_MUTED, "F1: Help   F2: Easy/Advanced   F7: Defaults   F10: Save & Exit");
  DrawOrangeLightBar (BackBuffer, W, H, H - 6, 6);

  // 7. Optional Help Modal Overlay
  if (State->ShowHelpModal) {
    RenderHelpModal (State, BackBuffer);
  }
}

// ---------------------------------------------------------------------------
// Fast Surgical 1-Second Dynamic Stats Redraw (Zero Full-Screen Redraw Lag!)
// ---------------------------------------------------------------------------
UINT32
AuroraBiosRenderDynamicOnly (
  const AURORA_BIOS_STATE  *State,
  UINT32                   *BackBuffer,
  AURORA_DIRTY_RECT        OutRects[3]
  )
{
  INT32   W     = State->Width;
  INT32   H     = State->Height;
  UINT32  Count = 0;
  char    Tmp[64];
  char    Num[24];
  char    Hh[4];
  char    Mm[4];
  char    Ss[4];

  if (State->ShowHelpModal) {
    return 0;
  }

  // 1. Top-right clock & blinking heartbeat dot
  {
    INT32  Cx = W - 110;
    INT32  Cy = 10;
    INT32  Cw = 104;
    INT32  Ch = 26;

    FillGradV (BackBuffer, W, H, Cx, Cy, Cw, Ch, 0x00110C09u, 0x00070504u);
    if (State->Info.ClockHour != 0u || State->Info.ClockMin != 0u || State->Info.ClockSec != 0u) {
      BiosFmt2D (Hh, State->Info.ClockHour);
      BiosFmt2D (Mm, State->Info.ClockMin);
      BiosFmt2D (Ss, State->Info.ClockSec);
    } else {
      BiosFmt2D (Hh, (State->Info.UptimeSec / 3600u) % 24u);
      BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 60u);
      BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
    }
    BiosStrCpy (Tmp, sizeof (Tmp), Hh);
    BiosStrCat (Tmp, sizeof (Tmp), ":");
    BiosStrCat (Tmp, sizeof (Tmp), Mm);
    BiosStrCat (Tmp, sizeof (Tmp), ":");
    BiosStrCat (Tmp, sizeof (Tmp), Ss);
    FillRect (BackBuffer, W, H, W - 102, 20, 6, 6, (State->Info.UptimeSec & 1u) ? COL_ORANGE_HI : COL_ORANGE_DEEP);
    DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, W - 90, 16, COL_TEXT_SEC, Tmp);

    OutRects[Count].Valid = TRUE;
    OutRects[Count].MinX  = Cx;
    OutRects[Count].MinY  = Cy;
    OutRects[Count].MaxX  = Cx + Cw;
    OutRects[Count].MaxY  = Cy + Ch;
    Count++;
  }

  if (State->Mode == AURORA_MODE_EASY) {
    // 2. Easy Mode: Setup Uptime hero readout (pure black background!)
    INT32  Ux = 786;
    INT32  Uy = 240;
    INT32  Uw = 156;
    INT32  Uh = 42;
    FillRect (BackBuffer, W, H, Ux, Uy, Uw, Uh, COL_BLACK);
    BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 100u);
    BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
    BiosStrCpy (Tmp, sizeof (Tmp), Mm);
    BiosStrCat (Tmp, sizeof (Tmp), ":");
    BiosStrCat (Tmp, sizeof (Tmp), Ss);
    DrawTextFace (BackBuffer, W, H, &mAuroraFontHero, 790, 242, COL_WHITE, Tmp);
    DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, 790 + MeasureText (&mAuroraFontHero, Tmp) + 6, 256, COL_ORANGE_HI, "LIVE");

    OutRects[Count].Valid = TRUE;
    OutRects[Count].MinX  = Ux;
    OutRects[Count].MinY  = Uy;
    OutRects[Count].MaxX  = Ux + Uw;
    OutRects[Count].MaxY  = Uy + Uh;
    Count++;

    // 3. Easy Mode: Peripherals live RX packet counter
    {
      INT32  Rx = 674 + 62;
      INT32  Ry = 338 + 18 + 4 * 28 - 2;
      INT32  Rw = (W - 324 - 14 - 674) - 66;
      INT32  Rh = 22;
      if (Rw > 40) {
        FillRect (BackBuffer, W, H, Rx, Ry, Rw, Rh, COL_BG_CARD);
        BiosFmtU32 (Tmp, sizeof (Tmp), State->Info.MtpPacketsRx);
        BiosStrCat (Tmp, sizeof (Tmp), " pkts (");
        BiosFmtU32 (Num, sizeof (Num), State->Info.MtpKbReports);
        BiosStrCat (Tmp, sizeof (Tmp), Num);
        BiosStrCat (Tmp, sizeof (Tmp), "K / ");
        BiosFmtU32 (Num, sizeof (Num), State->Info.MtpTpReports);
        BiosStrCat (Tmp, sizeof (Tmp), Num);
        BiosStrCat (Tmp, sizeof (Tmp), "T)");
        DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, 674 + 64, 338 + 18 + 4 * 28, COL_TEXT_SEC, Tmp);

        OutRects[Count].Valid = TRUE;
        OutRects[Count].MinX  = Rx;
        OutRects[Count].MinY  = Ry;
        OutRects[Count].MaxX  = Rx + Rw;
        OutRects[Count].MaxY  = Ry + Rh;
        Count++;
      }
    }
  } else {
    // 2. Advanced Mode: Hardware Monitor Uptime row
    INT32  SideX = 24 + (W - 384) + 16;
    INT32  Vx    = SideX + 132;
    INT32  Uy    = 148 + 36 + 8 * 28 - 2;
    INT32  Uw    = 96;
    INT32  Uh    = 22;
    FillRect (BackBuffer, W, H, Vx - 2, Uy, Uw, Uh, COL_BG_CARD);
    BiosFmt2D (Mm, (State->Info.UptimeSec / 60u) % 100u);
    BiosFmt2D (Ss, State->Info.UptimeSec % 60u);
    BiosStrCpy (Tmp, sizeof (Tmp), Mm);
    BiosStrCat (Tmp, sizeof (Tmp), ":");
    BiosStrCat (Tmp, sizeof (Tmp), Ss);
    DrawTextFace (BackBuffer, W, H, &mAuroraFontSmall, Vx, 148 + 36 + 8 * 28, COL_WHITE, Tmp);

    OutRects[Count].Valid = TRUE;
    OutRects[Count].MinX  = Vx - 2;
    OutRects[Count].MinY  = Uy;
    OutRects[Count].MaxX  = Vx - 2 + Uw;
    OutRects[Count].MaxY  = Uy + Uh;
    Count++;
  }

  return Count;
}

// ---------------------------------------------------------------------------
// Crisp Pointer Arrow Sprite (High-Visibility 18x22 White + Black + Orange)
// ---------------------------------------------------------------------------
VOID
AuroraBiosDrawCursor (
  UINT32  *BackBuffer,
  INT32   Width,
  INT32   Height,
  INT32   MouseX,
  INT32   MouseY
  )
{
  static const char  *Sprite[18] = {
    "X...............",
    "XX..............",
    "XWX.............",
    "XWWX............",
    "XWWWX...........",
    "XWWWWX..........",
    "XWWWWWX.........",
    "XWWWWWWX........",
    "XWWWWWWWX.......",
    "XWWWWWWWWX......",
    "XWWWWWWWWWX.....",
    "XWWWWWWXXXXX....",
    "XWWXOWWX........",
    "XWX.XWWX........",
    "XX...XWWX.......",
    "X....XWWX.......",
    "......XX........",
    "................"
  };
  INT32  Sy;
  INT32  Sx;

  for (Sy = 0; Sy < 18; Sy++) {
    for (Sx = 0; Sx < 16; Sx++) {
      char  C = Sprite[Sy][Sx];
      if (C == 'X') {
        PutPx (BackBuffer, Width, Height, MouseX + Sx, MouseY + Sy, COL_BLACK);
      } else if (C == 'W') {
        PutPx (BackBuffer, Width, Height, MouseX + Sx, MouseY + Sy, COL_WHITE);
      } else if (C == 'O') {
        PutPx (BackBuffer, Width, Height, MouseX + Sx, MouseY + Sy, COL_ORANGE_HI);
      }
    }
  }
}

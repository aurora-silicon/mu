/** @file
  Aurora Silicon Gaming UEFI BIOS - Core UI Layout, Input & Software Compositor.

  Pure C, freestanding (no libc required), compiles both inside EDK2/Mu AArch64
  and natively on host for unit tests (-DAURORA_BIOS_HOST_TEST).

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AuroraBiosConfig.h"
#include "AuroraBiosFont.h"

// ---------------------------------------------------------------------------
// Palette (Carbon Obsidian + Electric Cyan + Solar Amber + Emerald Telemetry)
// ---------------------------------------------------------------------------
#define COL_BG            0x000A0E17u
#define COL_BG_GRAD_TOP   0x000F1726u
#define COL_HEADER_BG     0x00111927u
#define COL_CARD_BG       0x00141D2Cu
#define COL_CARD_ALT      0x00192436u
#define COL_CARD_HOVER    0x001F2E46u
#define COL_BORDER        0x0024354Eu
#define COL_BORDER_HI     0x0000E5FFu
#define COL_ACCENT        0x0000E5FFu  // Electric Cyan
#define COL_ACCENT_DIM    0x000A4F63u
#define COL_ACCENT_DARK   0x000C2D3Eu
#define COL_AMBER         0x00F59E0Bu  // Solar Amber
#define COL_AMBER_DARK    0x003F2A08u
#define COL_GREEN         0x0010B981u  // Emerald OK
#define COL_GREEN_DARK    0x000A3528u
#define COL_RED           0x00EF4444u  // Crimson Warning
#define COL_RED_DARK      0x003D1418u
#define COL_TEXT_BRIGHT   0x00F8FAFCu
#define COL_TEXT          0x00CBD5E1u
#define COL_TEXT_MUTED    0x0064748Bu
#define COL_FOOTER_BG     0x000D131Fu

// Control IDs
enum {
  CID_TAB_BASE            = 100, // 100..104
  // Dashboard
  CID_PROFILE_BALANCED    = 200,
  CID_PROFILE_PERFORMANCE = 201,
  CID_PROFILE_QUIET       = 202,
  CID_BTN_BOOT_OS         = 210,
  CID_BTN_UEFI_SHELL      = 211,
  CID_BTN_SCAN_USB        = 212,
  CID_BTN_SAVE_REBOOT     = 213,
  // Boot Manager
  CID_BOOT_ROW_BASE       = 300, // 300..307
  CID_BOOT_MOVE_UP        = 320,
  CID_BOOT_MOVE_DOWN      = 321,
  CID_BOOT_LAUNCH_SEL     = 322,
  CID_SLIDER_TIMEOUT      = 330,
  CID_TOGGLE_FASTBOOT     = 331,
  CID_TOGGLE_SPLASH       = 332,
  CID_CHOICE_DEF_TARGET   = 333,
  // Hardware & GPU
  CID_TOGGLE_GPU_POOL     = 400,
  CID_CHOICE_CON_SCALE    = 401,
  CID_SLIDER_MOUSE_SPEED  = 402,
  CID_CHOICE_SERIAL_DBG   = 403,
  CID_TOGGLE_USB_AUTO     = 404,
  CID_TOGGLE_USB_BOOT     = 405,
  // Diagnostics
  CID_DIAG_SCAN_USB       = 500,
  CID_DIAG_RESET_INPUT    = 501,
  // Save & Exit
  CID_EXIT_SAVE_BOOT      = 600,
  CID_EXIT_SAVE_REBOOT    = 601,
  CID_EXIT_DISCARD_BOOT   = 602,
  CID_EXIT_LOAD_DEFAULTS  = 603,
  CID_EXIT_SHELL          = 604,
  CID_EXIT_POWER_OFF      = 605
};

// ---------------------------------------------------------------------------
// Freestanding String / Formatting / Memory Helpers
// ---------------------------------------------------------------------------
static VOID
ZeroBytes (
  VOID   *Dst,
  UINTN  Len
  )
{
  UINT8  *P = (UINT8 *)Dst;
  while (Len--) {
    *P++ = 0;
  }
}

static VOID
CopyBytes (
  VOID        *Dst,
  const VOID  *Src,
  UINTN       Len
  )
{
  UINT8        *D = (UINT8 *)Dst;
  const UINT8  *S = (const UINT8 *)Src;
  while (Len--) {
    *D++ = *S++;
  }
}

static UINTN
StrLenA (
  const char  *S
  )
{
  UINTN  N = 0;
  if (S == NULL) {
    return 0;
  }
  while (S[N] != '\0') {
    N++;
  }
  return N;
}

static VOID
StrCopyA (
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
StrCatA (
  char        *Dst,
  UINTN       DstSize,
  const char  *Src
  )
{
  UINTN  Len = StrLenA (Dst);
  if (Len < DstSize) {
    StrCopyA (Dst + Len, DstSize - Len, Src);
  }
}

static VOID
AppendU64 (
  char    *Dst,
  UINTN   DstSize,
  UINT64  Val,
  UINTN   MinDigits
  )
{
  char   Tmp[24];
  UINTN  N = 0;
  char   Out[24];
  UINTN  O = 0;

  if (Val == 0) {
    Tmp[N++] = '0';
  } else {
    while (Val > 0 && N < sizeof (Tmp)) {
      Tmp[N++] = (char)('0' + (Val % 10));
      Val /= 10;
    }
  }
  while (N < MinDigits && N < sizeof (Tmp)) {
    Tmp[N++] = '0';
  }
  while (N > 0 && O + 1 < sizeof (Out)) {
    Out[O++] = Tmp[--N];
  }
  Out[O] = '\0';
  StrCatA (Dst, DstSize, Out);
}

static VOID
AppendI32 (
  char   *Dst,
  UINTN  DstSize,
  INT32  Val
  )
{
  if (Val < 0) {
    StrCatA (Dst, DstSize, "-");
    AppendU64 (Dst, DstSize, (UINT64)(-(INT64)Val), 1);
  } else {
    AppendU64 (Dst, DstSize, (UINT64)Val, 1);
  }
}

static VOID
AppendHex16 (
  char    *Dst,
  UINTN   DstSize,
  UINT16  Val
  )
{
  static const char  Hex[] = "0123456789ABCDEF";
  char               Buf[5];
  Buf[0] = Hex[(Val >> 12) & 0xF];
  Buf[1] = Hex[(Val >> 8) & 0xF];
  Buf[2] = Hex[(Val >> 4) & 0xF];
  Buf[3] = Hex[Val & 0xF];
  Buf[4] = '\0';
  StrCatA (Dst, DstSize, Buf);
}

static INT32
ClampI32 (
  INT32  V,
  INT32  Lo,
  INT32  Hi
  )
{
  if (V < Lo) {
    return Lo;
  }
  if (V > Hi) {
    return Hi;
  }
  return V;
}

// ---------------------------------------------------------------------------
// CRC32 & Configuration Persistence Helpers
// ---------------------------------------------------------------------------
UINT32
AuroraBiosCrc32 (
  const VOID  *Data,
  UINTN       Length
  )
{
  const UINT8  *Bytes = (const UINT8 *)Data;
  UINT32       Crc    = 0xFFFFFFFFu;
  UINTN        I;
  UINTN        Bit;

  for (I = 0; I < Length; I++) {
    Crc ^= (UINT32)Bytes[I];
    for (Bit = 0; Bit < 8; Bit++) {
      if ((Crc & 1u) != 0) {
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
  if (Cfg == NULL) {
    return;
  }
  ZeroBytes (Cfg, sizeof (*Cfg));
  Cfg->Magic              = AURORA_BIOS_CONFIG_MAGIC;
  Cfg->Version            = AURORA_BIOS_CONFIG_VERSION;
  Cfg->PerformanceProfile = AURORA_PROFILE_PERFORMANCE;
  Cfg->BootTimeoutSec     = 0;   // 0s default keeps unattended boots instant
  Cfg->FastBoot           = 1;
  Cfg->BootSplashLogo     = 1;
  Cfg->DefaultBootTarget  = AURORA_BOOT_TARGET_OS;
  Cfg->GpuLocalPoolEnable = 1;
  Cfg->ConsoleScaleMode   = 1;   // 1x native 2560x1664 coherent with Windows
  Cfg->MouseSpeed         = 5;
  Cfg->SerialDebugLevel   = AURORA_DEBUG_ERRORS;
  Cfg->UsbHidAutoConnect  = 0;   // On-demand safe default
  Cfg->ExternalUsbBoot    = 1;
  Cfg->EnterBiosOnce      = 0;
  AuroraBiosConfigFinalize (Cfg);
}

VOID
AuroraBiosConfigFinalize (
  AURORA_BIOS_CONFIG  *Cfg
  )
{
  if (Cfg == NULL) {
    return;
  }
  Cfg->Magic   = AURORA_BIOS_CONFIG_MAGIC;
  Cfg->Version = AURORA_BIOS_CONFIG_VERSION;
  if (Cfg->PerformanceProfile > AURORA_PROFILE_QUIET) {
    Cfg->PerformanceProfile = AURORA_PROFILE_PERFORMANCE;
  }
  if (Cfg->BootTimeoutSec > 10u) {
    Cfg->BootTimeoutSec = 10u;
  }
  if (Cfg->MouseSpeed < 1u) {
    Cfg->MouseSpeed = 1u;
  }
  if (Cfg->MouseSpeed > 10u) {
    Cfg->MouseSpeed = 10u;
  }
  if (Cfg->ConsoleScaleMode != 1u && Cfg->ConsoleScaleMode != 2u) {
    Cfg->ConsoleScaleMode = 1u;
  }
  if (Cfg->DefaultBootTarget > AURORA_BOOT_TARGET_BIOS) {
    Cfg->DefaultBootTarget = AURORA_BOOT_TARGET_OS;
  }
  if (Cfg->SerialDebugLevel > AURORA_DEBUG_VERBOSE) {
    Cfg->SerialDebugLevel = AURORA_DEBUG_ERRORS;
  }
  Cfg->Crc32 = AuroraBiosCrc32 (Cfg, AURORA_OFFSET_OF (AURORA_BIOS_CONFIG, Crc32));
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
  if (Cfg->Magic != AURORA_BIOS_CONFIG_MAGIC ||
      Cfg->Version != AURORA_BIOS_CONFIG_VERSION) {
    return FALSE;
  }
  Expected = AuroraBiosCrc32 (Cfg, AURORA_OFFSET_OF (AURORA_BIOS_CONFIG, Crc32));
  return (Cfg->Crc32 == Expected) ? TRUE : FALSE;
}

UINT32
AuroraBiosCountModified (
  const AURORA_BIOS_STATE  *State
  )
{
  UINT32                    Diff = 0;
  const AURORA_BIOS_CONFIG  *A   = &State->Config;
  const AURORA_BIOS_CONFIG  *B   = &State->SavedConfig;

  if (A->PerformanceProfile != B->PerformanceProfile) Diff++;
  if (A->BootTimeoutSec != B->BootTimeoutSec)         Diff++;
  if (A->FastBoot != B->FastBoot)                     Diff++;
  if (A->BootSplashLogo != B->BootSplashLogo)         Diff++;
  if (A->DefaultBootTarget != B->DefaultBootTarget)   Diff++;
  if (A->GpuLocalPoolEnable != B->GpuLocalPoolEnable) Diff++;
  if (A->ConsoleScaleMode != B->ConsoleScaleMode)     Diff++;
  if (A->MouseSpeed != B->MouseSpeed)                 Diff++;
  if (A->SerialDebugLevel != B->SerialDebugLevel)     Diff++;
  if (A->UsbHidAutoConnect != B->UsbHidAutoConnect)   Diff++;
  if (A->ExternalUsbBoot != B->ExternalUsbBoot)       Diff++;
  if (State->BootOrderDirty)                          Diff++;
  return Diff;
}

// ---------------------------------------------------------------------------
// Dirty Rect Helpers
// ---------------------------------------------------------------------------
static VOID
MarkDirty (
  AURORA_BIOS_STATE  *State,
  INT32              X,
  INT32              Y,
  INT32              W,
  INT32              H
  )
{
  INT32  X1 = ClampI32 (X, 0, State->Width);
  INT32  Y1 = ClampI32 (Y, 0, State->Height);
  INT32  X2 = ClampI32 (X + W, 0, State->Width);
  INT32  Y2 = ClampI32 (Y + H, 0, State->Height);

  if (X2 <= X1 || Y2 <= Y1) {
    return;
  }
  if (!State->Dirty.Valid) {
    State->Dirty.Valid = TRUE;
    State->Dirty.MinX  = X1;
    State->Dirty.MinY  = Y1;
    State->Dirty.MaxX  = X2;
    State->Dirty.MaxY  = Y2;
  } else {
    if (X1 < State->Dirty.MinX) State->Dirty.MinX = X1;
    if (Y1 < State->Dirty.MinY) State->Dirty.MinY = Y1;
    if (X2 > State->Dirty.MaxX) State->Dirty.MaxX = X2;
    if (Y2 > State->Dirty.MaxY) State->Dirty.MaxY = Y2;
  }
}

static VOID
MarkFullRedraw (
  AURORA_BIOS_STATE  *State
  )
{
  State->FullRedrawNeeded = TRUE;
  MarkDirty (State, 0, 0, State->Width, State->Height);
}

VOID
AuroraBiosSetStatus (
  AURORA_BIOS_STATE  *State,
  const char         *Message,
  UINT32             ColorRgb
  )
{
  StrCopyA (State->StatusMsg, sizeof (State->StatusMsg), Message);
  State->StatusColor = ColorRgb;
  MarkFullRedraw (State);
}

// ---------------------------------------------------------------------------
// Layout Engine (responsive to any canvas size >= 800x600; ideal 1280x832)
// ---------------------------------------------------------------------------
static VOID
AddCtrl (
  AURORA_BIOS_STATE  *S,
  INT32              X,
  INT32              Y,
  INT32              W,
  INT32              H,
  AURORA_CTRL_KIND   Kind,
  UINT32             Id,
  UINT32             Param
  )
{
  AURORA_UI_RECT  *R;
  if (S->ControlCount >= AURORA_BIOS_MAX_CONTROLS) {
    return;
  }
  R        = &S->Controls[S->ControlCount++];
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
  AURORA_BIOS_STATE  *S
  )
{
  INT32   M     = 24;
  INT32   TabY  = 66;
  INT32   TabH  = 38;
  INT32   TabW  = (S->Width - M * 2 - 4 * 10) / AURORA_TAB_COUNT;
  INT32   BodyY = 122;
  INT32   BodyH = S->Height - BodyY - 52;
  INT32   I;

  S->ControlCount = 0;

  // 5 Top Navigation Tabs
  for (I = 0; I < AURORA_TAB_COUNT; I++) {
    AddCtrl (
      S,
      M + I * (TabW + 10),
      TabY,
      TabW,
      TabH,
      AURORA_CTRL_TAB,
      (UINT32)(CID_TAB_BASE + I),
      (UINT32)I
      );
  }

  if (S->ActiveTab == AURORA_TAB_DASHBOARD) {
    // 3 columns: Left (Telemetry), Mid (Gauges), Right (Quick Profile & Actions)
    INT32  ColGap = 18;
    INT32  ColW   = (S->Width - M * 2 - ColGap * 2) / 3;
    INT32  RightX = M + (ColW + ColGap) * 2;
    INT32  InnerX = RightX + 18;
    INT32  InnerW = ColW - 36;
    INT32  PillW  = (InnerW - 12) / 3;
    INT32  Y;

    // Performance Profile 3-pill selector
    Y = BodyY + 54;
    AddCtrl (S, InnerX, Y, PillW, 38, AURORA_CTRL_CHOICE, CID_PROFILE_BALANCED, AURORA_PROFILE_BALANCED);
    AddCtrl (S, InnerX + PillW + 6, Y, PillW, 38, AURORA_CTRL_CHOICE, CID_PROFILE_PERFORMANCE, AURORA_PROFILE_PERFORMANCE);
    AddCtrl (S, InnerX + (PillW + 6) * 2, Y, PillW, 38, AURORA_CTRL_CHOICE, CID_PROFILE_QUIET, AURORA_PROFILE_QUIET);

    // Quick Action Buttons
    Y = BodyY + 138;
    AddCtrl (S, InnerX, Y, InnerW, 48, AURORA_CTRL_BUTTON, CID_BTN_BOOT_OS, 0);
    Y += 62;
    AddCtrl (S, InnerX, Y, InnerW, 44, AURORA_CTRL_BUTTON, CID_BTN_UEFI_SHELL, 0);
    Y += 58;
    AddCtrl (S, InnerX, Y, InnerW, 44, AURORA_CTRL_BUTTON, CID_BTN_SCAN_USB, 0);
    Y += 58;
    AddCtrl (S, InnerX, Y, InnerW, 44, AURORA_CTRL_BUTTON, CID_BTN_SAVE_REBOOT, 0);
  } else if (S->ActiveTab == AURORA_TAB_BOOT) {
    INT32   LeftW  = ((S->Width - M * 2 - 20) * 58) / 100;
    INT32   RightX = M + LeftW + 20;
    INT32   RightW = S->Width - M - RightX;
    UINT32  Visible = S->BootEntryCount;
    INT32   RowY;
    INT32   BtnW;
    INT32   Sy;

    if (Visible > 8) {
      Visible = 8;
    }
    RowY = BodyY + 54;
    for (I = 0; I < (INT32)Visible; I++) {
      AddCtrl (S, M + 16, RowY + I * 44, LeftW - 32, 38, AURORA_CTRL_BOOT_ROW, (UINT32)(CID_BOOT_ROW_BASE + I), (UINT32)I);
    }

    // Boot list action buttons
    BtnW = (LeftW - 32 - 20) / 3;
    RowY = BodyY + BodyH - 64;
    AddCtrl (S, M + 16, RowY, BtnW, 42, AURORA_CTRL_BUTTON, CID_BOOT_MOVE_UP, 0);
    AddCtrl (S, M + 16 + BtnW + 10, RowY, BtnW, 42, AURORA_CTRL_BUTTON, CID_BOOT_MOVE_DOWN, 0);
    AddCtrl (S, M + 16 + (BtnW + 10) * 2, RowY, BtnW, 42, AURORA_CTRL_BUTTON, CID_BOOT_LAUNCH_SEL, 0);

    // Boot configuration controls on right card
    Sy = BodyY + 60;
    AddCtrl (S, RightX + 18, Sy, RightW - 36, 56, AURORA_CTRL_SLIDER, CID_SLIDER_TIMEOUT, 0);
    Sy += 74;
    AddCtrl (S, RightX + 18, Sy, RightW - 36, 48, AURORA_CTRL_TOGGLE, CID_TOGGLE_FASTBOOT, 0);
    Sy += 64;
    AddCtrl (S, RightX + 18, Sy, RightW - 36, 48, AURORA_CTRL_TOGGLE, CID_TOGGLE_SPLASH, 0);
    Sy += 64;
    AddCtrl (S, RightX + 18, Sy, RightW - 36, 48, AURORA_CTRL_CHOICE, CID_CHOICE_DEF_TARGET, 0);
  } else if (S->ActiveTab == AURORA_TAB_HARDWARE) {
    INT32  HalfW = (S->Width - M * 2 - 20) / 2;
    INT32  RightX = M + HalfW + 20;
    INT32  Sy = BodyY + 60;

    // Left card: GPU & Display
    AddCtrl (S, M + 18, Sy, HalfW - 36, 52, AURORA_CTRL_TOGGLE, CID_TOGGLE_GPU_POOL, 0);
    Sy += 70;
    AddCtrl (S, M + 18, Sy, HalfW - 36, 52, AURORA_CTRL_CHOICE, CID_CHOICE_CON_SCALE, 0);
    Sy += 70;
    AddCtrl (S, M + 18, Sy, HalfW - 36, 56, AURORA_CTRL_SLIDER, CID_SLIDER_MOUSE_SPEED, 0);

    // Right card: I/O & Firmware Debug
    Sy = BodyY + 60;
    AddCtrl (S, RightX + 18, Sy, HalfW - 36, 52, AURORA_CTRL_CHOICE, CID_CHOICE_SERIAL_DBG, 0);
    Sy += 70;
    AddCtrl (S, RightX + 18, Sy, HalfW - 36, 52, AURORA_CTRL_TOGGLE, CID_TOGGLE_USB_AUTO, 0);
    Sy += 70;
    AddCtrl (S, RightX + 18, Sy, HalfW - 36, 52, AURORA_CTRL_TOGGLE, CID_TOGGLE_USB_BOOT, 0);
  } else if (S->ActiveTab == AURORA_TAB_DIAG) {
    INT32  HalfW  = (S->Width - M * 2 - 20) / 2;
    INT32  RightX = M + HalfW + 20;
    INT32  BtnY   = BodyY + BodyH - 68;
    INT32  BtnW   = (HalfW - 48) / 2;

    AddCtrl (S, RightX + 18, BtnY, BtnW, 44, AURORA_CTRL_BUTTON, CID_DIAG_SCAN_USB, 0);
    AddCtrl (S, RightX + 18 + BtnW + 12, BtnY, BtnW, 44, AURORA_CTRL_BUTTON, CID_DIAG_RESET_INPUT, 0);
  } else if (S->ActiveTab == AURORA_TAB_EXIT) {
    INT32  LeftW  = ((S->Width - M * 2 - 20) * 42) / 100;
    INT32  RightX = M + LeftW + 20;
    INT32  RightW = S->Width - M - RightX;
    INT32  CardW  = (RightW - 48) / 2;
    INT32  CardH  = 86;
    INT32  Y0     = BodyY + 60;

    AddCtrl (S, RightX + 18, Y0, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_SAVE_BOOT, 0);
    AddCtrl (S, RightX + 18 + CardW + 12, Y0, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_SAVE_REBOOT, 0);
    AddCtrl (S, RightX + 18, Y0 + CardH + 18, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_DISCARD_BOOT, 0);
    AddCtrl (S, RightX + 18 + CardW + 12, Y0 + CardH + 18, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_LOAD_DEFAULTS, 0);
    AddCtrl (S, RightX + 18, Y0 + (CardH + 18) * 2, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_SHELL, 0);
    AddCtrl (S, RightX + 18 + CardW + 12, Y0 + (CardH + 18) * 2, CardW, CardH, AURORA_CTRL_BUTTON, CID_EXIT_POWER_OFF, 0);
  }

  if (S->FocusedCtrlIdx >= S->ControlCount) {
    S->FocusedCtrlIdx = (S->ControlCount > AURORA_TAB_COUNT) ? AURORA_TAB_COUNT : 0;
  }
  if (S->HoveredCtrlIdx >= S->ControlCount) {
    S->HoveredCtrlIdx = -1;
  }
}

VOID
AuroraBiosStateInit (
  AURORA_BIOS_STATE  *State,
  INT32              GopWidth,
  INT32              GopHeight
  )
{
  ZeroBytes (State, sizeof (*State));

  if (GopWidth >= 2000 && GopHeight >= 1200) {
    State->UiScale = 2;
  } else {
    State->UiScale = 1;
  }
  State->Width  = GopWidth / State->UiScale;
  State->Height = GopHeight / State->UiScale;
  if (State->Width < 640)  State->Width  = 640;
  if (State->Height < 480) State->Height = 480;

  AuroraBiosConfigSetDefaults (&State->Config);
  CopyBytes (&State->SavedConfig, &State->Config, sizeof (AURORA_BIOS_CONFIG));

  StrCopyA (State->Info.PlatformName, sizeof (State->Info.PlatformName), "MacBook Air (J813)");
  StrCopyA (State->Info.SocName, sizeof (State->Info.SocName), "Apple M5 (T8142) ARMv9");
  StrCopyA (State->Info.FirmwareVersion, sizeof (State->Info.FirmwareVersion), "Aurora Mu UEFI 2026.11");
  State->Info.GopWidth  = (UINT32)GopWidth;
  State->Info.GopHeight = (UINT32)GopHeight;
  State->Info.GopBpp    = 30;
  State->Info.CpuCores  = 10;

  State->ActiveTab       = AURORA_TAB_DASHBOARD;
  State->FocusedCtrlIdx  = AURORA_TAB_COUNT; // First content control
  State->HoveredCtrlIdx  = -1;
  State->MouseX          = State->Width / 2;
  State->MouseY          = State->Height / 2;
  State->PrevMouseX      = State->MouseX;
  State->PrevMouseY      = State->MouseY;
  State->MouseVisible    = TRUE;
  State->LastClickBootIdx = 0xFFFFFFFFu;

  AuroraBiosSetStatus (
    State,
    "READY  //  Mouse + Keyboard Active  |  F1-F5 Tabs  |  F10 Save & Boot  |  ESC Exit",
    COL_ACCENT
    );
  AuroraBiosBuildLayout (State);
  MarkFullRedraw (State);
}

// ---------------------------------------------------------------------------
// Control Activation Logic (shared by Mouse Click and Keyboard Enter/Space)
// ---------------------------------------------------------------------------
static VOID
SetTab (
  AURORA_BIOS_STATE  *S,
  AURORA_BIOS_TAB    Tab
  )
{
  if (Tab >= AURORA_TAB_COUNT) {
    return;
  }
  S->ActiveTab        = Tab;
  S->DraggingSliderId = 0;
  AuroraBiosBuildLayout (S);
  S->FocusedCtrlIdx = (S->ControlCount > AURORA_TAB_COUNT) ? AURORA_TAB_COUNT : (INT32)Tab;
  MarkFullRedraw (S);
}

static VOID
MoveBootOption (
  AURORA_BIOS_STATE  *S,
  INT32              Delta
  )
{
  UINT32             Cur = S->SelectedBootIdx;
  INT32              Dst = (INT32)Cur + Delta;
  AURORA_BOOT_ENTRY  Tmp;

  if (S->BootEntryCount < 2 || Cur >= S->BootEntryCount) {
    return;
  }
  if (Dst < 0 || Dst >= (INT32)S->BootEntryCount) {
    return;
  }
  CopyBytes (&Tmp, &S->BootEntries[Cur], sizeof (Tmp));
  CopyBytes (&S->BootEntries[Cur], &S->BootEntries[Dst], sizeof (Tmp));
  CopyBytes (&S->BootEntries[Dst], &Tmp, sizeof (Tmp));
  S->SelectedBootIdx = (UINT32)Dst;
  S->BootOrderDirty  = TRUE;
  S->PendingAction   = AURORA_ACTION_REORDER_BOOT;
  AuroraBiosSetStatus (S, "Boot priority updated (press F10 or Save & Exit to commit)", COL_AMBER);
}

static VOID
ApplySliderByX (
  AURORA_BIOS_STATE     *S,
  const AURORA_UI_RECT  *R,
  INT32                 MouseX
  )
{
  INT32  TrackX = R->X + 16;
  INT32  TrackW = R->W - 110;
  INT32  Rel;

  if (TrackW < 40) {
    TrackW = 40;
  }
  Rel = ClampI32 (MouseX - TrackX, 0, TrackW);

  if (R->Id == CID_SLIDER_TIMEOUT) {
    UINT32  Val = (UINT32)((Rel * 10 + TrackW / 2) / TrackW);
    if (Val != S->Config.BootTimeoutSec) {
      S->Config.BootTimeoutSec = Val;
      AuroraBiosSetStatus (S, (Val == 0) ? "Boot menu prompt set to 0s (instant boot)" : "Boot menu prompt timeout updated", COL_ACCENT);
    }
  } else if (R->Id == CID_SLIDER_MOUSE_SPEED) {
    UINT32  Val = (UINT32)(1 + (Rel * 9 + TrackW / 2) / TrackW);
    if (Val != S->Config.MouseSpeed) {
      S->Config.MouseSpeed = (UINT8)Val;
      AuroraBiosSetStatus (S, "Mouse cursor sensitivity updated live", COL_ACCENT);
    }
  }
}

static VOID
AdjustControlDelta (
  AURORA_BIOS_STATE     *S,
  const AURORA_UI_RECT  *R,
  INT32                 Dir
  )
{
  if (R == NULL) {
    return;
  }
  if (R->Id == CID_SLIDER_TIMEOUT) {
    INT32  V = ClampI32 ((INT32)S->Config.BootTimeoutSec + Dir, 0, 10);
    S->Config.BootTimeoutSec = (UINT32)V;
    AuroraBiosSetStatus (S, "Boot menu prompt timeout updated", COL_ACCENT);
  } else if (R->Id == CID_SLIDER_MOUSE_SPEED) {
    INT32  V = ClampI32 ((INT32)S->Config.MouseSpeed + Dir, 1, 10);
    S->Config.MouseSpeed = (UINT8)V;
    AuroraBiosSetStatus (S, "Mouse cursor sensitivity updated live", COL_ACCENT);
  } else if (R->Id == CID_CHOICE_DEF_TARGET) {
    S->Config.DefaultBootTarget = (UINT8)((S->Config.DefaultBootTarget + (Dir > 0 ? 1 : 2)) % 3);
    AuroraBiosSetStatus (S, "Default boot target updated", COL_ACCENT);
  } else if (R->Id == CID_CHOICE_CON_SCALE) {
    S->Config.ConsoleScaleMode = (S->Config.ConsoleScaleMode == 1) ? 2 : 1;
    AuroraBiosSetStatus (S, "Console scale mode updated (takes effect on next boot)", COL_ACCENT);
  } else if (R->Id == CID_CHOICE_SERIAL_DBG) {
    S->Config.SerialDebugLevel = (UINT8)((S->Config.SerialDebugLevel + (Dir > 0 ? 1 : 2)) % 3);
    AuroraBiosSetStatus (S, "Serial debug verbosity updated", COL_ACCENT);
  } else if (R->Kind == AURORA_CTRL_TOGGLE) {
    switch (R->Id) {
      case CID_TOGGLE_FASTBOOT:
        S->Config.FastBoot ^= 1;
        AuroraBiosSetStatus (S, S->Config.FastBoot ? "Fast Boot ENABLED" : "Fast Boot DISABLED", COL_ACCENT);
        break;
      case CID_TOGGLE_SPLASH:
        S->Config.BootSplashLogo ^= 1;
        AuroraBiosSetStatus (S, S->Config.BootSplashLogo ? "Boot Splash Logo ENABLED" : "Boot Splash Logo DISABLED", COL_ACCENT);
        break;
      case CID_TOGGLE_GPU_POOL:
        S->Config.GpuLocalPoolEnable ^= 1;
        AuroraBiosSetStatus (S, S->Config.GpuLocalPoolEnable ? "M5 GPU Local Pool ENABLED" : "M5 GPU Local Pool DISABLED", COL_AMBER);
        break;
      case CID_TOGGLE_USB_AUTO:
        S->Config.UsbHidAutoConnect ^= 1;
        AuroraBiosSetStatus (S, S->Config.UsbHidAutoConnect ? "USB HID Auto-Connect ENABLED" : "USB HID set to On-Demand Safe", COL_ACCENT);
        break;
      case CID_TOGGLE_USB_BOOT:
        S->Config.ExternalUsbBoot ^= 1;
        AuroraBiosSetStatus (S, S->Config.ExternalUsbBoot ? "External USB Boot ENABLED" : "External USB Boot DISABLED", COL_ACCENT);
        break;
      default:
        break;
    }
  }
}

static VOID
ActivateControl (
  AURORA_BIOS_STATE  *S,
  INT32              CtrlIdx,
  BOOLEAN            FromMouse,
  INT32              ClickX
  )
{
  const AURORA_UI_RECT  *R;

  if (CtrlIdx < 0 || CtrlIdx >= S->ControlCount) {
    return;
  }
  R = &S->Controls[CtrlIdx];

  if (R->Kind == AURORA_CTRL_TAB) {
    SetTab (S, (AURORA_BIOS_TAB)R->Param);
    return;
  }

  if (R->Kind == AURORA_CTRL_SLIDER) {
    if (FromMouse) {
      S->DraggingSliderId = R->Id;
      ApplySliderByX (S, R, ClickX);
    } else {
      AdjustControlDelta (S, R, 1);
    }
    return;
  }

  if (R->Kind == AURORA_CTRL_TOGGLE) {
    AdjustControlDelta (S, R, 1);
    return;
  }

  if (R->Kind == AURORA_CTRL_BOOT_ROW) {
    UINT32  Idx = R->Param;
    if (FromMouse &&
        S->LastClickBootIdx == Idx &&
        (S->TickCount - S->LastClickTick) <= 35u) {
      // Double-click launches the selected OS immediately
      S->SelectedBootIdx  = Idx;
      S->PendingAction    = AURORA_ACTION_BOOT_OPTION;
      S->PendingActionArg = Idx;
      AuroraBiosSetStatus (S, "Launching selected boot option...", COL_GREEN);
    } else {
      S->SelectedBootIdx  = Idx;
      S->LastClickBootIdx = Idx;
      S->LastClickTick    = S->TickCount;
      if (!FromMouse) {
        S->PendingAction    = AURORA_ACTION_BOOT_OPTION;
        S->PendingActionArg = Idx;
        AuroraBiosSetStatus (S, "Launching selected boot option...", COL_GREEN);
      } else {
        AuroraBiosSetStatus (S, "Boot entry selected (double-click or click BOOT NOW to launch)", COL_ACCENT);
      }
    }
    return;
  }

  switch (R->Id) {
    case CID_PROFILE_BALANCED:
      S->Config.PerformanceProfile = AURORA_PROFILE_BALANCED;
      AuroraBiosSetStatus (S, "Profile set to BALANCED (Apple M5 optimal power/perf)", COL_ACCENT);
      break;
    case CID_PROFILE_PERFORMANCE:
      S->Config.PerformanceProfile = AURORA_PROFILE_PERFORMANCE;
      AuroraBiosSetStatus (S, "Profile set to PERFORMANCE (maximum M5 P-core + G17G responsiveness)", COL_GREEN);
      break;
    case CID_PROFILE_QUIET:
      S->Config.PerformanceProfile = AURORA_PROFILE_QUIET;
      AuroraBiosSetStatus (S, "Profile set to QUIET (cool thermal envelope)", COL_AMBER);
      break;
    case CID_BTN_BOOT_OS:
    case CID_EXIT_SAVE_BOOT:
      AuroraBiosConfigFinalize (&S->Config);
      S->PendingAction = AURORA_ACTION_SAVE_AND_BOOT;
      AuroraBiosSetStatus (S, "Saving configuration and booting operating system...", COL_GREEN);
      break;
    case CID_BTN_UEFI_SHELL:
    case CID_EXIT_SHELL:
      S->PendingAction = AURORA_ACTION_LAUNCH_SHELL;
      AuroraBiosSetStatus (S, "Launching internal UEFI Shell...", COL_ACCENT);
      break;
    case CID_BTN_SCAN_USB:
    case CID_DIAG_SCAN_USB:
      S->PendingAction = AURORA_ACTION_SCAN_USB_HID;
      AuroraBiosSetStatus (S, "Scanning USB / Type-C HID controllers for mouse & keyboard...", COL_AMBER);
      break;
    case CID_BTN_SAVE_REBOOT:
    case CID_EXIT_SAVE_REBOOT:
      AuroraBiosConfigFinalize (&S->Config);
      S->PendingAction = AURORA_ACTION_SAVE_AND_REBOOT;
      AuroraBiosSetStatus (S, "Saving settings and rebooting system...", COL_AMBER);
      break;
    case CID_BOOT_MOVE_UP:
      MoveBootOption (S, -1);
      break;
    case CID_BOOT_MOVE_DOWN:
      MoveBootOption (S, 1);
      break;
    case CID_BOOT_LAUNCH_SEL:
      if (S->BootEntryCount > 0 && S->SelectedBootIdx < S->BootEntryCount) {
        S->PendingAction    = AURORA_ACTION_BOOT_OPTION;
        S->PendingActionArg = S->SelectedBootIdx;
        AuroraBiosSetStatus (S, "Launching selected boot option...", COL_GREEN);
      }
      break;
    case CID_CHOICE_DEF_TARGET:
    case CID_CHOICE_CON_SCALE:
    case CID_CHOICE_SERIAL_DBG:
      AdjustControlDelta (S, R, 1);
      break;
    case CID_DIAG_RESET_INPUT:
      S->Info.MouseClicks = 0;
      S->Info.KeyPresses  = 0;
      S->Info.LastRawDx   = 0;
      S->Info.LastRawDy   = 0;
      S->Info.LastKeyCode = 0;
      AuroraBiosSetStatus (S, "Input telemetry counters reset", COL_ACCENT);
      break;
    case CID_EXIT_DISCARD_BOOT:
      CopyBytes (&S->Config, &S->SavedConfig, sizeof (AURORA_BIOS_CONFIG));
      S->BootOrderDirty = FALSE;
      S->PendingAction  = AURORA_ACTION_DISCARD_AND_BOOT;
      AuroraBiosSetStatus (S, "Discarded unsaved changes; continuing boot...", COL_AMBER);
      break;
    case CID_EXIT_LOAD_DEFAULTS:
      AuroraBiosConfigSetDefaults (&S->Config);
      AuroraBiosSetStatus (S, "Loaded optimized Apple M5 J813 defaults (Save & Exit to commit)", COL_AMBER);
      break;
    case CID_EXIT_POWER_OFF:
      S->PendingAction = AURORA_ACTION_POWER_OFF;
      AuroraBiosSetStatus (S, "Powering off system...", COL_RED);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Input Event Handlers (Keyboard + Absolute Mouse + Relative Mouse)
// ---------------------------------------------------------------------------
static INT32
HitTest (
  const AURORA_BIOS_STATE  *S,
  INT32                    X,
  INT32                    Y
  )
{
  INT32  I;
  for (I = S->ControlCount - 1; I >= 0; I--) {
    const AURORA_UI_RECT  *R = &S->Controls[I];
    if (X >= R->X && X < R->X + R->W && Y >= R->Y && Y < R->Y + R->H) {
      return I;
    }
  }
  return -1;
}

VOID
AuroraBiosHandleKey (
  AURORA_BIOS_STATE  *S,
  AURORA_BIOS_KEY    Key
  )
{
  if (Key == AURORA_KEY_NONE) {
    return;
  }
  S->Info.KeyPresses++;
  S->Info.LastKeyCode = (UINT32)Key;

  switch (Key) {
    case AURORA_KEY_F1: SetTab (S, AURORA_TAB_DASHBOARD); return;
    case AURORA_KEY_F2: SetTab (S, AURORA_TAB_BOOT);      return;
    case AURORA_KEY_F3: SetTab (S, AURORA_TAB_HARDWARE);  return;
    case AURORA_KEY_F4: SetTab (S, AURORA_TAB_DIAG);      return;
    case AURORA_KEY_F5: SetTab (S, AURORA_TAB_EXIT);      return;
    case AURORA_KEY_F9:
      AuroraBiosConfigSetDefaults (&S->Config);
      AuroraBiosSetStatus (S, "Loaded optimized Apple M5 J813 defaults", COL_AMBER);
      return;
    case AURORA_KEY_F10:
      AuroraBiosConfigFinalize (&S->Config);
      S->PendingAction = AURORA_ACTION_SAVE_AND_BOOT;
      AuroraBiosSetStatus (S, "F10: Saving configuration and booting OS...", COL_GREEN);
      return;
    case AURORA_KEY_ESC:
      if (S->ActiveTab != AURORA_TAB_EXIT && AuroraBiosCountModified (S) > 0) {
        SetTab (S, AURORA_TAB_EXIT);
        AuroraBiosSetStatus (S, "Unsaved changes pending - Save or Discard below", COL_AMBER);
      } else {
        S->PendingAction = AURORA_ACTION_DISCARD_AND_BOOT;
      }
      return;
    case AURORA_KEY_TAB:
      if (S->ControlCount > 0) {
        S->FocusedCtrlIdx = (S->FocusedCtrlIdx + 1) % S->ControlCount;
        MarkFullRedraw (S);
      }
      return;
    case AURORA_KEY_SHIFT_TAB:
      if (S->ControlCount > 0) {
        S->FocusedCtrlIdx = (S->FocusedCtrlIdx + S->ControlCount - 1) % S->ControlCount;
        MarkFullRedraw (S);
      }
      return;
    case AURORA_KEY_UP:
      if (S->ControlCount > 0) {
        S->FocusedCtrlIdx = (S->FocusedCtrlIdx + S->ControlCount - 1) % S->ControlCount;
        if (S->Controls[S->FocusedCtrlIdx].Kind == AURORA_CTRL_BOOT_ROW) {
          S->SelectedBootIdx = S->Controls[S->FocusedCtrlIdx].Param;
        }
        MarkFullRedraw (S);
      }
      return;
    case AURORA_KEY_DOWN:
      if (S->ControlCount > 0) {
        S->FocusedCtrlIdx = (S->FocusedCtrlIdx + 1) % S->ControlCount;
        if (S->Controls[S->FocusedCtrlIdx].Kind == AURORA_CTRL_BOOT_ROW) {
          S->SelectedBootIdx = S->Controls[S->FocusedCtrlIdx].Param;
        }
        MarkFullRedraw (S);
      }
      return;
    case AURORA_KEY_LEFT:
      if (S->FocusedCtrlIdx >= 0 && S->FocusedCtrlIdx < S->ControlCount) {
        const AURORA_UI_RECT  *R = &S->Controls[S->FocusedCtrlIdx];
        if (R->Kind == AURORA_CTRL_SLIDER || R->Kind == AURORA_CTRL_CHOICE || R->Kind == AURORA_CTRL_TOGGLE) {
          AdjustControlDelta (S, R, -1);
          return;
        }
      }
      SetTab (S, (AURORA_BIOS_TAB)((S->ActiveTab + AURORA_TAB_COUNT - 1) % AURORA_TAB_COUNT));
      return;
    case AURORA_KEY_RIGHT:
      if (S->FocusedCtrlIdx >= 0 && S->FocusedCtrlIdx < S->ControlCount) {
        const AURORA_UI_RECT  *R = &S->Controls[S->FocusedCtrlIdx];
        if (R->Kind == AURORA_CTRL_SLIDER || R->Kind == AURORA_CTRL_CHOICE || R->Kind == AURORA_CTRL_TOGGLE) {
          AdjustControlDelta (S, R, 1);
          return;
        }
      }
      SetTab (S, (AURORA_BIOS_TAB)((S->ActiveTab + 1) % AURORA_TAB_COUNT));
      return;
    case AURORA_KEY_PLUS:
      if (S->ActiveTab == AURORA_TAB_BOOT) {
        MoveBootOption (S, -1);
      } else if (S->FocusedCtrlIdx >= 0 && S->FocusedCtrlIdx < S->ControlCount) {
        AdjustControlDelta (S, &S->Controls[S->FocusedCtrlIdx], 1);
      }
      return;
    case AURORA_KEY_MINUS:
      if (S->ActiveTab == AURORA_TAB_BOOT) {
        MoveBootOption (S, 1);
      } else if (S->FocusedCtrlIdx >= 0 && S->FocusedCtrlIdx < S->ControlCount) {
        AdjustControlDelta (S, &S->Controls[S->FocusedCtrlIdx], -1);
      }
      return;
    case AURORA_KEY_ENTER:
    case AURORA_KEY_SPACE:
      ActivateControl (S, S->FocusedCtrlIdx, FALSE, 0);
      return;
    default:
      break;
  }
}

VOID
AuroraBiosHandleMouseMove (
  AURORA_BIOS_STATE  *S,
  INT32              NewX,
  INT32              NewY,
  BOOLEAN            LeftDown
  )
{
  INT32    ClampedX = ClampI32 (NewX, 0, S->Width - 1);
  INT32    ClampedY = ClampI32 (NewY, 0, S->Height - 1);
  INT32    NewHover;
  BOOLEAN  JustPressed  = LeftDown && !S->MouseLeftDown;
  BOOLEAN  JustReleased = !LeftDown && S->MouseLeftDown;

  S->MouseVisible = TRUE;

  if (ClampedX != S->MouseX || ClampedY != S->MouseY) {
    MarkDirty (S, S->MouseX - 2, S->MouseY - 2, 24, 28);
    S->PrevMouseX = S->MouseX;
    S->PrevMouseY = S->MouseY;
    S->MouseX     = ClampedX;
    S->MouseY     = ClampedY;
    MarkDirty (S, S->MouseX - 2, S->MouseY - 2, 24, 28);
  }

  NewHover = HitTest (S, ClampedX, ClampedY);
  if (NewHover != S->HoveredCtrlIdx) {
    if (S->HoveredCtrlIdx >= 0 && S->HoveredCtrlIdx < S->ControlCount) {
      const AURORA_UI_RECT  *OldR = &S->Controls[S->HoveredCtrlIdx];
      MarkDirty (S, OldR->X - 2, OldR->Y - 2, OldR->W + 4, OldR->H + 4);
    }
    S->HoveredCtrlIdx = NewHover;
    if (NewHover >= 0 && NewHover < S->ControlCount) {
      const AURORA_UI_RECT  *CurR = &S->Controls[NewHover];
      MarkDirty (S, CurR->X - 2, CurR->Y - 2, CurR->W + 4, CurR->H + 4);
    }
    S->FullRedrawNeeded = TRUE;
  }

  if (JustPressed) {
    S->Info.MouseClicks++;
    if (NewHover >= 0) {
      S->FocusedCtrlIdx = NewHover;
      ActivateControl (S, NewHover, TRUE, ClampedX);
    }
  } else if (LeftDown && S->DraggingSliderId != 0) {
    INT32  I;
    for (I = 0; I < S->ControlCount; I++) {
      if (S->Controls[I].Id == S->DraggingSliderId) {
        ApplySliderByX (S, &S->Controls[I], ClampedX);
        break;
      }
    }
  }

  if (JustReleased) {
    S->DraggingSliderId = 0;
  }

  S->MousePrevLeftDown = S->MouseLeftDown;
  S->MouseLeftDown     = LeftDown;
}

VOID
AuroraBiosHandleMouseDelta (
  AURORA_BIOS_STATE  *S,
  INT32              DeltaX,
  INT32              DeltaY,
  BOOLEAN            LeftDown
  )
{
  INT32  Speed = (INT32)S->Config.MouseSpeed;
  INT32  ScaledDx;
  INT32  ScaledDy;

  S->Info.LastRawDx = DeltaX;
  S->Info.LastRawDy = DeltaY;

  if (Speed < 1) Speed = 1;
  ScaledDx = (DeltaX * Speed + (DeltaX >= 0 ? 2 : -2)) / 4;
  ScaledDy = (DeltaY * Speed + (DeltaY >= 0 ? 2 : -2)) / 4;
  if (DeltaX != 0 && ScaledDx == 0) ScaledDx = (DeltaX > 0) ? 1 : -1;
  if (DeltaY != 0 && ScaledDy == 0) ScaledDy = (DeltaY > 0) ? 1 : -1;

  AuroraBiosHandleMouseMove (S, S->MouseX + ScaledDx, S->MouseY + ScaledDy, LeftDown);
}

// ---------------------------------------------------------------------------
// 2D Software Rasterizer Primitives (0x00RRGGBB in RAM BackBuffer)
// ---------------------------------------------------------------------------
static VOID
FillRect (
  UINT32  *Buf,
  INT32   Bw,
  INT32   Bh,
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color
  )
{
  INT32  X1 = ClampI32 (X, 0, Bw);
  INT32  Y1 = ClampI32 (Y, 0, Bh);
  INT32  X2 = ClampI32 (X + W, 0, Bw);
  INT32  Y2 = ClampI32 (Y + H, 0, Bh);
  INT32  Py;
  INT32  Px;

  for (Py = Y1; Py < Y2; Py++) {
    UINT32  *Row = Buf + (UINTN)Py * (UINTN)Bw;
    for (Px = X1; Px < X2; Px++) {
      Row[Px] = Color;
    }
  }
}

static VOID
DrawRectOutline (
  UINT32  *Buf,
  INT32   Bw,
  INT32   Bh,
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  INT32   Thick,
  UINT32  Color
  )
{
  FillRect (Buf, Bw, Bh, X, Y, W, Thick, Color);
  FillRect (Buf, Bw, Bh, X, Y + H - Thick, W, Thick, Color);
  FillRect (Buf, Bw, Bh, X, Y, Thick, H, Color);
  FillRect (Buf, Bw, Bh, X + W - Thick, Y, Thick, H, Color);
}

static VOID
DrawChamferedCard (
  UINT32  *Buf,
  INT32   Bw,
  INT32   Bh,
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  FillCol,
  UINT32  BorderCol,
  UINT32  AccentCol
  )
{
  FillRect (Buf, Bw, Bh, X, Y, W, H, FillCol);
  DrawRectOutline (Buf, Bw, Bh, X, Y, W, H, 1, BorderCol);
  // Gaming top accent bar + sharp corner ticks
  if (AccentCol != 0) {
    FillRect (Buf, Bw, Bh, X, Y, W, 3, AccentCol);
    FillRect (Buf, Bw, Bh, X, Y, 10, 3, COL_TEXT_BRIGHT);
    FillRect (Buf, Bw, Bh, X + W - 10, Y + H - 2, 10, 2, AccentCol);
  }
}

static VOID
DrawChar (
  UINT32  *Buf,
  INT32   Bw,
  INT32   Bh,
  INT32   X,
  INT32   Y,
  char    Ch,
  UINT32  Color,
  INT32   Scale,
  BOOLEAN Bold
  )
{
  INT32         Idx;
  INT32         Row;
  INT32         Col;
  const UINT8   *Glyph;

  if (Ch < 0x20 || Ch > 0x7E) {
    Ch = '?';
  }
  Idx   = (INT32)(Ch - 0x20);
  Glyph = mAuroraFont8x8[Idx];

  for (Row = 0; Row < 8; Row++) {
    UINT8  Bits = Glyph[Row];
    if (Bits == 0) {
      continue;
    }
    for (Col = 0; Col < 8; Col++) {
      if ((Bits & (0x80u >> Col)) != 0) {
        FillRect (
          Buf,
          Bw,
          Bh,
          X + Col * Scale,
          Y + Row * 2 * Scale,
          Bold ? (Scale + 1) : Scale,
          2 * Scale,
          Color
          );
      }
    }
  }
}

static VOID
DrawText (
  UINT32      *Buf,
  INT32       Bw,
  INT32       Bh,
  INT32       X,
  INT32       Y,
  const char  *Str,
  UINT32      Color,
  INT32       Scale,
  BOOLEAN     Bold
  )
{
  INT32  Step = 8 * Scale + (Bold ? 1 : 0);
  if (Str == NULL) {
    return;
  }
  while (*Str != '\0') {
    if (X + Step > 0 && X < Bw) {
      DrawChar (Buf, Bw, Bh, X, Y, *Str, Color, Scale, Bold);
    }
    X += Step;
    Str++;
  }
}

static VOID
DrawTextClipped (
  UINT32      *Buf,
  INT32       Bw,
  INT32       Bh,
  INT32       X,
  INT32       Y,
  INT32       MaxW,
  const char  *Str,
  UINT32      Color,
  INT32       Scale,
  BOOLEAN     Bold
  )
{
  INT32  Step = 8 * Scale + (Bold ? 1 : 0);
  INT32  MaxX = X + MaxW;
  if (Str == NULL) {
    return;
  }
  while (*Str != '\0' && (X + Step) <= MaxX) {
    DrawChar (Buf, Bw, Bh, X, Y, *Str, Color, Scale, Bold);
    X += Step;
    Str++;
  }
}

static VOID
DrawGaugeBar (
  UINT32  *Buf,
  INT32   Bw,
  INT32   Bh,
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Percent,
  UINT32  FillCol
  )
{
  INT32  Segs = 20;
  INT32  Gap  = 3;
  INT32  SegW = (W - (Segs - 1) * Gap) / Segs;
  INT32  Lit  = (INT32)((Percent * (UINT32)Segs + 50u) / 100u);
  INT32  I;

  if (SegW < 4) SegW = 4;
  for (I = 0; I < Segs; I++) {
    UINT32  C = (I < Lit) ? FillCol : COL_CARD_ALT;
    FillRect (Buf, Bw, Bh, X + I * (SegW + Gap), Y, SegW, H, C);
  }
}

static VOID
DrawButton (
  UINT32                *Buf,
  INT32                 Bw,
  INT32                 Bh,
  const AURORA_UI_RECT  *R,
  const char            *Label,
  const char            *SubLabel,
  BOOLEAN               Active,
  BOOLEAN               Hovered,
  BOOLEAN               Focused,
  UINT32                AccentCol
  )
{
  UINT32  Bg     = Active ? COL_ACCENT_DARK : (Hovered ? COL_CARD_HOVER : COL_CARD_ALT);
  UINT32  Border = (Focused || Hovered || Active) ? AccentCol : COL_BORDER;
  UINT32  TxtCol = (Active || Hovered || Focused) ? COL_TEXT_BRIGHT : COL_TEXT;

  FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
  DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, (Focused || Active) ? 2 : 1, Border);
  // Left glowing gaming stripe
  FillRect (Buf, Bw, Bh, R->X, R->Y, 4, R->H, (Active || Hovered || Focused) ? AccentCol : COL_BORDER);

  if (SubLabel != NULL && SubLabel[0] != '\0') {
    DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + 14, R->W - 20, Label, TxtCol, 1, TRUE);
    DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + 42, R->W - 20, SubLabel, COL_TEXT_MUTED, 1, FALSE);
  } else {
    INT32  TextY = R->Y + (R->H - 16) / 2;
    DrawTextClipped (Buf, Bw, Bh, R->X + 14, TextY, R->W - 20, Label, TxtCol, 1, TRUE);
  }
}

static VOID
DrawToggleRow (
  UINT32                *Buf,
  INT32                 Bw,
  INT32                 Bh,
  const AURORA_UI_RECT  *R,
  const char            *Label,
  BOOLEAN               Enabled,
  BOOLEAN               Hovered,
  BOOLEAN               Focused
  )
{
  UINT32  Bg     = Hovered ? COL_CARD_HOVER : COL_CARD_ALT;
  UINT32  Border = (Focused || Hovered) ? COL_BORDER_HI : COL_BORDER;
  INT32   PillW  = 78;
  INT32   PillH  = 26;
  INT32   PillX  = R->X + R->W - PillW - 14;
  INT32   PillY  = R->Y + (R->H - PillH) / 2;

  FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
  DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Focused ? 2 : 1, Border);
  DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + (R->H - 16) / 2, R->W - PillW - 36, Label, COL_TEXT_BRIGHT, 1, FALSE);

  // Toggle switch pill
  FillRect (Buf, Bw, Bh, PillX, PillY, PillW, PillH, Enabled ? COL_GREEN_DARK : COL_BG);
  DrawRectOutline (Buf, Bw, Bh, PillX, PillY, PillW, PillH, 1, Enabled ? COL_GREEN : COL_TEXT_MUTED);
  if (Enabled) {
    FillRect (Buf, Bw, Bh, PillX + PillW - 24, PillY + 3, 21, PillH - 6, COL_GREEN);
    DrawText (Buf, Bw, Bh, PillX + 10, PillY + 5, "ON", COL_GREEN, 1, TRUE);
  } else {
    FillRect (Buf, Bw, Bh, PillX + 3, PillY + 3, 21, PillH - 6, COL_TEXT_MUTED);
    DrawText (Buf, Bw, Bh, PillX + 34, PillY + 5, "OFF", COL_TEXT_MUTED, 1, TRUE);
  }
}

static VOID
DrawChoiceRow (
  UINT32                *Buf,
  INT32                 Bw,
  INT32                 Bh,
  const AURORA_UI_RECT  *R,
  const char            *Label,
  const char            *ValueText,
  BOOLEAN               Hovered,
  BOOLEAN               Focused,
  UINT32                ValColor
  )
{
  UINT32  Bg     = Hovered ? COL_CARD_HOVER : COL_CARD_ALT;
  UINT32  Border = (Focused || Hovered) ? COL_BORDER_HI : COL_BORDER;
  INT32   BoxW   = 176;
  INT32   BoxH   = 28;
  INT32   BoxX   = R->X + R->W - BoxW - 14;
  INT32   BoxY   = R->Y + (R->H - BoxH) / 2;

  FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
  DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Focused ? 2 : 1, Border);
  DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + (R->H - 16) / 2, R->W - BoxW - 32, Label, COL_TEXT_BRIGHT, 1, FALSE);

  FillRect (Buf, Bw, Bh, BoxX, BoxY, BoxW, BoxH, COL_BG);
  DrawRectOutline (Buf, Bw, Bh, BoxX, BoxY, BoxW, BoxH, 1, ValColor);
  DrawTextClipped (Buf, Bw, Bh, BoxX + 10, BoxY + 6, BoxW - 16, ValueText, ValColor, 1, TRUE);
}

static VOID
DrawSliderRow (
  UINT32                *Buf,
  INT32                 Bw,
  INT32                 Bh,
  const AURORA_UI_RECT  *R,
  const char            *Label,
  const char            *ValueText,
  UINT32                Val,
  UINT32                MinVal,
  UINT32                MaxVal,
  BOOLEAN               Hovered,
  BOOLEAN               Focused
  )
{
  UINT32  Bg     = Hovered ? COL_CARD_HOVER : COL_CARD_ALT;
  UINT32  Border = (Focused || Hovered) ? COL_BORDER_HI : COL_BORDER;
  INT32   TrackX = R->X + 16;
  INT32   TrackY = R->Y + 34;
  INT32   TrackW = R->W - 110;
  INT32   FillW;

  if (TrackW < 40) TrackW = 40;
  FillW = (MaxVal > MinVal) ? (INT32)(((Val - MinVal) * (UINT32)TrackW) / (MaxVal - MinVal)) : 0;

  FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
  DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Focused ? 2 : 1, Border);
  DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + 8, R->W - 110, Label, COL_TEXT_BRIGHT, 1, FALSE);
  DrawTextClipped (Buf, Bw, Bh, R->X + R->W - 88, R->Y + 18, 80, ValueText, COL_ACCENT, 1, TRUE);

  // Track + glowing fill + thumb
  FillRect (Buf, Bw, Bh, TrackX, TrackY, TrackW, 8, COL_BG);
  DrawRectOutline (Buf, Bw, Bh, TrackX, TrackY, TrackW, 8, 1, COL_BORDER);
  if (FillW > 0) {
    FillRect (Buf, Bw, Bh, TrackX + 1, TrackY + 1, FillW, 6, COL_ACCENT);
  }
  FillRect (Buf, Bw, Bh, TrackX + FillW - 5, TrackY - 4, 10, 16, COL_TEXT_BRIGHT);
  DrawRectOutline (Buf, Bw, Bh, TrackX + FillW - 5, TrackY - 4, 10, 16, 1, COL_ACCENT);
}

static VOID
DrawKeyValue (
  UINT32      *Buf,
  INT32       Bw,
  INT32       Bh,
  INT32       X,
  INT32       Y,
  INT32       W,
  const char  *Key,
  const char  *Val,
  UINT32      ValCol
  )
{
  DrawTextClipped (Buf, Bw, Bh, X, Y, W / 2, Key, COL_TEXT_MUTED, 1, FALSE);
  DrawTextClipped (Buf, Bw, Bh, X + W / 2 - 12, Y, W / 2 + 12, Val, ValCol, 1, TRUE);
}

// ---------------------------------------------------------------------------
// Tab Renderers
// ---------------------------------------------------------------------------
static VOID
RenderDashboard (
  const AURORA_BIOS_STATE  *S,
  UINT32                   *Buf,
  INT32                    M,
  INT32                    BodyY,
  INT32                    BodyH
  )
{
  INT32  Bw     = S->Width;
  INT32  Bh     = S->Height;
  INT32  ColGap = 18;
  INT32  ColW   = (Bw - M * 2 - ColGap * 2) / 3;
  INT32  Col0X  = M;
  INT32  Col1X  = M + ColW + ColGap;
  INT32  Col2X  = M + (ColW + ColGap) * 2;
  char   Tmp[64];
  INT32  Y;
  INT32  I;

  // Column 0: Silicon & Platform Telemetry
  DrawChamferedCard (Buf, Bw, Bh, Col0X, BodyY, ColW, BodyH, COL_CARD_BG, COL_BORDER, COL_ACCENT);
  DrawText (Buf, Bw, Bh, Col0X + 16, BodyY + 14, "APPLE SILICON TELEMETRY", COL_ACCENT, 1, TRUE);
  FillRect (Buf, Bw, Bh, Col0X + 16, BodyY + 36, ColW - 32, 1, COL_BORDER);

  Y = BodyY + 52;
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "PLATFORM", S->Info.PlatformName, COL_TEXT_BRIGHT); Y += 32;
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "PROCESSOR", S->Info.SocName, COL_ACCENT); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.CpuCores, 1); StrCatA (Tmp, sizeof (Tmp), " Cores (ARM64)");
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "CPU CORES", Tmp, COL_TEXT_BRIGHT); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.TotalRamMiB / 1024u, 1); StrCatA (Tmp, sizeof (Tmp), " GB Unified LPDDR5X");
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "MEMORY", Tmp, COL_GREEN); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.GopWidth, 1); StrCatA (Tmp, sizeof (Tmp), "x");
  AppendU64 (Tmp, sizeof (Tmp), S->Info.GopHeight, 1); StrCatA (Tmp, sizeof (Tmp), " @ ");
  AppendU64 (Tmp, sizeof (Tmp), S->Info.GopBpp, 1); StrCatA (Tmp, sizeof (Tmp), "bpc");
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "DISPLAY", Tmp, COL_TEXT_BRIGHT); Y += 32;

  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "GPU POOL", S->Info.GpuPoolActive ? "G17G ACTIVE (1024 MB)" : "STANDARD", S->Info.GpuPoolActive ? COL_GREEN : COL_AMBER); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.StorageDeviceCount, 1); StrCatA (Tmp, sizeof (Tmp), " NVMe/Block (");
  AppendU64 (Tmp, sizeof (Tmp), S->Info.PrimaryDiskGiB, 1); StrCatA (Tmp, sizeof (Tmp), " GB)");
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "STORAGE", Tmp, COL_TEXT_BRIGHT); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.SimplePointerCount + S->Info.AbsPointerCount, 1); StrCatA (Tmp, sizeof (Tmp), " Mouse / ");
  AppendU64 (Tmp, sizeof (Tmp), S->Info.KeyboardCount, 1); StrCatA (Tmp, sizeof (Tmp), " KB");
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "INPUT HID", Tmp, (S->Info.SimplePointerCount + S->Info.AbsPointerCount) > 0 ? COL_GREEN : COL_AMBER); Y += 32;

  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "CONFIG ESP", S->Info.EspWritable ? "PERSISTENT FAT32" : "RAM NVRAM", S->Info.EspWritable ? COL_GREEN : COL_AMBER); Y += 32;
  DrawKeyValue (Buf, Bw, Bh, Col0X + 16, Y, ColW - 32, "FIRMWARE", S->Info.FirmwareVersion, COL_TEXT);

  // Column 1: System Status & Performance Gauges
  DrawChamferedCard (Buf, Bw, Bh, Col1X, BodyY, ColW, BodyH, COL_CARD_BG, COL_BORDER, COL_GREEN);
  DrawText (Buf, Bw, Bh, Col1X + 16, BodyY + 14, "SYSTEM HEALTH & GAUGES", COL_GREEN, 1, TRUE);
  FillRect (Buf, Bw, Bh, Col1X + 16, BodyY + 36, ColW - 32, 1, COL_BORDER);

  Y = BodyY + 54;
  DrawText (Buf, Bw, Bh, Col1X + 16, Y, "M5 GPU LOCAL POOL CARVEOUT", COL_TEXT, 1, FALSE);
  DrawText (Buf, Bw, Bh, Col1X + ColW - 96, Y, S->Info.GpuPoolActive ? "100% OK" : "OFF", S->Info.GpuPoolActive ? COL_GREEN : COL_AMBER, 1, TRUE);
  DrawGaugeBar (Buf, Bw, Bh, Col1X + 16, Y + 24, ColW - 32, 16, S->Info.GpuPoolActive ? 100 : 15, COL_GREEN);
  Y += 62;

  {
    UINT32  ProfPct = (S->Config.PerformanceProfile == AURORA_PROFILE_PERFORMANCE) ? 100 :
                      (S->Config.PerformanceProfile == AURORA_PROFILE_BALANCED) ? 70 : 40;
    UINT32  ProfCol = (S->Config.PerformanceProfile == AURORA_PROFILE_PERFORMANCE) ? COL_ACCENT :
                      (S->Config.PerformanceProfile == AURORA_PROFILE_BALANCED) ? COL_GREEN : COL_AMBER;
    DrawText (Buf, Bw, Bh, Col1X + 16, Y, "PERFORMANCE PROFILE TARGET", COL_TEXT, 1, FALSE);
    DrawText (
      Buf, Bw, Bh, Col1X + ColW - 112, Y,
      (S->Config.PerformanceProfile == AURORA_PROFILE_PERFORMANCE) ? "MAX PERF" :
      (S->Config.PerformanceProfile == AURORA_PROFILE_BALANCED) ? "BALANCED" : "QUIET",
      ProfCol, 1, TRUE
      );
    DrawGaugeBar (Buf, Bw, Bh, Col1X + 16, Y + 24, ColW - 32, 16, ProfPct, ProfCol);
  }
  Y += 62;

  {
    UINT32  SpdPct = (UINT32)S->Config.MouseSpeed * 10u;
    Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Config.MouseSpeed, 1); StrCatA (Tmp, sizeof (Tmp), " / 10");
    DrawText (Buf, Bw, Bh, Col1X + 16, Y, "POINTER SENSITIVITY", COL_TEXT, 1, FALSE);
    DrawText (Buf, Bw, Bh, Col1X + ColW - 76, Y, Tmp, COL_ACCENT, 1, TRUE);
    DrawGaugeBar (Buf, Bw, Bh, Col1X + 16, Y + 24, ColW - 32, 16, SpdPct, COL_ACCENT);
  }
  Y += 68;

  // Boot Priority #1 preview box inside center card
  FillRect (Buf, Bw, Bh, Col1X + 16, Y, ColW - 32, 92, COL_CARD_ALT);
  DrawRectOutline (Buf, Bw, Bh, Col1X + 16, Y, ColW - 32, 92, 1, COL_BORDER);
  DrawText (Buf, Bw, Bh, Col1X + 28, Y + 12, "PRIMARY BOOT TARGET (#1)", COL_AMBER, 1, TRUE);
  if (S->BootEntryCount > 0) {
    DrawTextClipped (Buf, Bw, Bh, Col1X + 28, Y + 38, ColW - 56, S->BootEntries[0].Description, COL_TEXT_BRIGHT, 1, TRUE);
    DrawTextClipped (Buf, Bw, Bh, Col1X + 28, Y + 62, ColW - 56, S->BootEntries[0].DeviceHint, COL_TEXT_MUTED, 1, FALSE);
  } else {
    DrawText (Buf, Bw, Bh, Col1X + 28, Y + 42, "Windows Boot Manager / Internal NVMe", COL_TEXT_BRIGHT, 1, FALSE);
  }
  Y += 110;

  // Quick summary of active mode flags
  DrawText (Buf, Bw, Bh, Col1X + 16, Y, "DIRECT DCP SCANOUT: ZERO-COPY 2560x1664", COL_GREEN, 1, FALSE); Y += 26;
  DrawText (Buf, Bw, Bh, Col1X + 16, Y, "CORNER RAINBOW CHECKPOINTS: REMOVED", COL_ACCENT, 1, FALSE); Y += 26;
  DrawText (Buf, Bw, Bh, Col1X + 16, Y, "FAST 64-BIT GOP BLITTER: ACTIVE", COL_ACCENT, 1, FALSE);

  // Column 2: Quick Profile & Action Deck
  DrawChamferedCard (Buf, Bw, Bh, Col2X, BodyY, ColW, BodyH, COL_CARD_BG, COL_BORDER, COL_AMBER);
  DrawText (Buf, Bw, Bh, Col2X + 16, BodyY + 14, "PROFILE & QUICK LAUNCH", COL_AMBER, 1, TRUE);
  FillRect (Buf, Bw, Bh, Col2X + 16, BodyY + 36, ColW - 32, 1, COL_BORDER);

  DrawText (Buf, Bw, Bh, Col2X + 18, BodyY + 108, "QUICK COMMANDS", COL_TEXT_MUTED, 1, TRUE);

  for (I = AURORA_TAB_COUNT; I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R   = &S->Controls[I];
    BOOLEAN               Hov  = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc  = (S->FocusedCtrlIdx == I);
    switch (R->Id) {
      case CID_PROFILE_BALANCED:
        DrawButton (Buf, Bw, Bh, R, "BALANCED", NULL, S->Config.PerformanceProfile == AURORA_PROFILE_BALANCED, Hov, Foc, COL_GREEN);
        break;
      case CID_PROFILE_PERFORMANCE:
        DrawButton (Buf, Bw, Bh, R, "PERF", NULL, S->Config.PerformanceProfile == AURORA_PROFILE_PERFORMANCE, Hov, Foc, COL_ACCENT);
        break;
      case CID_PROFILE_QUIET:
        DrawButton (Buf, Bw, Bh, R, "QUIET", NULL, S->Config.PerformanceProfile == AURORA_PROFILE_QUIET, Hov, Foc, COL_AMBER);
        break;
      case CID_BTN_BOOT_OS:
        DrawButton (Buf, Bw, Bh, R, ">> BOOT WINDOWS / OS NOW", NULL, TRUE, Hov, Foc, COL_GREEN);
        break;
      case CID_BTN_UEFI_SHELL:
        DrawButton (Buf, Bw, Bh, R, ">_ LAUNCH UEFI SHELL", NULL, FALSE, Hov, Foc, COL_ACCENT);
        break;
      case CID_BTN_SCAN_USB:
        DrawButton (Buf, Bw, Bh, R, "[+] SCAN USB MOUSE & KEYBOARD", NULL, FALSE, Hov, Foc, COL_AMBER);
        break;
      case CID_BTN_SAVE_REBOOT:
        DrawButton (Buf, Bw, Bh, R, "[R] SAVE SETTINGS & REBOOT", NULL, FALSE, Hov, Foc, COL_RED);
        break;
      default:
        break;
    }
  }
}

static VOID
RenderBootManager (
  const AURORA_BIOS_STATE  *S,
  UINT32                   *Buf,
  INT32                    M,
  INT32                    BodyY,
  INT32                    BodyH
  )
{
  INT32  Bw     = S->Width;
  INT32  Bh     = S->Height;
  INT32  LeftW  = ((Bw - M * 2 - 20) * 58) / 100;
  INT32  RightX = M + LeftW + 20;
  INT32  RightW = Bw - M - RightX;
  INT32  I;
  char   Tmp[64];

  DrawChamferedCard (Buf, Bw, Bh, M, BodyY, LeftW, BodyH, COL_CARD_BG, COL_BORDER, COL_ACCENT);
  DrawText (Buf, Bw, Bh, M + 16, BodyY + 14, "UEFI BOOT PRIORITY ORDER (CLICK OR +/- TO REORDER)", COL_ACCENT, 1, TRUE);
  FillRect (Buf, Bw, Bh, M + 16, BodyY + 36, LeftW - 32, 1, COL_BORDER);

  if (S->BootEntryCount == 0) {
    DrawText (Buf, Bw, Bh, M + 24, BodyY + 72, "No UEFI Boot#### options enumerated yet.", COL_TEXT_MUTED, 1, FALSE);
  }

  DrawChamferedCard (Buf, Bw, Bh, RightX, BodyY, RightW, BodyH, COL_CARD_BG, COL_BORDER, COL_AMBER);
  DrawText (Buf, Bw, Bh, RightX + 16, BodyY + 14, "BOOT CONFIGURATION", COL_AMBER, 1, TRUE);
  FillRect (Buf, Bw, Bh, RightX + 16, BodyY + 36, RightW - 32, 1, COL_BORDER);

  for (I = AURORA_TAB_COUNT; I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R  = &S->Controls[I];
    BOOLEAN               Hov = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc = (S->FocusedCtrlIdx == I);

    if (R->Kind == AURORA_CTRL_BOOT_ROW) {
      UINT32                    Idx = R->Param;
      const AURORA_BOOT_ENTRY   *E  = &S->BootEntries[Idx];
      BOOLEAN                   Sel = (Idx == S->SelectedBootIdx);
      UINT32                    Bg  = Sel ? COL_ACCENT_DARK : (Hov ? COL_CARD_HOVER : COL_CARD_ALT);
      UINT32                    Bd  = (Sel || Foc || Hov) ? COL_ACCENT : COL_BORDER;

      FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
      DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, (Sel || Foc) ? 2 : 1, Bd);
      FillRect (Buf, Bw, Bh, R->X, R->Y, 4, R->H, Sel ? COL_GREEN : COL_BORDER);

      Tmp[0] = '#'; Tmp[1] = '\0';
      AppendU64 (Tmp, sizeof (Tmp), Idx + 1, 1);
      StrCatA (Tmp, sizeof (Tmp), " [Boot");
      AppendHex16 (Tmp, sizeof (Tmp), E->OptionNumber);
      StrCatA (Tmp, sizeof (Tmp), "] ");
      StrCatA (Tmp, sizeof (Tmp), E->Description);
      DrawTextClipped (Buf, Bw, Bh, R->X + 14, R->Y + 11, R->W - 24, Tmp, Sel ? COL_TEXT_BRIGHT : COL_TEXT, 1, Sel);
    } else if (R->Id == CID_BOOT_MOVE_UP) {
      DrawButton (Buf, Bw, Bh, R, "^ MOVE UP", NULL, FALSE, Hov, Foc, COL_ACCENT);
    } else if (R->Id == CID_BOOT_MOVE_DOWN) {
      DrawButton (Buf, Bw, Bh, R, "v MOVE DOWN", NULL, FALSE, Hov, Foc, COL_ACCENT);
    } else if (R->Id == CID_BOOT_LAUNCH_SEL) {
      DrawButton (Buf, Bw, Bh, R, ">> BOOT NOW", NULL, TRUE, Hov, Foc, COL_GREEN);
    } else if (R->Id == CID_SLIDER_TIMEOUT) {
      Tmp[0] = '\0';
      if (S->Config.BootTimeoutSec == 0) {
        StrCopyA (Tmp, sizeof (Tmp), "0s INST");
      } else {
        AppendU64 (Tmp, sizeof (Tmp), S->Config.BootTimeoutSec, 1);
        StrCatA (Tmp, sizeof (Tmp), " sec");
      }
      DrawSliderRow (Buf, Bw, Bh, R, "BIOS Prompt Timeout (0s = Instant)", Tmp, S->Config.BootTimeoutSec, 0, 10, Hov, Foc);
    } else if (R->Id == CID_TOGGLE_FASTBOOT) {
      DrawToggleRow (Buf, Bw, Bh, R, "Fast Boot (Zero-Delay Hand-off)", S->Config.FastBoot != 0, Hov, Foc);
    } else if (R->Id == CID_TOGGLE_SPLASH) {
      DrawToggleRow (Buf, Bw, Bh, R, "Aurora Boot Splash Graphic", S->Config.BootSplashLogo != 0, Hov, Foc);
    } else if (R->Id == CID_CHOICE_DEF_TARGET) {
      const char  *Txt = (S->Config.DefaultBootTarget == AURORA_BOOT_TARGET_OS) ? "WINDOWS / OS" :
                         (S->Config.DefaultBootTarget == AURORA_BOOT_TARGET_SHELL) ? "UEFI SHELL" : "STAY IN BIOS";
      DrawChoiceRow (Buf, Bw, Bh, R, "Default Boot Target", Txt, Hov, Foc, COL_ACCENT);
    }
  }
}

static VOID
RenderHardware (
  const AURORA_BIOS_STATE  *S,
  UINT32                   *Buf,
  INT32                    M,
  INT32                    BodyY,
  INT32                    BodyH
  )
{
  INT32  Bw     = S->Width;
  INT32  Bh     = S->Height;
  INT32  HalfW  = (Bw - M * 2 - 20) / 2;
  INT32  RightX = M + HalfW + 20;
  INT32  I;
  char   Tmp[64];

  DrawChamferedCard (Buf, Bw, Bh, M, BodyY, HalfW, BodyH, COL_CARD_BG, COL_BORDER, COL_ACCENT);
  DrawText (Buf, Bw, Bh, M + 16, BodyY + 14, "APPLE M5 GPU & DISPLAY ENGINE", COL_ACCENT, 1, TRUE);
  FillRect (Buf, Bw, Bh, M + 16, BodyY + 36, HalfW - 32, 1, COL_BORDER);

  DrawChamferedCard (Buf, Bw, Bh, RightX, BodyY, HalfW, BodyH, COL_CARD_BG, COL_BORDER, COL_AMBER);
  DrawText (Buf, Bw, Bh, RightX + 16, BodyY + 14, "PERIPHERALS & SERIAL TELEMETRY", COL_AMBER, 1, TRUE);
  FillRect (Buf, Bw, Bh, RightX + 16, BodyY + 36, HalfW - 32, 1, COL_BORDER);

  for (I = AURORA_TAB_COUNT; I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R  = &S->Controls[I];
    BOOLEAN               Hov = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc = (S->FocusedCtrlIdx == I);

    switch (R->Id) {
      case CID_TOGGLE_GPU_POOL:
        DrawToggleRow (Buf, Bw, Bh, R, "G17G GPU Local Memory Pool (1024 MB)", S->Config.GpuLocalPoolEnable != 0, Hov, Foc);
        break;
      case CID_CHOICE_CON_SCALE:
        DrawChoiceRow (Buf, Bw, Bh, R, "UEFI Console Scaling", (S->Config.ConsoleScaleMode == 2) ? "2x HiDPI" : "1x NATIVE", Hov, Foc, COL_ACCENT);
        break;
      case CID_SLIDER_MOUSE_SPEED:
        Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Config.MouseSpeed, 1); StrCatA (Tmp, sizeof (Tmp), " / 10");
        DrawSliderRow (Buf, Bw, Bh, R, "Mouse Cursor Sensitivity (Live)", Tmp, S->Config.MouseSpeed, 1, 10, Hov, Foc);
        break;
      case CID_CHOICE_SERIAL_DBG: {
        const char  *Dbg = (S->Config.SerialDebugLevel == AURORA_DEBUG_ERRORS) ? "ERRORS ONLY" :
                           (S->Config.SerialDebugLevel == AURORA_DEBUG_INFO) ? "INFO + BOOT" : "FULL VERBOSE";
        DrawChoiceRow (Buf, Bw, Bh, R, "UART Serial Debug Level", Dbg, Hov, Foc, COL_AMBER);
        break;
      }
      case CID_TOGGLE_USB_AUTO:
        DrawToggleRow (Buf, Bw, Bh, R, "USB HID Auto-Connect at Boot", S->Config.UsbHidAutoConnect != 0, Hov, Foc);
        break;
      case CID_TOGGLE_USB_BOOT:
        DrawToggleRow (Buf, Bw, Bh, R, "Type-C / External USB Boot Media", S->Config.ExternalUsbBoot != 0, Hov, Foc);
        break;
      default:
        break;
    }
  }

  // Informational hardware notes at bottom of cards
  DrawText (Buf, Bw, Bh, M + 18, BodyY + 292, "HARDWARE DISPLAY PIPELINE:", COL_TEXT_MUTED, 1, TRUE);
  DrawText (Buf, Bw, Bh, M + 18, BodyY + 318, "- Panel: Native 2560x1664 X2R10G10B10 (30-bit Apple DCP)", COL_TEXT, 1, FALSE);
  DrawText (Buf, Bw, Bh, M + 18, BodyY + 344, "- GOP Fast Blitter: 64-bit Write-Combining Burst Active", COL_GREEN, 1, FALSE);
  DrawText (Buf, Bw, Bh, M + 18, BodyY + 370, "- Native Windows WDDM: AuroraD3D11 + Direct DCP Scanout", COL_ACCENT, 1, FALSE);

  DrawText (Buf, Bw, Bh, RightX + 18, BodyY + 292, "PERSISTENCE BACKING:", COL_TEXT_MUTED, 1, TRUE);
  DrawText (Buf, Bw, Bh, RightX + 18, BodyY + 318, "- UEFI Runtime Variable: AuroraBiosConfig", COL_TEXT, 1, FALSE);
  DrawText (Buf, Bw, Bh, RightX + 18, BodyY + 344, "- NVMe ESP Mirror: \\EFI\\Aurora\\bios.cfg (CRC32 verified)", COL_GREEN, 1, FALSE);
  DrawText (Buf, Bw, Bh, RightX + 18, BodyY + 370, "- Survives full power cycles across emulated NVRAM resets", COL_TEXT, 1, FALSE);
}

static VOID
RenderDiagnostics (
  const AURORA_BIOS_STATE  *S,
  UINT32                   *Buf,
  INT32                    M,
  INT32                    BodyY,
  INT32                    BodyH
  )
{
  INT32  Bw     = S->Width;
  INT32  Bh     = S->Height;
  INT32  HalfW  = (Bw - M * 2 - 20) / 2;
  INT32  RightX = M + HalfW + 20;
  INT32  Y;
  INT32  I;
  char   Tmp[64];

  // Left Card: Hardware & Storage Inventory
  DrawChamferedCard (Buf, Bw, Bh, M, BodyY, HalfW, BodyH, COL_CARD_BG, COL_BORDER, COL_GREEN);
  DrawText (Buf, Bw, Bh, M + 16, BodyY + 14, "LIVE HARDWARE & STORAGE INVENTORY", COL_GREEN, 1, TRUE);
  FillRect (Buf, Bw, Bh, M + 16, BodyY + 36, HalfW - 32, 1, COL_BORDER);

  Y = BodyY + 56;
  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.StorageDeviceCount, 1); StrCatA (Tmp, sizeof (Tmp), " handles");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "BLOCK I/O HANDLES", Tmp, COL_TEXT_BRIGHT); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.PrimaryDiskGiB, 1); StrCatA (Tmp, sizeof (Tmp), " GiB (Apple ANS NVMe)");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "PRIMARY STORAGE", Tmp, COL_GREEN); Y += 34;

  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "ESP CONFIG FILE", S->Info.EspConfigFound ? "FOUND (CRC32 VALID)" : "WILL CREATE ON SAVE", S->Info.EspConfigFound ? COL_GREEN : COL_AMBER); Y += 34;
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "ESP WRITE ACCESS", S->Info.EspWritable ? "WRITABLE FAT32" : "READ-ONLY / ABSENT", S->Info.EspWritable ? COL_GREEN : COL_RED); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->BootEntryCount, 1); StrCatA (Tmp, sizeof (Tmp), " boot entries");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "UEFI BOOT OPTIONS", Tmp, COL_ACCENT); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.UptimeSec, 1); StrCatA (Tmp, sizeof (Tmp), " seconds");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, HalfW - 36, "BIOS SESSION UPTIME", Tmp, COL_TEXT_BRIGHT);

  // Right Card: Live Mouse + Keyboard Interactive Test Bench
  DrawChamferedCard (Buf, Bw, Bh, RightX, BodyY, HalfW, BodyH, COL_CARD_BG, COL_BORDER, COL_ACCENT);
  DrawText (Buf, Bw, Bh, RightX + 16, BodyY + 14, "LIVE MOUSE & KEYBOARD TEST BENCH", COL_ACCENT, 1, TRUE);
  FillRect (Buf, Bw, Bh, RightX + 16, BodyY + 36, HalfW - 32, 1, COL_BORDER);

  Y = BodyY + 56;
  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.SimplePointerCount, 1);
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "SIMPLE POINTER (USB MOUSE)", Tmp, S->Info.SimplePointerCount > 0 ? COL_GREEN : COL_AMBER); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.AbsPointerCount, 1);
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "ABSOLUTE POINTER (HID)", Tmp, S->Info.AbsPointerCount > 0 ? COL_GREEN : COL_AMBER); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.KeyboardCount, 1);
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "TEXT INPUT (KB / SERIAL)", Tmp, S->Info.KeyboardCount > 0 ? COL_GREEN : COL_AMBER); Y += 34;

  Tmp[0] = '\0'; StrCatA (Tmp, sizeof (Tmp), "("); AppendI32 (Tmp, sizeof (Tmp), S->MouseX);
  StrCatA (Tmp, sizeof (Tmp), ", "); AppendI32 (Tmp, sizeof (Tmp), S->MouseY);
  StrCatA (Tmp, sizeof (Tmp), ")  Btn="); StrCatA (Tmp, sizeof (Tmp), S->MouseLeftDown ? "DOWN" : "UP");
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "CURSOR POSITION", Tmp, COL_ACCENT); Y += 34;

  Tmp[0] = '\0'; StrCatA (Tmp, sizeof (Tmp), "dX="); AppendI32 (Tmp, sizeof (Tmp), S->Info.LastRawDx);
  StrCatA (Tmp, sizeof (Tmp), "  dY="); AppendI32 (Tmp, sizeof (Tmp), S->Info.LastRawDy);
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "LAST RAW MOUSE DELTA", Tmp, COL_TEXT_BRIGHT); Y += 34;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Info.MouseClicks, 1); StrCatA (Tmp, sizeof (Tmp), " clicks / ");
  AppendU64 (Tmp, sizeof (Tmp), S->Info.KeyPresses, 1); StrCatA (Tmp, sizeof (Tmp), " keys");
  DrawKeyValue (Buf, Bw, Bh, RightX + 18, Y, HalfW - 36, "INTERACTION EVENTS", Tmp, COL_GREEN);

  for (I = AURORA_TAB_COUNT; I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R  = &S->Controls[I];
    BOOLEAN               Hov = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc = (S->FocusedCtrlIdx == I);
    if (R->Id == CID_DIAG_SCAN_USB) {
      DrawButton (Buf, Bw, Bh, R, "[+] SCAN USB HID", NULL, FALSE, Hov, Foc, COL_AMBER);
    } else if (R->Id == CID_DIAG_RESET_INPUT) {
      DrawButton (Buf, Bw, Bh, R, "RESET COUNTERS", NULL, FALSE, Hov, Foc, COL_ACCENT);
    }
  }
}

static VOID
RenderSaveExit (
  const AURORA_BIOS_STATE  *S,
  UINT32                   *Buf,
  INT32                    M,
  INT32                    BodyY,
  INT32                    BodyH
  )
{
  INT32   Bw       = S->Width;
  INT32   Bh       = S->Height;
  INT32   LeftW    = ((Bw - M * 2 - 20) * 42) / 100;
  INT32   RightX   = M + LeftW + 20;
  INT32   RightW   = Bw - M - RightX;
  UINT32  Modified = AuroraBiosCountModified (S);
  INT32   Y;
  INT32   I;
  char    Tmp[64];

  // Left Summary Card
  DrawChamferedCard (Buf, Bw, Bh, M, BodyY, LeftW, BodyH, COL_CARD_BG, COL_BORDER, Modified > 0 ? COL_AMBER : COL_GREEN);
  DrawText (Buf, Bw, Bh, M + 16, BodyY + 14, "CONFIGURATION SUMMARY", Modified > 0 ? COL_AMBER : COL_GREEN, 1, TRUE);
  FillRect (Buf, Bw, Bh, M + 16, BodyY + 36, LeftW - 32, 1, COL_BORDER);

  Y = BodyY + 56;
  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), Modified, 1); StrCatA (Tmp, sizeof (Tmp), " setting(s) changed");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "UNSAVED CHANGES", Tmp, Modified > 0 ? COL_AMBER : COL_GREEN); Y += 38;

  DrawKeyValue (
    Buf, Bw, Bh, M + 18, Y, LeftW - 36, "PROFILE",
    (S->Config.PerformanceProfile == AURORA_PROFILE_PERFORMANCE) ? "PERFORMANCE" :
    (S->Config.PerformanceProfile == AURORA_PROFILE_BALANCED) ? "BALANCED" : "QUIET",
    COL_ACCENT
    ); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Config.BootTimeoutSec, 1); StrCatA (Tmp, sizeof (Tmp), "s");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "BIOS TIMEOUT", Tmp, COL_TEXT_BRIGHT); Y += 32;
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "FAST BOOT", S->Config.FastBoot ? "ON" : "OFF", COL_TEXT_BRIGHT); Y += 32;
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "GPU LOCAL POOL", S->Config.GpuLocalPoolEnable ? "ENABLED" : "DISABLED", COL_GREEN); Y += 32;

  Tmp[0] = '\0'; AppendU64 (Tmp, sizeof (Tmp), S->Config.MouseSpeed, 1); StrCatA (Tmp, sizeof (Tmp), " / 10");
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "MOUSE SPEED", Tmp, COL_ACCENT); Y += 32;
  DrawKeyValue (Buf, Bw, Bh, M + 18, Y, LeftW - 36, "BOOT ORDER", S->BootOrderDirty ? "MODIFIED" : "UNCHANGED", S->BootOrderDirty ? COL_AMBER : COL_TEXT);

  // Right Action Grid
  DrawChamferedCard (Buf, Bw, Bh, RightX, BodyY, RightW, BodyH, COL_CARD_BG, COL_BORDER, COL_ACCENT);
  DrawText (Buf, Bw, Bh, RightX + 16, BodyY + 14, "SAVE, EXIT & POWER CONTROLS", COL_ACCENT, 1, TRUE);
  FillRect (Buf, Bw, Bh, RightX + 16, BodyY + 36, RightW - 32, 1, COL_BORDER);

  for (I = AURORA_TAB_COUNT; I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R  = &S->Controls[I];
    BOOLEAN               Hov = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc = (S->FocusedCtrlIdx == I);

    switch (R->Id) {
      case CID_EXIT_SAVE_BOOT:
        DrawButton (Buf, Bw, Bh, R, "SAVE & BOOT OS", "Write NVRAM + ESP & launch OS", TRUE, Hov, Foc, COL_GREEN);
        break;
      case CID_EXIT_SAVE_REBOOT:
        DrawButton (Buf, Bw, Bh, R, "SAVE & REBOOT", "Commit changes & cold reset", FALSE, Hov, Foc, COL_ACCENT);
        break;
      case CID_EXIT_DISCARD_BOOT:
        DrawButton (Buf, Bw, Bh, R, "DISCARD & BOOT", "Revert changes & continue boot", FALSE, Hov, Foc, COL_AMBER);
        break;
      case CID_EXIT_LOAD_DEFAULTS:
        DrawButton (Buf, Bw, Bh, R, "LOAD DEFAULTS", "Reset to optimal M5 J813 profile", FALSE, Hov, Foc, COL_AMBER);
        break;
      case CID_EXIT_SHELL:
        DrawButton (Buf, Bw, Bh, R, "UEFI SHELL", "Open interactive EFI Shell", FALSE, Hov, Foc, COL_ACCENT);
        break;
      case CID_EXIT_POWER_OFF:
        DrawButton (Buf, Bw, Bh, R, "POWER OFF", "Shut down Apple Silicon host", FALSE, Hov, Foc, COL_RED);
        break;
      default:
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Full Backbuffer Render + Crisp Arrow Mouse Cursor Sprite
// ---------------------------------------------------------------------------
VOID
AuroraBiosRender (
  AURORA_BIOS_STATE  *S,
  UINT32             *Buf
  )
{
  static const char  *TabLabels[AURORA_TAB_COUNT] = {
    "[F1] DASHBOARD",
    "[F2] BOOT MANAGER",
    "[F3] HARDWARE & GPU",
    "[F4] DIAGNOSTICS",
    "[F5] SAVE & EXIT"
  };
  INT32  Bw    = S->Width;
  INT32  Bh    = S->Height;
  INT32  M     = 24;
  INT32  BodyY = 122;
  INT32  BodyH = Bh - BodyY - 52;
  INT32  I;
  char   ClockStr[32];

  if (Buf == NULL || Bw <= 0 || Bh <= 0) {
    return;
  }

  // Deep carbon background + subtle top header band
  FillRect (Buf, Bw, Bh, 0, 0, Bw, Bh, COL_BG);
  FillRect (Buf, Bw, Bh, 0, 0, Bw, 56, COL_HEADER_BG);
  FillRect (Buf, Bw, Bh, 0, 56, Bw, 2, COL_ACCENT);
  // Subtle gaming corner accent bar
  FillRect (Buf, Bw, Bh, 0, 0, 6, 56, COL_ACCENT);

  // Header Brand Title
  DrawText (Buf, Bw, Bh, 22, 10, "AURORA", COL_ACCENT, 2, TRUE);
  DrawText (Buf, Bw, Bh, 140, 10, "BIOS", COL_TEXT_BRIGHT, 2, TRUE);
  DrawText (Buf, Bw, Bh, 224, 20, "//  APPLE M5 J813 GAMING UEFI", COL_TEXT_MUTED, 1, TRUE);

  // Right-side Live Telemetry Badges in Header
  ClockStr[0] = '\0';
  AppendU64 (ClockStr, sizeof (ClockStr), S->Info.ClockHour, 2); StrCatA (ClockStr, sizeof (ClockStr), ":");
  AppendU64 (ClockStr, sizeof (ClockStr), S->Info.ClockMin, 2);  StrCatA (ClockStr, sizeof (ClockStr), ":");
  AppendU64 (ClockStr, sizeof (ClockStr), S->Info.ClockSec, 2);
  FillRect (Buf, Bw, Bh, Bw - 116, 12, 94, 32, COL_CARD_ALT);
  DrawRectOutline (Buf, Bw, Bh, Bw - 116, 12, 94, 32, 1, COL_BORDER);
  DrawText (Buf, Bw, Bh, Bw - 102, 20, ClockStr, COL_ACCENT, 1, TRUE);

  // Dirty changes badge if any modified
  {
    UINT32  Mod = AuroraBiosCountModified (S);
    if (Mod > 0) {
      char  ModStr[32];
      ModStr[0] = '\0'; AppendU64 (ModStr, sizeof (ModStr), Mod, 1); StrCatA (ModStr, sizeof (ModStr), " UNSAVED");
      FillRect (Buf, Bw, Bh, Bw - 236, 12, 110, 32, COL_AMBER_DARK);
      DrawRectOutline (Buf, Bw, Bh, Bw - 236, 12, 110, 32, 1, COL_AMBER);
      DrawText (Buf, Bw, Bh, Bw - 224, 20, ModStr, COL_AMBER, 1, TRUE);
    }
  }

  // Top Navigation Tabs
  for (I = 0; I < AURORA_TAB_COUNT && I < S->ControlCount; I++) {
    const AURORA_UI_RECT  *R      = &S->Controls[I];
    BOOLEAN               Active  = ((INT32)S->ActiveTab == I);
    BOOLEAN               Hov     = (S->HoveredCtrlIdx == I);
    BOOLEAN               Foc     = (S->FocusedCtrlIdx == I);
    UINT32                Bg      = Active ? COL_ACCENT_DARK : (Hov ? COL_CARD_HOVER : COL_CARD_BG);
    UINT32                Bd      = Active ? COL_ACCENT : ((Foc || Hov) ? COL_BORDER_HI : COL_BORDER);
    UINT32                TxtCol  = Active ? COL_TEXT_BRIGHT : (Hov ? COL_ACCENT : COL_TEXT);

    FillRect (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, Bg);
    DrawRectOutline (Buf, Bw, Bh, R->X, R->Y, R->W, R->H, (Active || Foc) ? 2 : 1, Bd);
    if (Active) {
      FillRect (Buf, Bw, Bh, R->X, R->Y + R->H - 3, R->W, 3, COL_ACCENT);
    }
    DrawTextClipped (Buf, Bw, Bh, R->X + 12, R->Y + 11, R->W - 20, TabLabels[I], TxtCol, 1, Active);
  }

  // Active Tab Body
  switch (S->ActiveTab) {
    case AURORA_TAB_DASHBOARD: RenderDashboard (S, Buf, M, BodyY, BodyH);   break;
    case AURORA_TAB_BOOT:      RenderBootManager (S, Buf, M, BodyY, BodyH); break;
    case AURORA_TAB_HARDWARE:  RenderHardware (S, Buf, M, BodyY, BodyH);    break;
    case AURORA_TAB_DIAG:      RenderDiagnostics (S, Buf, M, BodyY, BodyH); break;
    case AURORA_TAB_EXIT:      RenderSaveExit (S, Buf, M, BodyY, BodyH);    break;
    default: break;
  }

  // Bottom Status / Hotkey Footer Bar
  FillRect (Buf, Bw, Bh, 0, Bh - 38, Bw, 38, COL_FOOTER_BG);
  FillRect (Buf, Bw, Bh, 0, Bh - 38, Bw, 1, COL_BORDER);
  FillRect (Buf, Bw, Bh, 14, Bh - 25, 8, 12, S->StatusColor ? S->StatusColor : COL_ACCENT);
  DrawTextClipped (Buf, Bw, Bh, 30, Bh - 27, Bw - 44, S->StatusMsg, S->StatusColor ? S->StatusColor : COL_TEXT, 1, FALSE);

  S->FullRedrawNeeded = FALSE;
}

VOID
AuroraBiosDrawCursor (
  UINT32  *Buf,
  INT32   Width,
  INT32   Height,
  INT32   MouseX,
  INT32   MouseY
  )
{
  // Crisp 14x20 gaming arrow cursor: 'X'=dark outline, '#'=bright white, 'C'=cyan glow core
  static const char  *Sprite[20] = {
    "X.............",
    "XX............",
    "X#X...........",
    "X##X..........",
    "X#C#X.........",
    "X#CC#X........",
    "X#CCC#X.......",
    "X#CCCC#X......",
    "X#CCCCC#X.....",
    "X#CCCCCC#X....",
    "X#CCCCCCC#X...",
    "X#CCCCCCCC#X..",
    "X#CCCCCXXXXX..",
    "X#CC#CCX......",
    "X#C#X#C#X.....",
    "X##X.X#C#X....",
    "XX...X#C#X....",
    "......X##X....",
    "......X##X....",
    ".......XX....."
  };
  INT32  Py;
  INT32  Px;

  if (Buf == NULL) {
    return;
  }
  for (Py = 0; Py < 20; Py++) {
    INT32  Dy = MouseY + Py;
    if (Dy < 0 || Dy >= Height) {
      continue;
    }
    for (Px = 0; Px < 14; Px++) {
      INT32  Dx = MouseX + Px;
      char   C  = Sprite[Py][Px];
      if (C == '.' || Dx < 0 || Dx >= Width) {
        continue;
      }
      if (C == 'X') {
        Buf[(UINTN)Dy * (UINTN)Width + (UINTN)Dx] = 0x0002060Du;
      } else if (C == '#') {
        Buf[(UINTN)Dy * (UINTN)Width + (UINTN)Dx] = COL_TEXT_BRIGHT;
      } else {
        Buf[(UINTN)Dy * (UINTN)Width + (UINTN)Dx] = COL_ACCENT;
      }
    }
  }
}

/** @file
  Aurora Silicon Gaming UEFI BIOS - Shared Configuration & Core UI API.

  Dual-compiled for both AArch64 UEFI (CLANGPDB) and host desk unit tests
  (AURORA_BIOS_HOST_TEST) so layout, hit-testing, navigation, config CRC32
  persistence, and rendering are thoroughly verified before flashing.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef AURORA_BIOS_CONFIG_H_
#define AURORA_BIOS_CONFIG_H_

#ifdef AURORA_BIOS_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef size_t    UINTN;
typedef bool      BOOLEAN;
#define VOID      void
#define AURORA_OFFSET_OF(T, F)  offsetof (T, F)
#ifndef TRUE
#define TRUE  true
#endif
#ifndef FALSE
#define FALSE false
#endif
#else
#include <Uefi.h>
#define AURORA_OFFSET_OF(T, F)  OFFSET_OF (T, F)
#endif

// {A05A8B10-6C4E-4F29-B317-9E2C4D8A5F01}
#define AURORA_BIOS_CONFIG_GUID_INIT \
  { 0xA05A8B10, 0x6C4E, 0x4F29, { 0xB3, 0x17, 0x9E, 0x2C, 0x4D, 0x8A, 0x5F, 0x01 } }

#define AURORA_BIOS_CONFIG_VAR_NAME  L"AuroraBiosConfig"
#define AURORA_BIOS_CONFIG_ESP_PATH  L"\\EFI\\Aurora\\bios.cfg"
#define AURORA_BIOS_CONFIG_MAGIC     0x41555242u  // 'AURB'
#define AURORA_BIOS_CONFIG_VERSION   1u

#define AURORA_BIOS_MAX_BOOT_ENTRIES 16
#define AURORA_BIOS_MAX_TEXT         64
#define AURORA_BIOS_MAX_CONTROLS     32

// Performance Profiles
#define AURORA_PROFILE_BALANCED      0u
#define AURORA_PROFILE_PERFORMANCE   1u
#define AURORA_PROFILE_QUIET         2u

// Default Boot Targets
#define AURORA_BOOT_TARGET_OS        0u
#define AURORA_BOOT_TARGET_SHELL     1u
#define AURORA_BOOT_TARGET_BIOS      2u

// Serial Debug Verbosity
#define AURORA_DEBUG_ERRORS          0u
#define AURORA_DEBUG_INFO            1u
#define AURORA_DEBUG_VERBOSE         2u

// Tabs
typedef enum {
  AURORA_TAB_DASHBOARD = 0,
  AURORA_TAB_BOOT      = 1,
  AURORA_TAB_HARDWARE  = 2,
  AURORA_TAB_DIAG      = 3,
  AURORA_TAB_EXIT      = 4,
  AURORA_TAB_COUNT     = 5
} AURORA_BIOS_TAB;

// Actions requested by the core UI back to the UEFI host loop
typedef enum {
  AURORA_ACTION_NONE = 0,
  AURORA_ACTION_SAVE_CONFIG,
  AURORA_ACTION_SAVE_AND_BOOT,
  AURORA_ACTION_SAVE_AND_REBOOT,
  AURORA_ACTION_DISCARD_AND_BOOT,
  AURORA_ACTION_BOOT_OPTION,      // Boot Entry Index = ActionArg
  AURORA_ACTION_LAUNCH_SHELL,
  AURORA_ACTION_SCAN_USB_HID,
  AURORA_ACTION_POWER_OFF,
  AURORA_ACTION_REORDER_BOOT      // Reorder occurred in BootEntries[]
} AURORA_BIOS_ACTION;

// Input key abstraction (normalized from UEFI SimpleTextInputEx & serial)
typedef enum {
  AURORA_KEY_NONE = 0,
  AURORA_KEY_UP,
  AURORA_KEY_DOWN,
  AURORA_KEY_LEFT,
  AURORA_KEY_RIGHT,
  AURORA_KEY_TAB,
  AURORA_KEY_SHIFT_TAB,
  AURORA_KEY_ENTER,
  AURORA_KEY_SPACE,
  AURORA_KEY_ESC,
  AURORA_KEY_F1,
  AURORA_KEY_F2,
  AURORA_KEY_F3,
  AURORA_KEY_F4,
  AURORA_KEY_F5,
  AURORA_KEY_F9,
  AURORA_KEY_F10,
  AURORA_KEY_PLUS,
  AURORA_KEY_MINUS
} AURORA_BIOS_KEY;

// Persistent Configuration (stored in UEFI NVRAM + ESP \EFI\Aurora\bios.cfg)
#pragma pack(push, 1)
typedef struct {
  UINT32  Magic;
  UINT32  Version;
  UINT32  PerformanceProfile;   // 0=Balanced, 1=Performance, 2=Quiet
  UINT32  BootTimeoutSec;       // 0..10s (0 = instant boot unless hotkey held)
  UINT8   FastBoot;             // 0/1
  UINT8   BootSplashLogo;       // 0/1
  UINT8   DefaultBootTarget;    // 0=OS, 1=UEFI Shell, 2=BIOS
  UINT8   GpuLocalPoolEnable;   // 0/1
  UINT8   ConsoleScaleMode;     // 1=1x Native, 2=2x HiDPI
  UINT8   MouseSpeed;           // 1..10
  UINT8   SerialDebugLevel;     // 0=Errors, 1=Info, 2=Verbose
  UINT8   UsbHidAutoConnect;    // 0=On-Demand Safe, 1=Auto
  UINT8   ExternalUsbBoot;      // 0/1
  UINT8   EnterBiosOnce;        // One-shot flag consumed on boot
  UINT8   Reserved[2];
  UINT32  Crc32;
} AURORA_BIOS_CONFIG;
#pragma pack(pop)

// Live Boot Option entry populated from EfiBootManagerGetLoadOptions
typedef struct {
  UINT16  OptionNumber;         // e.g. 0x0001 for Boot0001
  BOOLEAN IsActive;
  BOOLEAN IsShell;
  char    Description[AURORA_BIOS_MAX_TEXT];
  char    DeviceHint[AURORA_BIOS_MAX_TEXT];
} AURORA_BOOT_ENTRY;

// Live Hardware Telemetry populated at UEFI startup & updated dynamically
typedef struct {
  char    PlatformName[48];     // e.g. "Apple MacBook Air (J813 / M5)"
  char    SocName[48];          // e.g. "Apple M5 (T8142) ARMv9"
  char    FirmwareVersion[48];  // e.g. "Aurora Mu UEFI 2026.11"
  UINT32  CpuCores;
  UINT64  TotalRamMiB;
  UINT32  GopWidth;
  UINT32  GopHeight;
  UINT32  GopBpp;
  BOOLEAN GpuPoolActive;
  UINT32  StorageDeviceCount;
  UINT64  PrimaryDiskGiB;
  BOOLEAN EspConfigFound;
  BOOLEAN EspWritable;
  UINT32  SimplePointerCount;
  UINT32  AbsPointerCount;
  UINT32  KeyboardCount;
  UINT32  ClockHour;
  UINT32  ClockMin;
  UINT32  ClockSec;
  UINT32  UptimeSec;
  INT32   LastRawDx;
  INT32   LastRawDy;
  UINT32  LastKeyCode;
  UINT32  MouseClicks;
  UINT32  KeyPresses;
} AURORA_BIOS_TELEMETRY;

// Interactive hit-testable UI control rectangle (in logical canvas coordinates)
typedef enum {
  AURORA_CTRL_NONE = 0,
  AURORA_CTRL_TAB,
  AURORA_CTRL_BUTTON,
  AURORA_CTRL_TOGGLE,
  AURORA_CTRL_CHOICE,
  AURORA_CTRL_SLIDER,
  AURORA_CTRL_BOOT_ROW
} AURORA_CTRL_KIND;

typedef struct {
  INT32            X;
  INT32            Y;
  INT32            W;
  INT32            H;
  AURORA_CTRL_KIND Kind;
  UINT32           Id;
  UINT32           Param;
} AURORA_UI_RECT;

// Dirty rectangle in logical canvas pixels (inclusive-exclusive)
typedef struct {
  BOOLEAN Valid;
  INT32   MinX;
  INT32   MinY;
  INT32   MaxX;
  INT32   MaxY;
} AURORA_DIRTY_RECT;

// Complete Gaming BIOS UI State
typedef struct {
  // Logical canvas geometry (e.g. 1280x832 on 2560x1664 HiDPI with Scale=2)
  INT32                 Width;
  INT32                 Height;
  INT32                 UiScale;

  AURORA_BIOS_CONFIG    Config;
  AURORA_BIOS_CONFIG    SavedConfig;
  AURORA_BIOS_TELEMETRY Info;

  AURORA_BOOT_ENTRY     BootEntries[AURORA_BIOS_MAX_BOOT_ENTRIES];
  UINT32                BootEntryCount;
  UINT32                SelectedBootIdx;
  BOOLEAN               BootOrderDirty;

  AURORA_BIOS_TAB       ActiveTab;
  INT32                 FocusedCtrlIdx;
  INT32                 HoveredCtrlIdx;

  // Mouse state in logical canvas coordinates
  INT32                 MouseX;
  INT32                 MouseY;
  INT32                 PrevMouseX;
  INT32                 PrevMouseY;
  BOOLEAN               MouseLeftDown;
  BOOLEAN               MousePrevLeftDown;
  BOOLEAN               MouseVisible;
  UINT32                DraggingSliderId;
  UINT32                LastClickTick;
  UINT32                LastClickBootIdx;

  // Status / toast message bar
  char                  StatusMsg[96];
  UINT32                StatusColor;
  UINT32                TickCount;

  // Layout controls computed each frame for active tab
  AURORA_UI_RECT        Controls[AURORA_BIOS_MAX_CONTROLS];
  INT32                 ControlCount;

  // Pending action for UEFI host
  AURORA_BIOS_ACTION    PendingAction;
  UINT32                PendingActionArg;

  // Dirty-rect tracking for lag-free partial GOP blitting
  BOOLEAN               FullRedrawNeeded;
  AURORA_DIRTY_RECT     Dirty;
} AURORA_BIOS_STATE;

// Config helpers
UINT32
AuroraBiosCrc32 (
  const VOID  *Data,
  UINTN       Length
  );

VOID
AuroraBiosConfigSetDefaults (
  AURORA_BIOS_CONFIG  *Cfg
  );

VOID
AuroraBiosConfigFinalize (
  AURORA_BIOS_CONFIG  *Cfg
  );

BOOLEAN
AuroraBiosConfigValidate (
  const AURORA_BIOS_CONFIG  *Cfg
  );

UINT32
AuroraBiosCountModified (
  const AURORA_BIOS_STATE  *State
  );

// UI Lifecycle & Input
VOID
AuroraBiosStateInit (
  AURORA_BIOS_STATE  *State,
  INT32              GopWidth,
  INT32              GopHeight
  );

VOID
AuroraBiosBuildLayout (
  AURORA_BIOS_STATE  *State
  );

VOID
AuroraBiosSetStatus (
  AURORA_BIOS_STATE  *State,
  const char         *Message,
  UINT32             ColorRgb
  );

VOID
AuroraBiosHandleKey (
  AURORA_BIOS_STATE  *State,
  AURORA_BIOS_KEY    Key
  );

VOID
AuroraBiosHandleMouseMove (
  AURORA_BIOS_STATE  *State,
  INT32              NewX,
  INT32              NewY,
  BOOLEAN            LeftDown
  );

VOID
AuroraBiosHandleMouseDelta (
  AURORA_BIOS_STATE  *State,
  INT32              DeltaX,
  INT32              DeltaY,
  BOOLEAN            LeftDown
  );

// Software Backbuffer Renderer (0x00RRGGBB pixels, Width * Height UINT32s)
VOID
AuroraBiosRender (
  AURORA_BIOS_STATE  *State,
  UINT32             *BackBuffer
  );

// Draw just the cursor sprite on top of BackBuffer (or restore patch)
VOID
AuroraBiosDrawCursor (
  UINT32  *BackBuffer,
  INT32   Width,
  INT32   Height,
  INT32   MouseX,
  INT32   MouseY
  );

#endif  // AURORA_BIOS_CONFIG_H_

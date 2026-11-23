/*
 * Host unit test suite for Aurora Silicon Gaming UEFI BIOS Core.
 * Compile with:
 *   clang -Wall -Wextra -Werror -fsanitize=address,undefined \
 *     -DAURORA_BIOS_HOST_TEST -I.. ../AuroraBiosCore.c test_aurora_bios.c \
 *     -o /tmp/test_aurora_bios && /tmp/test_aurora_bios
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "AuroraBiosConfig.h"

static void
TestConfigCrcAndDefaults (void)
{
  AURORA_BIOS_CONFIG Cfg;
  AuroraBiosConfigSetDefaults (&Cfg);
  assert (Cfg.Magic == AURORA_BIOS_CONFIG_MAGIC);
  assert (Cfg.Version == AURORA_BIOS_CONFIG_VERSION);
  assert (Cfg.BootTimeoutSec == 0);
  assert (Cfg.MouseSpeed == 5);
  assert (AuroraBiosConfigValidate (&Cfg) == TRUE);

  // Mutate a byte and verify CRC rejects corruption
  Cfg.BootTimeoutSec = 7;
  assert (AuroraBiosConfigValidate (&Cfg) == FALSE);
  AuroraBiosConfigFinalize (&Cfg);
  assert (AuroraBiosConfigValidate (&Cfg) == TRUE);

  // Clamp out-of-range fields
  Cfg.MouseSpeed = 99;
  Cfg.BootTimeoutSec = 50;
  AuroraBiosConfigFinalize (&Cfg);
  assert (Cfg.MouseSpeed == 10);
  assert (Cfg.BootTimeoutSec == 10);
  assert (AuroraBiosConfigValidate (&Cfg) == TRUE);
  printf ("[PASS] config defaults, clamping, and CRC32 validation\n");
}

static void
TestHiDpiRenderAndAllTabs (void)
{
  AURORA_BIOS_STATE S;
  AuroraBiosStateInit (&S, 2560, 1664);
  assert (S.UiScale == 2);
  assert (S.Width == 1280);
  assert (S.Height == 832);

  // Populate 3 sample UEFI boot options
  S.BootEntryCount = 3;
  S.BootEntries[0].OptionNumber = 0x0001;
  strcpy (S.BootEntries[0].Description, "Windows Boot Manager");
  strcpy (S.BootEntries[0].DeviceHint, "HD(1,GPT)/EFI/Microsoft/Boot/bootmgfw.efi");
  S.BootEntries[1].OptionNumber = 0x0002;
  strcpy (S.BootEntries[1].Description, "UEFI Internal Shell");
  strcpy (S.BootEntries[1].DeviceHint, "FvVol/Shell.efi");
  S.BootEntries[2].OptionNumber = 0x0003;
  strcpy (S.BootEntries[2].Description, "USB External Recovery");
  strcpy (S.BootEntries[2].DeviceHint, "USB(0,0)/EFI/BOOT/BOOTAA64.EFI");

  UINT32 *Buf = calloc ((size_t)S.Width * (size_t)S.Height, sizeof (UINT32));
  assert (Buf != NULL);

  // Render every tab and confirm rich non-uniform pixels
  for (int t = 0; t < AURORA_TAB_COUNT; t++) {
    AuroraBiosHandleKey (&S, (AURORA_BIOS_KEY)(AURORA_KEY_F1 + t));
    assert ((int)S.ActiveTab == t);
    AuroraBiosRender (&S, Buf);
    AuroraBiosDrawCursor (Buf, S.Width, S.Height, S.MouseX, S.MouseY);

    size_t nonBg = 0;
    for (int i = 0; i < S.Width * S.Height; i++) {
      if (Buf[i] != 0x000A0E17u) nonBg++;
    }
    assert (nonBg > 100000);
  }

  free (Buf);
  printf ("[PASS] HiDPI 2560x1664 (2x -> 1280x832) render across all 5 tabs\n");
}

static void
TestMouseHitTestingAndDirtyRect (void)
{
  AURORA_BIOS_STATE S;
  AuroraBiosStateInit (&S, 2560, 1664);
  S.BootEntryCount = 2;
  S.BootEntries[0].OptionNumber = 0x0001;
  strcpy (S.BootEntries[0].Description, "Windows Boot Manager");
  S.BootEntries[1].OptionNumber = 0x0002;
  strcpy (S.BootEntries[1].Description, "UEFI Shell");

  // Clear dirty rect after initial full render
  S.FullRedrawNeeded = FALSE;
  S.Dirty.Valid = FALSE;

  // Tiny relative mouse movement in empty space -> tiny dirty rect (lag-free cursor!)
  AuroraBiosHandleMouseMove (&S, 50, 50, FALSE);
  S.Dirty.Valid = FALSE;
  S.FullRedrawNeeded = FALSE;
  AuroraBiosHandleMouseDelta (&S, 4, 2, FALSE);
  assert (S.Dirty.Valid == TRUE);
  assert ((S.Dirty.MaxX - S.Dirty.MinX) < 40);
  assert ((S.Dirty.MaxY - S.Dirty.MinY) < 40);
  assert (S.FullRedrawNeeded == FALSE);

  // Click Tab 1 ([F2] BOOT MANAGER) with mouse
  AURORA_UI_RECT TabBoot = S.Controls[1];
  AuroraBiosHandleMouseMove (&S, TabBoot.X + 10, TabBoot.Y + 10, TRUE);
  AuroraBiosHandleMouseMove (&S, TabBoot.X + 10, TabBoot.Y + 10, FALSE);
  assert (S.ActiveTab == AURORA_TAB_BOOT);

  // Click Boot Row #1, then click MOVE UP -> swaps Boot0002 above Boot0001
  int row1Idx = -1, upIdx = -1, sliderIdx = -1;
  for (int i = 0; i < S.ControlCount; i++) {
    if (S.Controls[i].Kind == AURORA_CTRL_BOOT_ROW && S.Controls[i].Param == 1) row1Idx = i;
    if (S.Controls[i].Id == 320) upIdx = i;       // CID_BOOT_MOVE_UP
    if (S.Controls[i].Id == 330) sliderIdx = i;   // CID_SLIDER_TIMEOUT
  }
  assert (row1Idx >= 0 && upIdx >= 0 && sliderIdx >= 0);

  AURORA_UI_RECT R = S.Controls[row1Idx];
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, TRUE);
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, FALSE);
  assert (S.SelectedBootIdx == 1);

  R = S.Controls[upIdx];
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, TRUE);
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, FALSE);
  assert (S.BootEntries[0].OptionNumber == 0x0002);
  assert (S.BootEntries[1].OptionNumber == 0x0001);
  assert (S.BootOrderDirty == TRUE);
  assert (S.PendingAction == AURORA_ACTION_REORDER_BOOT);
  S.PendingAction = AURORA_ACTION_NONE;

  // Drag timeout slider to midpoint (~5s)
  R = S.Controls[sliderIdx];
  AuroraBiosHandleMouseMove (&S, R.X + 16 + (R.W - 110) / 2, R.Y + 34, TRUE);
  AuroraBiosHandleMouseMove (&S, R.X + 16 + (R.W - 110) / 2, R.Y + 34, FALSE);
  assert (S.Config.BootTimeoutSec == 5);
  assert (AuroraBiosCountModified (&S) >= 2);

  // Double-click boot entry #0 -> AURORA_ACTION_BOOT_OPTION
  R = S.Controls[AURORA_TAB_COUNT]; // row 0
  S.TickCount = 100;
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, TRUE);
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, FALSE);
  assert (S.PendingAction == AURORA_ACTION_NONE);
  S.TickCount = 110;
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, TRUE);
  AuroraBiosHandleMouseMove (&S, R.X + 10, R.Y + 10, FALSE);
  assert (S.PendingAction == AURORA_ACTION_BOOT_OPTION);
  assert (S.PendingActionArg == 0);
  printf ("[PASS] mouse hit-testing, dirty-rect cursor, boot reorder, slider drag, double-click boot\n");
}

static void
TestKeyboardNavigationAndExitFlows (void)
{
  AURORA_BIOS_STATE S;
  AuroraBiosStateInit (&S, 1280, 800);
  assert (S.UiScale == 1);

  // Switch to Hardware tab via F3, toggle GPU pool via Enter
  AuroraBiosHandleKey (&S, AURORA_KEY_F3);
  assert (S.ActiveTab == AURORA_TAB_HARDWARE);
  assert (S.Config.GpuLocalPoolEnable == 1);
  AuroraBiosHandleKey (&S, AURORA_KEY_ENTER);
  assert (S.Config.GpuLocalPoolEnable == 0);

  // Pressing ESC with unsaved changes navigates to Save & Exit instead of discarding silently
  AuroraBiosHandleKey (&S, AURORA_KEY_ESC);
  assert (S.ActiveTab == AURORA_TAB_EXIT);
  assert (S.PendingAction == AURORA_ACTION_NONE);

  // F9 resets defaults
  AuroraBiosHandleKey (&S, AURORA_KEY_F9);
  assert (S.Config.GpuLocalPoolEnable == 1);

  // F10 triggers Save & Boot
  AuroraBiosHandleKey (&S, AURORA_KEY_F10);
  assert (S.PendingAction == AURORA_ACTION_SAVE_AND_BOOT);
  assert (AuroraBiosConfigValidate (&S.Config) == TRUE);

  printf ("[PASS] keyboard F1-F10 navigation, unsaved-change ESC guard, defaults, save & boot\n");
}

int
main (void)
{
  TestConfigCrcAndDefaults ();
  TestHiDpiRenderAndAllTabs ();
  TestMouseHitTestingAndDirtyRect ();
  TestKeyboardNavigationAndExitFlows ();
  printf ("=== ALL AURORA BIOS HOST TESTS PASSED (4/4) ===\n");
  return 0;
}

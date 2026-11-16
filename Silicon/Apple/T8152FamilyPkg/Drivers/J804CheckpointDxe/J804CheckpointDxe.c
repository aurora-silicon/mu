/** @file
  J804 M1: DXE and BDS checkpoint blocks on the panel.

  Row 2 of PanelCheckpointLib, one block per stage, in the order they occur:
    0  white    DXE core entry            (J804DxeCoreCheckpointLib)
    1  cyan     driver dispatch started   (this driver, APRIORI after PcdDxe)
    2  green    BDS entry                 (gMsStartOfBdsNotifyGuid)
    3  yellow   EndOfDxe                  (gEfiEndOfDxeEventGroupGuid)
    4  orange   ReadyToBoot: the Shell is about to start
    5  magenta  first timer tick after ReadyToBoot (timer interrupts work)

  The graphics console clears the panel when it starts, so each later stage
  redraws every block that must already have been passed (all of rows 0 and
  1, and row 2 up to the current stage). After ReadyToBoot a 500 ms timer
  keeps redrawing for two minutes so the blocks survive the Shell's own
  clear screen. On a hang the last block drawn stays on the panel.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Library/DebugLib.h>
#include <Library/PanelCheckpointLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define STAGE_DXE_CORE      0
#define STAGE_DISPATCH      1
#define STAGE_BDS_ENTRY     2
#define STAGE_END_OF_DXE    3
#define STAGE_READY_TO_BOOT 4
#define STAGE_TIMER_TICK    5

#define PREPI_BLOCKS        7
#define REDRAW_PERIOD_100NS (500 * 10000)
#define REDRAW_TICKS        240

STATIC UINTN      mStage;
STATIC UINTN      mRedrawTicks;
STATIC EFI_EVENT  mRedrawEvent;

STATIC
VOID
RedrawUpTo (
  IN UINTN  Stage
  )
{
  UINTN  Index;

  for (Index = 0; Index < PREPI_BLOCKS; Index++) {
    PanelCheckpointDraw (PANEL_CHECKPOINT_ROW_PREPI, Index, PanelCheckpointColor (Index));
    PanelCheckpointDraw (PANEL_CHECKPOINT_ROW_DXE_LOADER, Index, PanelCheckpointColor (Index));
  }

  for (Index = 0; Index <= Stage; Index++) {
    PanelCheckpointDraw (PANEL_CHECKPOINT_ROW_DXE, Index, PanelCheckpointColor (Index));
  }
}

STATIC
VOID
Reach (
  IN UINTN        Stage,
  IN CONST CHAR8  *Name
  )
{
  if (Stage > mStage) {
    mStage = Stage;
  }

  DEBUG ((DEBUG_INFO, "J804_M1_CHECKPOINT_DXE: %a\n", Name));
  RedrawUpTo (mStage);
}

STATIC
VOID
EFIAPI
OnRedrawTimer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  Reach (STAGE_TIMER_TICK, "timer tick after ReadyToBoot");
  mRedrawTicks++;
  if (mRedrawTicks >= REDRAW_TICKS) {
    gBS->SetTimer (mRedrawEvent, TimerCancel, 0);
  }
}

STATIC
VOID
EFIAPI
OnStartOfBds (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  Reach (STAGE_BDS_ENTRY, "BDS entry");
}

STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);
  Reach (STAGE_END_OF_DXE, "EndOfDxe");
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  gBS->CloseEvent (Event);
  Reach (STAGE_READY_TO_BOOT, "ReadyToBoot");

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnRedrawTimer,
                  NULL,
                  &mRedrawEvent
                  );
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mRedrawEvent, TimerPeriodic, REDRAW_PERIOD_100NS);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J804_M1_CHECKPOINT_DXE: no redraw timer: %r\n", Status));
  }
}

EFI_STATUS
EFIAPI
J804CheckpointDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  Event;

  mStage = STAGE_DXE_CORE;
  Reach (STAGE_DISPATCH, "driver dispatch started");

  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_CALLBACK,
         OnStartOfBds,
         NULL,
         &gMsStartOfBdsNotifyGuid,
         &Event
         );
  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_CALLBACK,
         OnEndOfDxe,
         NULL,
         &gEfiEndOfDxeEventGroupGuid,
         &Event
         );
  EfiCreateEventReadyToBootEx (TPL_CALLBACK, OnReadyToBoot, NULL, &Event);

  return EFI_SUCCESS;
}

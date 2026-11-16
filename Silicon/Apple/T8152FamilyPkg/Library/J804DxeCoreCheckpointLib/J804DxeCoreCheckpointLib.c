/** @file
  J804 M1: first DXE checkpoint block, drawn from a NULL-class library linked
  into the DXE core. Its constructor runs inside DxeMain right after the core
  has initialised its memory services, before any driver is dispatched.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/PanelCheckpointLib.h>

EFI_STATUS
EFIAPI
J804DxeCoreCheckpointLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "J804_M1_CHECKPOINT_DXE: DXE core entry\n"));
  PanelCheckpointDraw (PANEL_CHECKPOINT_ROW_DXE, 0, PanelCheckpointColor (0));
  return EFI_SUCCESS;
}

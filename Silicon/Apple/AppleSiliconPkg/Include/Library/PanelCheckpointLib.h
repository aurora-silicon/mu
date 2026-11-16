/** @file
  Coloured progress blocks drawn straight into the boot framebuffer.

  For bring-up without a serial console: each boot stage fills one block, so
  the last block on the panel shows how far firmware got. The blocks sit in
  the lower-left corner, clear of rounded panel corners and of a camera notch
  at the top edge, and below where Shell text normally starts.

  Rows, top to bottom:
    0  PrePi stages (stage codes 0x10..0x16)
    1  PrePi DXE loader and hand-off to DXE core (stage codes 0x20..0x26)
    2  DXE and BDS (platform defined)
    3  fatal errors: a striped block whose position is the error code

  The library reads the framebuffer geometry from the boot_args copy at
  PcdBootArgsPointer, so it is usable once PrePi has copied boot_args there,
  with or without the MMU on, and in DXE until a GOP driver owns the panel.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef PANEL_CHECKPOINT_LIB_H_
#define PANEL_CHECKPOINT_LIB_H_

#define PANEL_CHECKPOINT_ROW_PREPI       0
#define PANEL_CHECKPOINT_ROW_DXE_LOADER  1
#define PANEL_CHECKPOINT_ROW_DXE         2
#define PANEL_CHECKPOINT_ROW_ERROR       3

#define PANEL_CHECKPOINT_BLOCKS_PER_ROW  8

/**
  Colour of block Index in any row: white, cyan, green, yellow, orange,
  magenta, red, blue. The same position has the same colour in every row.

  @param[in] Index  Block within the row.

  @return Colour as 0x00RRGGBB.
**/
UINT32
EFIAPI
PanelCheckpointColor (
  IN UINTN  Index
  );

/**
  Fill one checkpoint block.

  @param[in] Row    PANEL_CHECKPOINT_ROW_* (0..2; use PanelCheckpointError
                    for row 3).
  @param[in] Index  Block within the row, 0..PANEL_CHECKPOINT_BLOCKS_PER_ROW-1.
  @param[in] Rgb    Colour as 0x00RRGGBB. Converted for 30 bpp panels.
**/
VOID
EFIAPI
PanelCheckpointDraw (
  IN UINTN   Row,
  IN UINTN   Index,
  IN UINT32  Rgb
  );

/**
  Draw a striped red/white block in the error row. The block's position is
  the error code, so no two causes look alike.

  @param[in] Code  Error code, 0..PANEL_CHECKPOINT_BLOCKS_PER_ROW-1.
**/
VOID
EFIAPI
PanelCheckpointError (
  IN UINTN  Code
  );

#endif

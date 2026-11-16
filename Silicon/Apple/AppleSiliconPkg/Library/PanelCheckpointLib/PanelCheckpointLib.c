/** @file
  Coloured progress blocks drawn straight into the boot framebuffer.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/AppleDTLib.h>
#include <Library/PanelCheckpointLib.h>
#include <Library/PcdLib.h>

//
// Geometry in pixels. The origin is the lower-left corner: 96 px in from the
// left edge and 96 px up from the bottom edge, which keeps every block off a
// rounded panel corner and far from a notch at the top centre.
//
#define BLOCK_WIDTH     64
#define BLOCK_HEIGHT    32
#define BLOCK_PITCH_X   80
#define BLOCK_PITCH_Y   48
#define MARGIN_LEFT     96
#define MARGIN_BOTTOM   96
#define ROW_COUNT       4
#define STRIPE_HEIGHT   4

#define BOOT_ARGS_DEPTH_MASK  0xFF

STATIC
UINT32
EncodePixel (
  IN UINT32  Rgb,
  IN UINT32  Depth
  )
{
  UINT32  Red;
  UINT32  Green;
  UINT32  Blue;

  if (Depth != 30) {
    return Rgb & 0x00FFFFFF;
  }

  //
  // x2r10g10b10: widen each 8-bit channel, repeating its top two bits so
  // full scale stays full scale.
  //
  Red   = (Rgb >> 16) & 0xFF;
  Green = (Rgb >> 8) & 0xFF;
  Blue  = Rgb & 0xFF;
  Red   = (Red << 2) | (Red >> 6);
  Green = (Green << 2) | (Green >> 6);
  Blue  = (Blue << 2) | (Blue >> 6);
  return (Red << 20) | (Green << 10) | Blue;
}

STATIC
VOID
FillBlock (
  IN UINTN    Row,
  IN UINTN    Index,
  IN UINT32   Rgb,
  IN BOOLEAN  Striped
  )
{
  CONST struct boot_args  *BootArgs;
  UINT64                  Base;
  UINT64                  Stride;
  UINT64                  Width;
  UINT64                  Height;
  UINT32                  Depth;
  UINT32                  Pixel;
  UINT32                  White;
  UINTN                   XStart;
  UINTN                   YStart;
  UINTN                   X;
  UINTN                   Y;
  volatile UINT32         *Line;

  if ((Row >= ROW_COUNT) || (Index >= PANEL_CHECKPOINT_BLOCKS_PER_ROW)) {
    return;
  }

  BootArgs = (CONST struct boot_args *)(UINTN)FixedPcdGet64 (PcdBootArgsPointer);
  Base     = BootArgs->video.base;
  Stride   = BootArgs->video.stride;
  Width    = BootArgs->video.width;
  Height   = BootArgs->video.height;
  Depth    = (UINT32)(BootArgs->video.depth & BOOT_ARGS_DEPTH_MASK);

  if ((Base == 0) || ((Depth != 30) && (Depth != 32)) ||
      (Width < MARGIN_LEFT + (PANEL_CHECKPOINT_BLOCKS_PER_ROW * BLOCK_PITCH_X)) ||
      (Height < MARGIN_BOTTOM + (ROW_COUNT * BLOCK_PITCH_Y) + BLOCK_PITCH_Y) ||
      (Stride < Width * sizeof (UINT32)))
  {
    return;
  }

  XStart = MARGIN_LEFT + (Index * BLOCK_PITCH_X);
  YStart = (UINTN)Height - MARGIN_BOTTOM - ((ROW_COUNT - Row) * BLOCK_PITCH_Y);
  Pixel  = EncodePixel (Rgb, Depth);
  White  = EncodePixel (0x00FFFFFF, Depth);

  for (Y = YStart; Y < YStart + BLOCK_HEIGHT; Y++) {
    Line = (volatile UINT32 *)(UINTN)(Base + (Y * Stride));
    for (X = XStart; X < XStart + BLOCK_WIDTH; X++) {
      if (Striped && ((((Y - YStart) / STRIPE_HEIGHT) & 1) != 0)) {
        Line[X] = White;
      } else {
        Line[X] = Pixel;
      }
    }
  }

  //
  // The framebuffer may be mapped write-combining or not at all cached; make
  // the stores visible before the caller can stop for good.
  //
  __asm__ __volatile__ ("dsb sy" ::: "memory");
}

UINT32
EFIAPI
PanelCheckpointColor (
  IN UINTN  Index
  )
{
  STATIC CONST UINT32  Palette[PANEL_CHECKPOINT_BLOCKS_PER_ROW] = {
    0x00FFFFFF, // white
    0x0000FFFF, // cyan
    0x0000FF00, // green
    0x00FFFF00, // yellow
    0x00FF8000, // orange
    0x00FF00FF, // magenta
    0x00FF0000, // red
    0x004080FF  // blue
  };

  return Palette[Index % PANEL_CHECKPOINT_BLOCKS_PER_ROW];
}

VOID
EFIAPI
PanelCheckpointDraw (
  IN UINTN   Row,
  IN UINTN   Index,
  IN UINT32  Rgb
  )
{
  if (Row >= PANEL_CHECKPOINT_ROW_ERROR) {
    return;
  }

  FillBlock (Row, Index, Rgb, FALSE);
}

VOID
EFIAPI
PanelCheckpointError (
  IN UINTN  Code
  )
{
  FillBlock (PANEL_CHECKPOINT_ROW_ERROR, Code, 0x00FF0000, TRUE);
}

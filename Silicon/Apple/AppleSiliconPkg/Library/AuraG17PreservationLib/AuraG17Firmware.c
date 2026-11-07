/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Library/AuraG17PreservationLib.h>
#include <Library/AppleDTLib.h>
#include <Library/PcdLib.h>

static const void *Property(void *Context, const char *Path, const char *Name, size_t *Length)
{
  dt_node_t *Root = Context != NULL ? Context : (dt_node_t *)(UINTN)FixedPcdGet64(PcdAdtPointer);
  if (Path[0] == '/' && Path[1] == '\0')
    return dt_node_prop (Root, Name, Length);
  return dt_node_prop (dt_find (Root, Path), Name, Length);
}
CONST VOID *AuraG17Record(UINTN *Length)
{
  size_t Size = 0;
  CONST VOID *Record = dt_get_prop("/chosen", AURA_G17_PROPERTY, &Size);
  *Length = Size;
  return Record;
}
BOOLEAN AuraG17Observe(struct aura_g17_observation *Actual)
{
  CONST struct boot_args *Args = (CONST struct boot_args *)(UINTN)PcdGet64(PcdBootArgsPointer);
  UINT64 DramSize;
  if (Args == NULL) return FALSE;
  switch (Args->revision) {
    case 1: DramSize = Args->rv1.mem_size_actual; break;
    case 2: DramSize = Args->rv2.mem_size_actual; break;
    case 3: DramSize = Args->rv3.mem_size_actual; break;
    default: return FALSE;
  }
  if (Args->phys_base != PcdGet64(PcdSystemMemoryBase) ||
      Args->mem_size != PcdGet64(PcdSystemMemorySize)) return FALSE;
  return (BOOLEAN)aura_g17_observe(Property, NULL, Args->phys_base,
                                  Args->mem_size, DramSize, Actual);
}

BOOLEAN AuraG17EarlyCheck(CONST VOID *BootArgs, CONST VOID *AdtSource,
                          UINT64 BootDestination, UINT64 AdtDestination)
{
  CONST struct boot_args *Args = BootArgs;
  struct aura_g17_observation Actual;
  struct aura_g17_range BootCopy, AdtCopy, Fd, Framebuffer;
  CONST VOID *Record;
  size_t Length = 0, Checked = 0;
  UINT64 DramSize;
  if (Args == NULL || AdtSource == NULL || Args->devtree_size == 0 ||
      Args->devtree_size > 0x400000 ||
      !aura_g17_adt_bounds (AdtSource, Args->devtree_size, NULL) ||
      dt_check ((VOID *)AdtSource, Args->devtree_size, &Checked) != 0 ||
      Checked > Args->devtree_size) return FALSE;
  Record = Property ((VOID *)AdtSource, "/chosen", AURA_G17_PROPERTY, &Length);
  if (Record == NULL) return TRUE;
  switch (Args->revision) {
    case 1: DramSize = Args->rv1.mem_size_actual; break;
    case 2: DramSize = Args->rv2.mem_size_actual; break;
    case 3: DramSize = Args->rv3.mem_size_actual; break;
    default: return FALSE;
  }
  if (!aura_g17_observe (Property, (VOID *)AdtSource, Args->phys_base,
                        Args->mem_size, DramSize, &Actual)) return FALSE;
  BootCopy.base = BootDestination; BootCopy.size = sizeof (*Args);
  AdtCopy.base = AdtDestination; AdtCopy.size = Args->devtree_size;
  Fd.base = PcdGet64 (PcdFdBaseAddress); Fd.size = FixedPcdGet32 (PcdFdSize);
  Framebuffer.base = Args->video.base; Framebuffer.size = 0;
  if (Framebuffer.base != 0) {
    if (Args->video.height == 0 || Args->video.stride == 0 ||
        Args->video.stride > MAX_UINT64 / Args->video.height) return FALSE;
    Framebuffer.size = Args->video.stride * Args->video.height;
  }
  return (BOOLEAN)aura_g17_early (Record, Length, &Actual, &BootCopy, &AdtCopy,
                                  &Fd, FixedPcdGet32 (PcdSystemMemoryUefiRegionSize), &Framebuffer);
}

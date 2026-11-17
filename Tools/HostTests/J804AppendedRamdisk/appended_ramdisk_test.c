/*
 * Host test: run Mu's own appended-ramdisk validators (AppendedRamdisk.h, the
 * code PEI and BootRamdiskHelperDxe run) on a header and image built by
 * Tools/j804-winpe-ramdisk.py, laid out exactly as run_guest.py -a lays them
 * out after the FD (header at FD base + PcdFdSize, image right behind it).
 *
 * usage: appended_ramdisk_test <hdr.bin> <WINPE.img>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Base.h>
#define NTASI_APPENDED_RAMDISK_INCLUDE_FAT_VALIDATOR 1
#include <AppendedRamdisk.h>

static long
FileSize (FILE *f)
{
  long n;
  fseek (f, 0, SEEK_END);
  n = ftell (f);
  fseek (f, 0, SEEK_SET);
  return n;
}

int
main (int argc, char **argv)
{
  FILE        *h, *i;
  long        hs, is;
  UINT8       *span;
  CONST VOID  *image;
  UINT64      image_size, reservation;

  if (argc != 3) {
    fprintf (stderr, "usage: %s hdr.bin WINPE.img\n", argv[0]);
    return 2;
  }
  h = fopen (argv[1], "rb");
  i = fopen (argv[2], "rb");
  if (!h || !i) {
    perror ("open");
    return 2;
  }
  hs = FileSize (h);
  is = FileSize (i);
  if (hs != NTASI_APPENDED_RAMDISK_HEADER_SIZE) {
    fprintf (stderr, "FAIL: header file is %ld bytes\n", hs);
    return 1;
  }
  /* Page-aligned, page-rounded span, as the guest RAM after the FD. */
  span = aligned_alloc (EFI_PAGE_SIZE, ALIGN_VALUE ((size_t)(hs + is), EFI_PAGE_SIZE));
  if (!span || fread (span, 1, hs, h) != (size_t)hs || fread (span + hs, 1, is, i) != (size_t)is) {
    fprintf (stderr, "read failed\n");
    return 2;
  }
  memset (span + hs + is, 0, ALIGN_VALUE ((size_t)(hs + is), EFI_PAGE_SIZE) - (hs + is));

  /* PEI (MemoryInitPeiLib): structural check only, against the 1 GiB + 4 KiB cap. */
  if (!NtasiValidateAppendedRamdisk ((CONST NTASI_APPENDED_RAMDISK_HEADER *)span,
                                     NTASI_APPENDED_RAMDISK_MAX_MAPPED_SPAN, FALSE,
                                     NULL, NULL, &reservation)) {
    printf ("FAIL: PEI header validation\n");
    return 1;
  }
  /* DXE (BootRamdiskHelperDxe): payload CRC within the PEI reservation, then FAT/GPT. */
  if (!NtasiValidateAppendedRamdisk ((CONST NTASI_APPENDED_RAMDISK_HEADER *)span,
                                     reservation, TRUE, &image, &image_size, NULL)) {
    printf ("FAIL: DXE header/payload validation\n");
    return 1;
  }
  if (!NtasiValidateFatBootSector (image, image_size)) {
    printf ("FAIL: FAT/GPT validation\n");
    return 1;
  }
  printf ("PASS: reservation 0x%llx, image 0x%llx bytes at header + 0x%lx\n",
          (unsigned long long)reservation, (unsigned long long)image_size,
          (long)((const UINT8 *)image - span));
  return 0;
}

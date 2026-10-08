/* SPDX-License-Identifier: MIT */
#ifndef J714_MTP_KEYBOARD_H
#define J714_MTP_KEYBOARD_H
#include <Uefi.h>
// Owned by the menu application. Stop must succeed before unloading it or
// handing MTP to Windows. No trackpad firmware is needed for this keyboard.
EFI_STATUS J714MtpKeyboardStart (VOID);
EFI_STATUS J714MtpKeyboardStop (VOID);
#endif

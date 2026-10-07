/* SPDX-License-Identifier: MIT */
/* Native FIQ needs DAIF.F to follow DAIF.I, as Linux does with DAIFSet/Clr #3.
 * Stock Windows never unmasks F: exception entry masks it and the handlers
 * (KiSynchronousException, KiAbortException, ...) clear only I, so timer and
 * fast-IPI FIQs stay blocked for whole page faults/syscalls. Exact 26100
 * kernel (GUID 9B3C83B580575E719D5592619E29C053 age 1): every DAIFSet/Clr #2
 * in executable sections, plus the seven computed DAIF masks that clear I and three user-context
 * SPSR sanitizers that otherwise force F=1 with I=0. The five IRQ/debug
 * DAIF #0xa operations also pair F: critically, exception return must keep
 * F masked from restoring SPSR/ELR until ERET, just like I. User masks already
 * exclude I; clear only F while preserving their remaining allowed bits. */
#ifndef WINDOWS_NATIVE_DAIF_PAIR_H
#define WINDOWS_NATIVE_DAIF_PAIR_H
#define WIN_DAIF_SET2 0xd50342dfu
#define WIN_DAIF_CLR2 0xd50342ffu
#define WIN_DAIF_SET3 0xd50343dfu
#define WIN_DAIF_CLR3 0xd50343ffu
#define WIN_DAIF_SET2_COUNT 369u
#define WIN_DAIF_CLR2_COUNT 362u
struct win_daif_edit { unsigned rva, size; unsigned long long old, new_; };
static const struct win_daif_edit win_daif_edits[] = {
    {0x605930, 4, 0xd5034adf, 0xd5034bdf}, /* KiRestoreFromTrapFrame: protect SPSR/ELR restore through ERET */
    {0x60d17c, 4, 0xd5034aff, 0xd5034bff}, /* KiServiceInternal: unmask paired F/I and debug */
    {0x60d28c, 4, 0xd5034aff, 0xd5034bff}, /* KiSystemServiceException */
    {0x60d2a4, 4, 0xd5034adf, 0xd5034bdf}, /* KiSystemServiceExit: protect user return */
    {0x60d488, 4, 0xd5034aff, 0xd5034bff}, /* KiSystemServiceExit: paired release */
    {0x26b298, 4, 0x321a0108, 0x12197908}, /* KeContextToKframes user SPSR: clear F instead of forcing it */
    {0x28a20c, 4, 0x321a0108, 0x12197908}, /* KiContinuePreviousModeUser SPSR: clear F instead of forcing it */
    {0x8d84d0, 4, 0x321a0108, 0x12197908}, /* PspSetContext user SPSR: clear F instead of forcing it */
    {0x2b2520, 4, 0x92805008, 0x92805808}, /* KiReturnToUserModeContextUpdates: movn x8,#0x280 -> #0x2c0 */
    {0x2b2550, 4, 0xd2805008, 0xd2805808}, /* KiReturnToUserModeContextUpdates: movz x8,#0x280 -> #0x2c0 */
    {0x2b2898, 8, 0xfffffd7f, 0xfffffd3f}, /* KiAbortException literal: F follows the trap frame like I */
    {0x2b2974, 4, 0x92805008, 0x92805808}, /* KiSynchronousException */
    {0x426400, 4, 0x92805008, 0x92805808}, /* VslSmcException */
    {0x4e22c4, 4, 0x92805008, 0x92805808}, /* KiSecureException */
    {0x4e24e8, 4, 0x92805008, 0x92805808}, /* KiVirtualizationFaultException */
};
#endif

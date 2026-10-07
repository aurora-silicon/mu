/* SPDX-License-Identifier: MIT */
/* Explicit J714 right-port direct USB3 backend. Host x1n1 must have reserved
 * USB2, granted the 20 alias windows, mapped private guest DMA with native
 * SPTM DART, and prepared the PHY. No host/USB0 DART access from Mu. */
#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/AppleDTLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Timer.h>
#include "apple_t6050_usb3_pipe.h"
#include "apple_t6050_usb2_reset.h"

STATIC UINT32 mPort = 2;
#define DRD  (0x88000000U + mPort * 0x1000000U)
#define PIPE (DRD + 0x24000U)
#define CORE (DRD + 0x100000U)
#define USB2 (DRD + 0x140000U)
#define BIST (CORE + 0x29000)

STATIC UINTN BankBase(unsigned int bank)
{
  switch (bank) { case 0: return USB2; case 35: return BIST; case 44: return PIPE; }
  ASSERT(FALSE);
  return 0;
}
STATIC unsigned int Read(void *ctx, unsigned int bank, unsigned int off)
{ return MmioRead32(BankBase(bank) + off); }
STATIC void Write(void *ctx, unsigned int bank, unsigned int off, unsigned int v)
{ MmioWrite32(BankBase(bank) + off, v); }
STATIC int Poll(void *ctx, unsigned int bank, unsigned int off,
                unsigned int mask, unsigned int v, unsigned int us)
{
  UINTN reg = BankBase(bank) + off;
  for (unsigned int i = 0; i <= us; i++) {
    if ((MmioRead32(reg) & mask) == v) return 0;
    if (i != us) MicroSecondDelay(1);
  }
  DEBUG((DEBUG_ERROR, "J714_USB3_POLL_FAIL bank=%u off=%x value=%x mask=%x expected=%x\n",
         bank, off, MmioRead32(reg), mask, v));
  return -1;
}
STATIC void Delay(void *ctx, unsigned int us) { MicroSecondDelay(us); }
STATIC void SleepMs(void *ctx, unsigned int ms) { MicroSecondDelay(ms * 1000); }
STATIC CONST struct apple_t6050_usb_io Io = {
  .read = Read, .write = Write, .poll = Poll, .delay = Delay,
};

struct BoardTable { CONST CHAR8 *name; UINTN base; UINT32 size; };
STATIC CONST struct BoardTable Tables[] = {
  {"tunable_BULK_FABRIC_DEFAULT", 0x30000, 0x4000},
  {"tunable_DRD_USB31_GBL_DEFAULT", 0xc100, 0x11800 - 0xc100},
  {"tunable_DRD_USB31_GBL_HOST", 0xc100, 0x11800 - 0xc100},
  {"tunable_DRD_CIO_REGS_DEFAULT", 0xcd20, 0x11800 - 0xcd20},
  {"tunable_DRD_USB31_DBG_DEFAULT", 0xd800, 0x11800 - 0xd800},
  {"tunable_DRD_USB31_CFG_HOST", 0x20000, 0x4000},
  {"tunable_LINK_REGS_DEFAULT", 0xd000, 0x4000},
  {"tunable_AUSBC_DEBUG_DEFAULT", 0x34000, 0x4000},
  {"tunable_PIPE_HANDLER_DEFAULT", 0x24000, 0x4000},
};
struct Tunable { UINT32 tag, mask, value; };
STATIC EFI_STATUS BoardTunables(BOOLEAN apply, BOOLEAN pipeOnly)
{
  dt_node_t *node = dt_get(mPort == 1 ? "usb-drd1" : "usb-drd2");
  if (node == NULL) return EFI_NOT_FOUND;
  for (UINTN i = 0; i < ARRAY_SIZE(Tables); i++) {
    UINTN bytes = 0;
    struct Tunable *t = dt_node_prop(node, Tables[i].name, &bytes);
    if (t == NULL || !bytes || bytes % sizeof(*t)) return EFI_COMPROMISED_DATA;
    for (UINTN j = 0; j < bytes / sizeof(*t); j++) {
      UINT32 off = t[j].tag & 0xffffff;
      if ((t[j].tag >> 24) != 32 || (off & 3) ||
          off > Tables[i].size - 4 || (t[j].value & ~t[j].mask))
        return EFI_COMPROMISED_DATA;
      if (apply && ((i == ARRAY_SIZE(Tables) - 1) == pipeOnly))
        MmioAndThenOr32(DRD + Tables[i].base + off, ~t[j].mask, t[j].value);
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS InitializeCore(VOID)
{
  UINT32 id = MmioRead32(DRD + 0xc120);
  if ((id & 0xffff0000) != 0x33310000) return EFI_UNSUPPORTED;
  MmioAnd32(DRD + 0xc200, ~BIT6);
  MmioAnd32(DRD + 0xc2c0, ~BIT17);
  /* DWC3's device-side reset while the native PIPE producer is parked. */
  MmioAndThenOr32(DRD + 0xc704, ~(BIT31 | (0xfU << 5)), BIT30);
  UINT32 tries;
  for (tries = 0; tries < 20000; tries++) {
    if (!(MmioRead32(DRD + 0xc704) & BIT30)) break;
    MicroSecondDelay(10);
  }
  if (tries == 20000) return EFI_TIMEOUT;
  UINT32 ctl = MmioRead32(DRD + 0xc110);
  ctl &= ~((3U << 4) | BIT3);
  if (((MmioRead32(DRD + 0xc144) >> 24) & 3) == 1) ctl &= ~BIT0;
  if ((id & 0xffff) < 0x190a) ctl |= BIT16;
  MmioWrite32(DRD + 0xc110, ctl);
  EFI_STATUS s = BoardTunables(TRUE, FALSE);
  if (EFI_ERROR(s)) return s;
  MmioAndThenOr32(DRD + 0xc110, 0x7ffff, 13U << 19);
  MmioAnd32(DRD + 0xc680, ~BIT13);
  MmioAndThenOr32(DRD + 0xc110, ~(3U << 12), 1U << 12);
  MmioOr32(DRD + 0xc630, BIT7 | 0x20);
  MmioAndThenOr32(DRD + 0xc100, ~0xffU, 0xf);
  MmioOr32(DRD + 0xc104, 0xf00);
  MmioOr32(DRD + 0xc200, BIT6);
  MmioOr32(DRD + 0xc2c0, BIT17);
  s = BoardTunables(TRUE, TRUE);
  if (EFI_ERROR(s)) return s;
  if (apple_t6050_usb3_pipe_switch(&Io)) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}
/* Native timer delivery belongs to Mu only until an EFI application starts.
 * The stage restores FMO before bootmgr/winload, whose vectors are not ready
 * for raw FIQ. NT still uses the separately fingerprinted FIQ/HAL handoff. */
STATIC EFI_IMAGE_START mStartImage;
STATIC EFI_TIMER_ARCH_PROTOCOL *mTimer;
STATIC UINT64 mTimerPeriod;
STATIC BOOLEAN mTimerPhaseActive;

STATIC UINT64 TimerPhaseHvc(BOOLEAN enable)
{
  register UINT64 X0 __asm__("x0") = 0x46514948;
  register UINT64 X1 __asm__("x1") = enable;
  __asm__ volatile("hvc #0xa520" : "+r"(X0), "+r"(X1) :: "memory");
  return X0;
}
STATIC EFI_STATUS SetFirmwareTimer(BOOLEAN enable)
{
  EFI_STATUS s;
  if (enable == mTimerPhaseActive) return EFI_SUCCESS;
  if (enable) {
    if (mTimer == NULL) {
      s = gBS->LocateProtocol(&gEfiTimerArchProtocolGuid, NULL, (VOID **)&mTimer);
      if (EFI_ERROR(s)) return s;
      s = mTimer->GetTimerPeriod(mTimer, &mTimerPeriod);
      if (EFI_ERROR(s) || !mTimerPeriod) return EFI_NOT_READY;
    }
    if (TimerPhaseHvc(TRUE)) return EFI_UNSUPPORTED;
    mTimerPhaseActive = TRUE;
    s = mTimer->SetTimerPeriod(mTimer, mTimerPeriod);
    if (EFI_ERROR(s)) {
      TimerPhaseHvc(FALSE); mTimerPhaseActive = FALSE; return s;
    }
    /* TimerDxe initializes IMASK=1. SetTimerPeriod uses EnableTimer, which
     * preserves it in the Apple counter library. Reenable explicitly clears
     * IMASK after the fresh compare value and native FIQ ownership are ready. */
    ArmGenericTimerReenableTimer();
    DEBUG((DEBUG_ERROR, "J714_MU_TIMER_NATIVE: period=%Lu ctl=%x\n",
           mTimerPeriod, (UINT32)ArmGenericTimerGetTimerCtrlReg()));
  } else {
    s = mTimer->SetTimerPeriod(mTimer, 0);
    if (EFI_ERROR(s)) return s;
    if (TimerPhaseHvc(FALSE)) return EFI_DEVICE_ERROR;
    mTimerPhaseActive = FALSE;
    DEBUG((DEBUG_ERROR, "J714_MU_TIMER_PROTECTED: application handoff\n"));
  }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI StartImage(EFI_HANDLE image, UINTN *size, CHAR16 **data)
{
  EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
  BOOLEAN restore = FALSE;
  EFI_STATUS s;
  if (mTimerPhaseActive &&
      !EFI_ERROR(gBS->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (VOID **)&loaded)) &&
      loaded->ImageCodeType == EfiLoaderCode) {
    s = SetFirmwareTimer(FALSE);
    if (EFI_ERROR(s)) return s; /* Never enter an application with FMO released. */
    restore = TRUE;
  }
  s = mStartImage(image, size, data);
  if (restore) {
    EFI_STATUS timerStatus = SetFirmwareTimer(TRUE);
    if (EFI_ERROR(timerStatus))
      DEBUG((DEBUG_ERROR, "J714_MU_TIMER_RETURN_FAILED: %r\n", timerStatus));
  }
  return s;
}
STATIC VOID UpdateBootServicesCrc(VOID)
{
  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32(gBS, gBS->Hdr.HeaderSize, &gBS->Hdr.CRC32);
}

STATIC EFI_STATUS BringupPort(VOID)
{
  EFI_HANDLE handle = NULL;
  EFI_STATUS s = BoardTunables(FALSE, FALSE);
  if (EFI_ERROR(s)) goto fail;
  /* Prove x1n1 stopped at the same cold boundary Linux uses. */
  if ((MmioRead32(PIPE + 0x1c) & 0x11) != 0x10 ||
      (MmioRead32(PIPE + 0xc) & 0x73) != 0x42 ||
      !(MmioRead32(CORE + 0x260) & BIT10) ||
      ((MmioRead32(CORE + 0x78) & 0x1ffff) != 0x110 &&
       (MmioRead32(CORE + 0x78) & 0x1ffff) != 0x111)) {
    s = EFI_NOT_READY; goto fail;
  }
  MmioAnd32(PIPE + 0x1c, ~BIT4);
  MmioOr32(PIPE + 0x1c, BIT0);
  s = InitializeCore();
  if (EFI_ERROR(s)) goto fail;
  s = SetFirmwareTimer(TRUE);
  if (EFI_ERROR(s)) goto fail;
  s = RegisterNonDiscoverableMmioDevice(mPort, NonDiscoverableDeviceTypeXhci,
        NonDiscoverableDeviceDmaTypeCoherent, NULL, &handle, 1,
        (UINTN)DRD, (UINTN)0xff00);
  if (EFI_ERROR(s)) goto fail;
  /* Establish the xHCI frontend before the late PHY reset, as in Linux. */
  s = gBS->ConnectController(handle, NULL, NULL, TRUE);
  if (EFI_ERROR(s)) goto fail;
  if (apple_t6050_usb2_port_reset(&Io, NULL, SleepMs)) {
    s = EFI_DEVICE_ERROR; goto fail;
  }
  DEBUG((DEBUG_ERROR, "J714_USB3_READY: port=%u, native PIPE, late PHY reset, xHCI connected\n", mPort));
  return EFI_SUCCESS;
fail:
  if (handle != NULL) gBS->DisconnectController(handle, NULL, NULL);
  MmioAnd32(PIPE + 0x1c, ~BIT0);
  MmioOr32(PIPE + 0x1c, BIT4);
  DEBUG((DEBUG_ERROR, "J714_USB3_FAILED: port=%u %r; controller held reset\n", mPort, s));
  return s;
}
STATIC VOID EFIAPI Bringup(EFI_EVENT event, VOID *ctx)
{
  gBS->CloseEvent(event);
  UINT32 ready = BIT2;
#if J714_USB_HOSTS
  if (MmioRead64(0x61f00000) != 0x78316e31 || MmioRead64(0x61f00008) != 1) return;
  ready = (UINT32)MmioRead64(0x61f00058) & (BIT1 | BIT2);
#endif
  BOOLEAN any = FALSE;
  for (mPort = 1; mPort <= 2; mPort++) {
    if (!(ready & (1U << mPort))) continue;
    if (!EFI_ERROR(BringupPort())) any = TRUE;
  }
  if (!any && mTimerPhaseActive) SetFirmwareTimer(FALSE);
}
EFI_STATUS EFIAPI J714Usb3Entry(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
  EFI_EVENT event;
  EFI_STATUS s = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Bringup, NULL,
                                    &gEfiEndOfDxeEventGroupGuid, &event);
  if (EFI_ERROR(s)) return s;
  mStartImage = gBS->StartImage;
  gBS->StartImage = StartImage;
  UpdateBootServicesCrc();
  return EFI_SUCCESS;
}

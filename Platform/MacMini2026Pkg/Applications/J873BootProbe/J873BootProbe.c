// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bounded on-device qualification of the firmware event and console paths.
#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC volatile UINTN mTicks;

STATIC VOID EFIAPI Tick (EFI_EVENT Event, VOID *Context)
{
  mTicks++;
}

EFI_STATUS EFIAPI UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_EVENT Event;
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINTN Keys = 0;
  UINTN Step;
  UINT64 El, Daif, Hcr = 0, Cnthctl = 0;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (El));
  __asm__ volatile ("mrs %0, DAIF" : "=r" (Daif));
  if (El == 8) {
    __asm__ volatile ("mrs %0, HCR_EL2" : "=r" (Hcr));
    __asm__ volatile ("mrs %0, CNTHCTL_EL2" : "=r" (Cnthctl));
  }
  Print (L"J873_PROBE_READY EL=%lx DAIF=%lx HCR=%lx CNTHCTL=%lx\r\n",
         El, Daif, Hcr, Cnthctl);
  Print (L"J873_PROBE_TIMER ctl=%lx cval=%lx count=%lx\r\n",
         (UINT64)ArmReadCntpCtl (), ArmReadCntpCval (), ArmReadCntPct ());
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                            Tick, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Print (L"J873_PROBE_CREATE_FAILED %r\r\n", Status);
    return Status;
  }
  Status = gBS->SetTimer (Event, TimerPeriodic, 1000000); // 100 ms
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Event);
    return Status;
  }
  // Busy delays avoid making successful idle/WFI a prerequisite for diagnosis.
  for (Step = 0; Step < 12000; Step++) {
    if (gST->ConIn != NULL &&
        !EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      Keys++;
      Print (L"J873_PROBE_KEY %04x %04x\r\n", Key.ScanCode, Key.UnicodeChar);
    }
    gBS->Stall (1000);
  }
  gBS->SetTimer (Event, TimerCancel, 0);
  gBS->CloseEvent (Event);
  Print (L"J873_PROBE_DONE ticks=%u keys=%u rx_pending=%u ctl=%lx\r\n",
         mTicks, Keys, MmioRead32 (FixedPcdGet64 (PcdAppleUartBase) + 0x2c),
         (UINT64)ArmReadCntpCtl ());
  return EFI_SUCCESS;
}

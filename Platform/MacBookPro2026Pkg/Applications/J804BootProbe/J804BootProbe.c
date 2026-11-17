// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// J804 boot probe: a bounded check of the exception level, the timer event
// path, the console and (at EL1 under the carrier) the ACPI tables and PSCI.
// Derived from MacMini2026Pkg J873BootProbe.
//
// It prints the raw CurrentEL value and the decoded level. It never touches
// a device directly: console input goes through ConIn and the serial check
// through SerialPortLib (the Null library unless the carrier offers a vUART).
// The PSCI query runs only at EL1, where the carrier serves SMC; at native
// EL2 an SMC has no handler.
//
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Acpi64.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Library/ArmLib.h>
#include <Library/ArmMonitorLib.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <J804Windows.h>

STATIC volatile UINTN  mTicks;

STATIC VOID EFIAPI
Tick (
  EFI_EVENT  Event,
  VOID       *Context
  )
{
  mTicks++;
}

STATIC CONST EFI_ACPI_DESCRIPTION_HEADER *
FindAcpiTable (
  UINT32  Signature
  )
{
  EFI_ACPI_6_4_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  UINT64                                        *Entry;
  UINTN                                         Count;
  UINTN                                         Index;

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) ||
      (Rsdp == NULL) || (Rsdp->XsdtAddress == 0))
  {
    return NULL;
  }

  Xsdt  = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  Count = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (UINT64);
  Entry = (UINT64 *)(Xsdt + 1);
  for (Index = 0; Index < Count; Index++) {
    CONST EFI_ACPI_DESCRIPTION_HEADER  *Table;

    Table = (CONST EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 (&Entry[Index]);
    if ((Table != NULL) && (Table->Signature == Signature)) {
      return Table;
    }
  }

  return NULL;
}

STATIC VOID
ProbeAcpi (
  VOID
  )
{
  CONST EFI_ACPI_DESCRIPTION_HEADER                   *Madt;
  CONST EFI_ACPI_DESCRIPTION_HEADER                   *Gtdt;
  CONST EFI_ACPI_DESCRIPTION_HEADER                   *Fadt;
  CONST UINT8                                         *Cursor;
  CONST UINT8                                         *End;
  UINTN                                               Gicc;
  UINT64                                              FirstMpidr;
  UINT64                                              GicdBase;

  Madt = FindAcpiTable (EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE);
  Gtdt = FindAcpiTable (EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE);
  Fadt = FindAcpiTable (EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE);
  if (Madt == NULL) {
    Print (L"J804_PROBE_ACPI none\r\n");
    return;
  }

  Gicc       = 0;
  FirstMpidr = MAX_UINT64;
  GicdBase   = 0;
  Cursor     = (CONST UINT8 *)Madt + sizeof (EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
  End        = (CONST UINT8 *)Madt + Madt->Length;
  while ((Cursor + 2 <= End) && (Cursor[1] >= 2) && (Cursor + Cursor[1] <= End)) {
    if (Cursor[0] == EFI_ACPI_6_4_GIC) {
      CONST EFI_ACPI_6_4_GIC_STRUCTURE  *Cpu = (CONST EFI_ACPI_6_4_GIC_STRUCTURE *)Cursor;

      if (Gicc == 0) {
        FirstMpidr = ReadUnaligned64 (&Cpu->MPIDR);
      }

      Gicc++;
    } else if (Cursor[0] == EFI_ACPI_6_4_GICD) {
      GicdBase = ReadUnaligned64 (&((CONST EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE *)Cursor)->PhysicalBaseAddress);
    }

    Cursor += Cursor[1];
  }

  Print (
    L"J804_PROBE_ACPI madt_gicc=%u first_mpidr=%lx gicd=%lx gtdt=%u/%u fadt_profile=%u\r\n",
    Gicc,
    FirstMpidr,
    GicdBase,
    (Gtdt != NULL) ? ((CONST EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE *)Gtdt)->NonSecurePL1TimerGSIV : 0,
    (Gtdt != NULL) ? ((CONST EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE *)Gtdt)->VirtualTimerGSIV : 0,
    (Fadt != NULL) ? ((CONST EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE *)Fadt)->PreferredPmProfile : 0
    );
}

EFI_STATUS EFIAPI
UefiMain (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT         Event;
  EFI_STATUS        Status;
  EFI_INPUT_KEY     Key;
  ARM_MONITOR_ARGS  Psci;
  UINTN             Keys = 0;
  UINTN             Step;
  UINT64            CurrentEl, El, Daif, Mpidr, Hcr = 0, Cnthctl = 0;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  __asm__ volatile ("mrs %0, DAIF" : "=r" (Daif));
  El    = (CurrentEl >> 2) & 3;
  Mpidr = ArmReadMpidr ();
  if (El == 2) {
    __asm__ volatile ("mrs %0, HCR_EL2" : "=r" (Hcr));
    __asm__ volatile ("mrs %0, CNTHCTL_EL2" : "=r" (Cnthctl));
  }

  Print (
    L"J804_PROBE_READY CurrentEL=%lx EL%u MPIDR=%lx boot_cpu=%a DAIF=%lx HCR=%lx CNTHCTL=%lx\r\n",
    CurrentEl,
    (UINT32)El,
    Mpidr,
    ((Mpidr & 0xff00ffffffULL) == J804_BOOT_CPU_MPIDR) ? "cpu6" : "OTHER",
    Daif,
    Hcr,
    Cnthctl
    );
  Print (
    L"J804_PROBE_TIMER ctl=%lx cval=%lx count=%lx\r\n",
    (UINT64)ArmReadCntpCtl (),
    ArmReadCntpCval (),
    ArmReadCntPct ()
    );

  if (El == 1) {
    Psci.Arg0 = ARM_SMC_ID_PSCI_VERSION;
    ArmMonitorCall (&Psci);
    Print (L"J804_PROBE_PSCI version=%lx\r\n", (UINT64)Psci.Arg0);
  } else {
    Print (L"J804_PROBE_PSCI skipped (not EL1, no carrier)\r\n");
  }

  ProbeAcpi ();

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Tick, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Print (L"J804_PROBE_CREATE_FAILED %r\r\n", Status);
    return Status;
  }

  Status = gBS->SetTimer (Event, TimerPeriodic, 1000000); // 100 ms
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Event);
    return Status;
  }

  // Busy delays avoid making successful idle/WFI a prerequisite for diagnosis.
  for (Step = 0; Step < 12000; Step++) {
    if ((gST->ConIn != NULL) &&
        !EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key)))
    {
      Keys++;
      Print (L"J804_PROBE_KEY %04x %04x\r\n", Key.ScanCode, Key.UnicodeChar);
    }

    gBS->Stall (1000);
  }

  gBS->SetTimer (Event, TimerCancel, 0);
  gBS->CloseEvent (Event);
  Print (
    L"J804_PROBE_DONE ticks=%u keys=%u serial_rx=%u ctl=%lx\r\n",
    mTicks,
    Keys,
    (UINT32)SerialPortPoll (),
    (UINT64)ArmReadCntpCtl ()
    );
  return EFI_SUCCESS;
}

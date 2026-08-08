/**
 * Copyright (c) 2024, amarioguy (AppleWOA authors).
 * 
 * Module Name:
 *     AppleUsbTypeCBringupDxe.c
 * 
 * Abstract:
 *     Platform specific driver for Apple silicon platforms to bring up the USB-C ports.
 * 
 * Environment:
 *     UEFI DXE (Driver Execution Environment).
 * 
 * License:
 *     SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT) AND GPL-2.0
 * 
 *     Original code basis is from the Asahi Linux u-boot project, original copyright and author notices below.
 *     Copyright (C) 2022 Mark Kettenis <kettenis@openbsd.org>
 *     Copyright (C) The Asahi Linux Contributors.
 *     
 *     Parts of DWC3 bringup code brought in from edk2-platforms, original copyright notice below.
 *     Copyright 2017, 2020 NXP
*/

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/ArmLib.h>
#include <Library/PrintLib.h>
#include <Library/PcdLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/TimerLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/AppleDTLib.h>

#include <Drivers/AppleUsbTypeCBringupDxe.h>

//
// This driver is just a stub to bringup register the DWC3 controller(s) as a non-discoverable XHCI controller(s), which
// the built-in XHCI DXE driver should be able to bring up more or less normally.
// Note that the PHY will be doing USB 2.0 speeds, because iBoot only brings up the PHY to that state,
// and bringing up the PHY to USB 3.0 speed is not only unnecessary for Windows installation but requires much
// more complicated tunables, and a number of them from fuses.
//
// As for actual bringup of the DWC3 controllers, the device registration sequence should be almost exactly the same as that of the sequence used on NXP's UsbHcd driver,
// Only difference here is that we do more of those bringups. Note that the Synopsys bringup will be based on the sequence done in 
// u-boot's DWC3 driver to ensure Apple platform compatibility. (which the NXP bringup sequence is based off of.)
//

STATIC VOID Dwc3XhciSetBeatBurstLength(IN DWC3_CONTROLLER *Controller) {
  MmioAndThenOr32 ((UINTN)&Controller->GSBusCfg0, ~USB3_ENABLE_BEAT_BURST_MASK,
    USB3_ENABLE_BEAT_BURST);

  MmioOr32 ((UINTN)&Controller->GSBusCfg1, USB3_SET_BEAT_BURST_LIMIT);
}

STATIC VOID Dwc3SetFladj(IN DWC3_CONTROLLER *Controller, IN UINT32 Value) {
  MmioOr32 ((UINTN)&Controller->GFLAdj, GFLADJ_30MHZ_REG_SEL | GFLADJ_30MHZ (Value));
}

STATIC VOID Dwc3SetMode(IN DWC3_CONTROLLER *Controller, IN UINT32 Mode) {
  MmioAndThenOr32 ((UINTN)&Controller->GCtl, ~(DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_OTG)), DWC3_GCTL_PRTCAPDIR (Mode));
}


STATIC VOID Dwc3ControllerSoftReset(IN DWC3_CONTROLLER *Controller) {
  //
  // put the core in reset first.
  //
  MmioOr32((UINTN)&Controller->GCtl, DWC3_GCTL_CORESOFTRESET);

  //
  // Assert USB 2 and USB 3 PHY reset here.
  // There doesn't seem to be Apple-specific carveouts for USB2 or USB3 PHY reset only in u-boot so reset both as per canonical
  // DWC3 u-boot/NXP EDK2 implementation.
  //
  MmioOr32((UINTN)&Controller->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_PHYSOFTRST);

  MmioOr32((UINTN)&Controller->GUsb2PhyCfg, DWC3_GUSB2PHYCFG_PHYSOFTRST);

  MemoryFence();

  MicroSecondDelay(100 * 1000);

  //
  // Clear USB 2 and USB 3 PHY reset.
  // Note that this doesn't actually bring up the USB 3 PHY, that's separate ATC setup which we're not doing here for USB 3.
  //

  MmioAnd32((UINTN)&Controller->GUsb3PipeCtl[0], ~DWC3_GUSB3PIPECTL_PHYSOFTRST);

  MmioAnd32 ((UINTN)&Controller->GUsb2PhyCfg, ~DWC3_GUSB2PHYCFG_PHYSOFTRST);

  MemoryFence();

  MicroSecondDelay(100 * 1000);

  //
  // PHYs are stable, take core out of reset.
  //

  MmioAnd32 ((UINTN)&Controller->GCtl, ~DWC3_GCTL_CORESOFTRESET);

}

STATIC EFI_STATUS Dwc3XhciCoreInit(IN DWC3_CONTROLLER *Controller)
{
  UINT32 Dwc3Revision;
  UINT32 Dwc3RegVal;
  UINTN Dwc3HwParams1Reg;
  Dwc3Revision = MmioRead32((UINTN)&Controller->GSnpsId);

  if((Dwc3Revision & DWC3_GSNPSID_MASK) != DWC3_SYNOPSYS_ALT_ID) {
    DEBUG((DEBUG_ERROR, "Dwc3XhciCoreInit: Revision 0x%x Not a Synopsys DWC3 core, aborting\n", Dwc3Revision));
    return EFI_NOT_FOUND;
  }

  //
  // soft reset the DWC3 here.
  //
  Dwc3ControllerSoftReset(Controller);

  Dwc3HwParams1Reg = MmioRead32((UINTN)&Controller->GHwParams1);

  Dwc3RegVal = MmioRead32((UINTN)&Controller->GCtl);
  Dwc3RegVal &= ~DWC3_GCTL_SCALEDOWN_MASK;
  Dwc3RegVal &= ~DWC3_GCTL_DISSCRAMBLE;

  if(DWC3_GHWPARAMS1_EN_PWROPT(Dwc3HwParams1Reg) == DWC3_GHWPARAMS1_EN_PWROPT_CLK) {
    Dwc3RegVal &= ~DWC3_GCTL_DSBLCLKGTNG;
  } else {
    DEBUG((DEBUG_INFO,"Dwc3XhciCoreInit: Power optimization unavailable\n"));
  }
  //
  // Both U-Boot and the NXP UsbHcd driver check for DWC3 errata on revisions < 1.90a - do likewise.
  //
  if((Dwc3Revision & DWC3_RELEASE_MASK) < DWC3_RELEASE_190a) {
    Dwc3RegVal |= DWC3_GCTL_U2RSTECN;
  }
  MmioWrite32((UINTN)&Controller->GCtl, Dwc3RegVal);

  return EFI_SUCCESS;
}


//
// "Pipehandler" block -- Apple-specific glue between the DWC3 core and the ATC
// (Apple Type-C) PHY. Offsets and bit names taken from m1n1's src/usb.c, which is
// the only description of this block we have.
//
// IMPORTANT: this is NOT in the DWC3 register window. It lives at reg index 3 of
// the usb-drdN ADT node (m1n1's usb_drd_get_regs() fetches index 0 and index 3),
// which is why this driver could not touch it before -- it only ever read index 0.
//
#define PIPEHANDLER_MUX_CTRL                     0x0C
#define PIPEHANDLER_MUX_CTRL_USB3                0x08
#define PIPEHANDLER_MUX_CTRL_DUMMY               0x22

#define PIPEHANDLER_AON_GEN                      0x1C
#define PIPEHANDLER_AON_GEN_DWC3_RESET_N         BIT0
#define PIPEHANDLER_AON_GEN_DWC3_FORCE_CLAMP_EN  BIT4

#define PIPEHANDLER_NONSELECTED_OVERRIDE         0x20
#define PIPEHANDLER_NATIVE_RESET                 BIT12
#define PIPEHANDLER_DUMMY_PHY_EN                 BIT15
#define PIPEHANDLER_NATIVE_POWER_DOWN            0xF

//
// Set to 0 to go back to leaving the PIPE PHY parked exactly as m1n1 left it.
//
// PROVISIONAL -- this is a fix for a hang whose mechanism is inferred, not proven.
// Confirm with tools/usb_portsc_probe.py (run from the plain proxy shell) before
// trusting it, and set this to 0 if it makes things worse.
//
//
// Set to 0 after hardware testing on J704 (build J704-FW-1).
//
// Un-parking makes the xHCI unusable. The write itself partly fails: the readback
// after storing PIPEHANDLER_MUX_CTRL_USB3 (0x08) was
//
//     Dwc3UnparkPipePhy: exit  MUX_CTRL=0x0  NONSELECTED_OVERRIDE=0x330  AON_GEN=0x1
//
// MUX_MODE (bits 1:0) took the USB3_PHY value of 0, but CLK_SELECT (bits 5:3)
// stayed 0 instead of latching USB3_PHY = 1. CLK_SELECT 0 is E_PIPEHANDLER_
// CLK_SELECT.UNK0 -- not a real clock source. XhciDxe then never gets its
// controller out of reset:
//
//     XhcResetHC!
//     ASSERT [XhciDxe] Xhci.c(2097): !(USBSTS & CNR)
//
// i.e. Controller Not Ready stayed set, and the DEBUG-build ASSERT dead-loops the
// machine before BDS can boot anything.
//
// CLK_SELECT will not latch USB3_PHY because nothing has brought the ATC PHY up.
// m1n1 only does the minimal ATC writes its USB2 device-mode gadget needs
// (src/usb.c:143-147) and then deliberately parks the mux on the dummy PHY
// (src/usb.c:149-151). There is no USB3 pipe clock to select.
//
// Leaving the mux parked keeps CLK_SELECT = DUMMY_PHY (4), which is a working
// clock -- m1n1's own gadget runs on it. USB3 will not link, but the xHCI's USB2
// port is UTMI and does not use the pipe at all, so a USB2 keyboard and USB2 boot
// media should still enumerate. That is all the bring-up actually needs.
//
// Set back to 1 only alongside real ATC PHY USB3 bring-up.
//
#define APPLE_DWC3_UNPARK_PIPE_PHY  0

/**
  Take the USB3 PIPE PHY out of the parked state m1n1 leaves it in.

  m1n1's usb_phy_bringup() runs for *every* port and parks the PIPE PHY:

      PIPEHANDLER_MUX_CTRL             = 0x22   (MUX_CTRL_DUMMY)
      PIPEHANDLER_NONSELECTED_OVERRIDE = 0x9332
          bit 15 DUMMY_PHY_EN        = 1        dummy PHY feeds the core
          bit 12 NATIVE_RESET        = 1        real ATC PHY held in reset
          bits 3:0 NATIVE_POWER_DOWN = 2        real PHY partly powered down

  That is correct for m1n1's own USB2 device-mode gadget: the dummy PHY gives the
  DWC3 core a pipe clock so it does not stall, and the real PHY stays off.

  It is wrong for us. XhciDxe reports "max speed 3" and a Usb3SupOffset, so it sees
  a SuperSpeed port, and a SuperSpeed port's PORTSC register sits in a domain
  clocked by the PIPE PHY. Reading it with that PHY held in reset and powered down
  appears to stall forever -- which matches the observed hang exactly: capability
  registers, DCBAA and the command/event rings are all in the always-on core domain
  and worked fine, then the first PORTSC read never returned. See
  docs/NEXT-STEPS.md, "Where the USB hang actually is".

  Must run before Dwc3XhciCoreInit(), mirroring m1n1's ordering (pipehandler first,
  then DWC3 core).

  @param PipehandlerBase  Base of the pipehandler block, i.e. usb-drdN reg index 3.
**/
#if APPLE_DWC3_UNPARK_PIPE_PHY
STATIC
VOID
Dwc3UnparkPipePhy (
  IN UINT64  PipehandlerBase
  )
{
  UINT32  Mux;
  UINT32  Override;
  UINT32  NewOverride;

  Mux      = MmioRead32 (PipehandlerBase + PIPEHANDLER_MUX_CTRL);
  Override = MmioRead32 (PipehandlerBase + PIPEHANDLER_NONSELECTED_OVERRIDE);

  DEBUG ((
    DEBUG_INFO,
    "Dwc3UnparkPipePhy: entry MUX_CTRL=0x%x NONSELECTED_OVERRIDE=0x%x (DUMMY_PHY_EN=%d NATIVE_RESET=%d NATIVE_POWER_DOWN=0x%x)\n",
    Mux,
    Override,
    (Override & PIPEHANDLER_DUMMY_PHY_EN) ? 1 : 0,
    (Override & PIPEHANDLER_NATIVE_RESET) ? 1 : 0,
    Override & PIPEHANDLER_NATIVE_POWER_DOWN
    ));

  //
  // Release the real PHY: out of reset, fully powered, and stop presenting the
  // dummy PHY to the core.
  //
  NewOverride = Override & ~(PIPEHANDLER_NATIVE_RESET |
                             PIPEHANDLER_DUMMY_PHY_EN |
                             PIPEHANDLER_NATIVE_POWER_DOWN);
  MmioWrite32 (PipehandlerBase + PIPEHANDLER_NONSELECTED_OVERRIDE, NewOverride);

  //
  // Point the mux at the real USB3 PHY rather than the dummy.
  //
  // MUX_CTRL is two subfields, not one value -- see m1n1
  // proxyclient/m1n1/hw/dwc3.py:247 R_PIPEHANDLER_MUX_CTRL:
  //
  //     MUX_MODE   = bits 1:0   0 = USB3_PHY, 2 = DUMMY_PHY
  //     CLK_SELECT = bits 5:3   1 = USB3_PHY, 4 = DUMMY_PHY
  //
  // so the flat constants below are consistent in both fields:
  //
  //     0x22 (parked)  -> MUX_MODE=2 DUMMY_PHY, CLK_SELECT=4 DUMMY_PHY
  //     0x08 (unparked)-> MUX_MODE=0 USB3_PHY,  CLK_SELECT=1 USB3_PHY
  //
  // Writing 0x08 therefore switches both the data mux and the pipe clock source
  // off the dummy PHY in one store, which is what m1n1 does in reverse at
  // src/usb.c:149.
  //
  MmioWrite32 (PipehandlerBase + PIPEHANDLER_MUX_CTRL, PIPEHANDLER_MUX_CTRL_USB3);

  //
  // Keep the core out of reset and unclamped. m1n1 already set DWC3_RESET_N; be
  // explicit rather than relying on it, and make sure FORCE_CLAMP is off.
  //
  MmioAndThenOr32 (
    PipehandlerBase + PIPEHANDLER_AON_GEN,
    ~PIPEHANDLER_AON_GEN_DWC3_FORCE_CLAMP_EN,
    PIPEHANDLER_AON_GEN_DWC3_RESET_N
    );

  //
  // Same 100ms the DWC3 soft-reset path uses. The PHY needs to produce a stable
  // pipe clock before the core -- and later XhciDxe -- touches port registers.
  //
  MicroSecondDelay (100 * 1000);

  DEBUG ((
    DEBUG_INFO,
    "Dwc3UnparkPipePhy: exit  MUX_CTRL=0x%x NONSELECTED_OVERRIDE=0x%x AON_GEN=0x%x\n",
    MmioRead32 (PipehandlerBase + PIPEHANDLER_MUX_CTRL),
    MmioRead32 (PipehandlerBase + PIPEHANDLER_NONSELECTED_OVERRIDE),
    MmioRead32 (PipehandlerBase + PIPEHANDLER_AON_GEN)
    ));
}
#endif // APPLE_DWC3_UNPARK_PIPE_PHY

//
// This function actually brings up the DWC3 controller. The PHY is already set up by iBoot so we don't need
// to deal with that here.
//
NON_DISCOVERABLE_DEVICE_INIT 
EFIAPI 
AppleUsbTypeCBringupDxeInitializeUsbController(IN UINTN Dwc3ControllerBaseReg)
{
  EFI_STATUS Status;
  DWC3_CONTROLLER *Dwc3Controller;
  UINT32 Usb2PhyCfgReg;
  //
  // This used to read "PHY reset/clock is brought up by iBoot, no need to do it
  // here." That is true of a bare iBoot handoff but NOT of the m1n1 path, which
  // is how we boot: m1n1's usb_phy_bringup() (src/usb.c:120) runs *after* iBoot
  // and deliberately re-parks every DWC3 on the dummy PHY at src/usb.c:149-151,
  // in a loop over all ports (src/usb.c:254 and :342).
  //
  // Measured on J704 with tools/usb_portsc_probe.py -- identical on usb-drd0,
  // usb-drd1 and usb-drd3:
  //
  //     MUX_CTRL = 0x22 (DUMMY)   NONSELECTED_OVERRIDE = 0x9332
  //     PORTSC   = 0x280 -> CCS=0 PED=0 PP=1 PLS=4 (Disabled)
  //
  // So the caller must run Dwc3UnparkPipePhy() before this function, or the core
  // comes up attached to a fake PHY and no device can ever raise CCS.
  //

  Dwc3Controller = (VOID *)(Dwc3ControllerBaseReg + DWC3_REG_OFFSET);

  Status = Dwc3XhciCoreInit(Dwc3Controller);
  if(EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "AppleUsbTypeCBringupDxeInitializeUsbController: USB controller init failed, status %r\n", Status));
    return (VOID *)EFI_DEVICE_ERROR;
  }

  //
  // the core is initialized at this point, U-Boot sets USB2 PHY config based on quirks in the device tree.
  // Since Apple platforms have none of those quirks defined in known device trees, just read and write back the PHY config to be safe.
  //
  Usb2PhyCfgReg = MmioRead32((UINTN)&Dwc3Controller->GUsb2PhyCfg[0]);
  MmioWrite32((UINTN)&Dwc3Controller->GUsb2PhyCfg[0], Usb2PhyCfgReg);

  //
  // Set the DWC3 to host mode.
  //
  Dwc3SetMode(Dwc3Controller, DWC3_GCTL_PRTCAP_HOST);

  //
  // Disabling this for now but per XHCI spec, this should be set per U-Boot comments? do this as a troubleshooting step if things don't work out. also seems to be set in the "core" dwc3 code in U-Boot.
  //
  Dwc3SetFladj(Dwc3Controller, GFLADJ_30MHZ_DEFAULT);
  Dwc3XhciSetBeatBurstLength(Dwc3Controller);
  return (VOID*)Status;
  
}


VOID 
EFIAPI 
AppleUsbTypeCBringupDxeBringupCallback(IN EFI_EVENT Event, IN VOID *Context)
{
  EFI_STATUS Status;
  UINT32 NumDwc3Controllers;
  UINT64 Dwc3ControllerBaseAddr;
#if APPLE_DWC3_UNPARK_PIPE_PHY
  // Only referenced by the un-park block below; the build is -Werror on
  // -Wunused-variable, so it has to follow the same #if.
  UINT64 PipehandlerBaseAddr;
#endif
  CHAR8 Dwc3RegNodeName[31];
  UINT32 Dwc3ControllerRegSize;
  //
  // Close the event so that we don't have duplicate events floating around.
  //
  gBS->CloseEvent(Event);

  DEBUG((DEBUG_INFO, "AppleUsbTypeCBringupDxeBringupCallback started\n"));

  NumDwc3Controllers = PcdGet32(PcdAppleNumDwc3Controllers);

  for(UINT32 Dwc3Index = 0; Dwc3Index < NumDwc3Controllers; Dwc3Index++) {
    //
    // No hardcoded skip list here any more.
    //
    // This used to be "if((Dwc3Index == 0) || (Dwc3Index == 2)) continue;" with
    // the comment "skip DWC3 0, it seems to be in charge of the DFU port". That
    // assumed the proxy/DFU port is always usb0. m1n1 actually comes up on
    // whichever USB-C port the host cable is in, so with the proxy on USB1 the
    // old list skipped the free controller and tried to bring up the one m1n1
    // was using -- and since m1n1 strips that node, dt_get() returned NULL and
    // the driver faulted.
    //
    // Selection is now purely by node presence, checked below: m1n1 removes the
    // ADT node for the controller it owns, so a missing usb-drdN means "leave
    // this one alone" regardless of which port that is. It also covers indices
    // absent on this SoC -- J704 has usb-drd0, usb-drd1 and usb-drd3, no
    // usb-drd2.
    //
    AsciiSPrint(Dwc3RegNodeName, ARRAY_SIZE(Dwc3RegNodeName), "usb-drd%d", Dwc3Index);
    dt_node_t *Dwc3Node = dt_get(Dwc3RegNodeName);

    //
    // The node may be absent: m1n1's hypervisor removes the ADT nodes for the
    // USB controller it is using as its own proxy/VUART, so with the proxy on
    // USB1 there is no /arm-io/usb-drd1 at all.
    //
    // Skipping is required even though dt_node_reg() is now NULL-safe -- it
    // leaves Dwc3ControllerBaseAddr untouched on failure, and registering a
    // non-discoverable XHCI controller at a stale address would be worse than
    // not registering one.
    //
    if(Dwc3Node == NULL) {
      DEBUG((DEBUG_INFO, "AppleUsbTypeCBringupDxeBringupCallback: no %a node, skipping DWC3 %d\n", Dwc3RegNodeName, Dwc3Index));
      continue;
    }

    if(dt_node_reg(Dwc3Node, 0, &Dwc3ControllerBaseAddr, NULL) != 0) {
      DEBUG((DEBUG_ERROR, "AppleUsbTypeCBringupDxeBringupCallback: no reg for %a, skipping DWC3 %d\n", Dwc3RegNodeName, Dwc3Index));
      continue;
    }

    Dwc3ControllerRegSize = 0x100000;//TODO: get from ADT
    DEBUG((DEBUG_INFO, "AppleUsbTypeCBringupDxeBringupCallback: DWC3_%d base address: 0x%llx, size = 0x%x\n", Dwc3Index, Dwc3ControllerBaseAddr, Dwc3ControllerRegSize));

#if APPLE_DWC3_UNPARK_PIPE_PHY
    //
    // Un-park the PIPE PHY before anything touches the DWC3 core or, later, the
    // xHCI port registers. See Dwc3UnparkPipePhy() for why.
    //
    // reg index 3 is the pipehandler block. It is a separate window from the DWC3
    // registers at index 0; if a platform's ADT lacks it there is nothing to
    // un-park, so carry on and let the old behaviour stand.
    //
    if(dt_node_reg(Dwc3Node, 3, &PipehandlerBaseAddr, NULL) != 0) {
      DEBUG((DEBUG_WARN, "AppleUsbTypeCBringupDxeBringupCallback: %a has no reg[3] (pipehandler), leaving PIPE PHY parked\n", Dwc3RegNodeName));
    } else {
      DEBUG((DEBUG_INFO, "AppleUsbTypeCBringupDxeBringupCallback: DWC3_%d pipehandler at 0x%llx\n", Dwc3Index, PipehandlerBaseAddr));
      Dwc3UnparkPipePhy(PipehandlerBaseAddr);
    }
#endif
    
    //
    // Register the controller as a non-registerable XHCI DMA-coherent controller. (All DMA on Apple systems must be cache-coherent)
    // Note: if this doesn't end up working, change the DMA type to non-coherent as one of the first steps to try.
    //
    Status = RegisterNonDiscoverableMmioDevice(NonDiscoverableDeviceTypeXhci, 
             NonDiscoverableDeviceDmaTypeCoherent,
             AppleUsbTypeCBringupDxeInitializeUsbController(Dwc3ControllerBaseAddr),
             NULL,
             1,
             Dwc3ControllerBaseAddr,
             Dwc3ControllerRegSize);
  }
  return;
} 


EFI_STATUS
EFIAPI 
AppleUsbTypeCBringupDxeInitialize(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
) 
{
    EFI_STATUS               Status;
    EFI_EVENT                EndOfDxeEvent;
    //
    // The UsbHcd code in edk2-platforms for NXP platforms (which also use DWC3 controllers) registers the initialization to take place at the end of DXE phase.
    // I'm not quite sure why this is the case, but to avoid problems, do likewise.
    //
    DEBUG((DEBUG_INFO, "AppleUsbTypeCBringupDxeInitialize started\n"));

    Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,
                                TPL_CALLBACK,
                                AppleUsbTypeCBringupDxeBringupCallback,
                                NULL,
                                &gEfiEndOfDxeEventGroupGuid,
                                &EndOfDxeEvent);

    return Status;
}


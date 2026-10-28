/* SPDX-License-Identifier: MIT
 * Menu-scoped M5 keyboard transport. Portable protocol/bootstrap/RTKit cores
 * are shared with Aurora's Windows driver; Mu HidKeyboardDxe owns key layout,
 * repeat, notifications and SimpleTextInput(Ex).
 */
#include <Library/J714MtpKeyboardLib.h>
#include <Protocol/HidKeyboardProtocol.h>
#include <Library/ArmLib.h>
#include <Protocol/Cpu.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "Shared/AppleMtpBootKeyboardCore.h"
#include "Shared/AppleMtpBootstrap.h"
#include "Shared/AppleMtpHid.h"
#include "Shared/AppleMtpStream.h"
#include "Shared/AppleRtkitRuntimeCoreM5.h"

#define CPU 0x62c00000UL
#define MAIL 0x62c08000UL
#define IRQ 0x62c14000UL
#define FIFO 0x62c34000UL
#define PAGE16 0x4000U
#define DVA 0x10000004000UL
#define SRAM 0x294c00000UL
#define SRAM_SIZE 0xc4000U
typedef struct { VOID *Cpu; UINT64 Dma; UINTN Size; BOOLEAN Mapped; } BUFFER;
STATIC struct {
  struct ntasi_asc_transport Asc;
  struct ntasi_rtkit_runtime Rt;
  BUFFER Buffers[4]; UINTN NumBuffers;
  APPLE_MTP_STREAM Stream;
  APPLE_MTP_BOOTSTRAP_CONTEXT Bootstrap;
  struct apple_mtp_boot_keyboard Layout;
  UINT8 Descriptor[4096]; UINTN DescriptorSize;
  UINT8 Sequence, PendingSequence, PendingReport, Last[8];
  APPLE_MTP_BOOTSTRAP_COMMAND Pending;
  BOOLEAN Initialized, Booted, Failed, DescriptorReady, Published, Dart, SlowWait;
  EFI_HANDLE Handle; EFI_EVENT Timer; EFI_CPU_ARCH_PROTOCOL *Cpu;
  KEYBOARD_HID_REPORT_CALLBACK Callback; VOID *CallbackContext;
} mK;
STATIC struct { VENDOR_DEVICE_PATH Vendor; EFI_DEVICE_PATH_PROTOCOL End; } mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   {0x3f47416b,0x7140,0x4bb0,{0xa0,0x3c,0x56,0x41,0x35,0x21,0,2}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4,0}}
};

// Existing x1n1 DART ABI, with the same register arguments as the Windows
// driver. Only dart-mtp (3), SID 0, is used. No register probing or new SPTM op.
#ifndef HVC
#define HVC(Name, Imm) \
STATIC UINT64 Name (UINT64 A, UINT64 B, UINT64 C, UINT64 D, UINT64 E, UINT64 F) { \
  register UINT64 X0 __asm__("x0")=A, X1 __asm__("x1")=B, X2 __asm__("x2")=C; \
  register UINT64 X3 __asm__("x3")=D, X4 __asm__("x4")=E, X5 __asm__("x5")=F; \
  __asm__ volatile("mov x9, sp\n hvc #" #Imm "\n mov sp, x9" \
    : "+r"(X0), "+r"(X1), "+r"(X2), "+r"(X3), "+r"(X4), "+r"(X5) :: "x9", "cc", "memory"); \
  return X0; }
#endif
HVC (DartInit, 0xa511)
HVC (DartAttach, 0xa512)
HVC (DartMap, 0xa513)
HVC (DartUnmap, 0xa514)
HVC (DartEnable, 0xa515)

STATIC uint32_t CpuRead (VOID *O, uint32_t R) { return MmioRead32 (CPU+R); }
STATIC VOID CpuWrite (VOID *O, uint32_t R, uint32_t V) { MmioWrite32 (CPU+R,V); }
STATIC uint32_t MailRead (VOID *O, uint32_t R) { return MmioRead32 (MAIL+R); }
STATIC uint64_t MailRead64 (VOID *O, uint32_t R) { return MmioRead64 (MAIL+R); }
STATIC VOID MailWrite64 (VOID *O, uint32_t R, uint64_t V) { MmioWrite64 (MAIL+R,V); }
STATIC VOID Barrier (VOID *O) { ArmDataMemoryBarrier (); }
STATIC VOID Idle (VOID *O) { MicroSecondDelay (mK.SlowWait ? 1000 : 10); }
STATIC VOID Crash (VOID *O, CONST struct ntasi_rtkit_shared_buffer *B) { mK.Failed=TRUE; }
STATIC int FirmwareBuffer (VOID *O, uint8_t E, uint64_t Address, size_t Size,
                          struct ntasi_rtkit_shared_buffer *B)
{
  if (!Size || Address < SRAM || Address >= SRAM+SRAM_SIZE || Size > SRAM+SRAM_SIZE-Address) return -1;
  B->cpu_address=(VOID *)(UINTN)Address; B->device_address=Address; B->size=Size; return 0;
}
STATIC int Allocate (VOID *O, uint8_t Endpoint, size_t Size, struct ntasi_rtkit_shared_buffer *Out)
{
  BUFFER *B;
  if (!Size || Size > 0x100000 || mK.NumBuffers == ARRAY_SIZE (mK.Buffers)) return -1;
  B=&mK.Buffers[mK.NumBuffers]; B->Size=ALIGN_VALUE(Size,PAGE16);
  B->Cpu=AllocateAlignedReservedPages(EFI_SIZE_TO_PAGES(B->Size),PAGE16);
  if (!B->Cpu) return -1;
  // Reserve one 1 MiB slot per endpoint, entirely inside the attached 32 MiB block.
  B->Dma=DVA+mK.NumBuffers*0x100000; mK.NumBuffers++;
  if (EFI_ERROR(mK.Cpu->SetMemoryAttributes(mK.Cpu,(UINTN)B->Cpu,B->Size,EFI_MEMORY_UC))) return -1;
  ZeroMem(B->Cpu,B->Size);
  if (DartMap(3,0,B->Dma,(UINTN)B->Cpu,B->Size,7)) return -1;
  B->Mapped=TRUE;
  Out->cpu_address=B->Cpu; Out->device_address=B->Dma; Out->size=B->Size; return 0;
}
STATIC VOID Release (VOID *O, uint8_t Endpoint, struct ntasi_rtkit_shared_buffer *B)
{
  // Reclamation happens only after quiesce + CPU stop, never in a callback.
}
STATIC BOOLEAN Send (CONST UINT8 *Bytes, UINTN Size)
{
  if (Size > 2048 || MmioRead32(FIFO+APPLE_DOCKCHANNEL_DATA_TX_FREE) < Size) return FALSE;
  for (UINTN I=0; I<Size; I++) MmioWrite32(FIFO+APPLE_DOCKCHANNEL_DATA_TX8,Bytes[I]);
  return TRUE;
}
STATIC VOID Deliver (UINT8 Report[8])
{
  if (CompareMem(mK.Last,Report,8)==0) return;
  CopyMem(mK.Last,Report,8);
  if (mK.Callback) mK.Callback(BootKeyboard,Report,8,mK.CallbackContext);
}
STATIC VOID Packet (CONST APPLE_DOCKCHANNEL_PACKET_VIEW *P)
{
  APPLE_MTP_MESSAGE_VIEW M;
  APPLE_MTP_CONTROL_EVENT_VIEW E;
  if (AppleMtpParseMessage(P,&M)!=AppleMtpSuccess) return;
  if (P->Channel==APPLE_DOCKCHANNEL_CHANNEL_COMMAND) {
    APPLE_MTP_ACK_VIEW Ack;
    if (mK.Pending==AppleMtpBootstrapCommandNone || P->Sequence!=mK.PendingSequence || P->Interface!=0) return;
    if (AppleMtpValidateAck(&M,mK.PendingSequence,0,AppleMtpFeatureReport,AppleMtpSetReport,mK.PendingReport,&Ack)!=AppleMtpSuccess ||
        AppleMtpBootstrapCommandComplete(&mK.Bootstrap,mK.Pending,Ack.ReturnCode)!=AppleMtpBootstrapSuccess) mK.Failed=TRUE;
    mK.Pending=AppleMtpBootstrapCommandNone; return;
  }
  if (P->Interface==0) {
    if (AppleMtpParseControlEvent(&M,&E)!=AppleMtpSuccess) return;
    if (E.Kind==AppleMtpControlEventReady && mK.Initialized) {
      AppleMtpBootstrapObserveReady(&mK.Bootstrap,E.Ready.Interface);
    } else if (E.Kind==AppleMtpControlEventInit && E.Init.InterfaceKind==AppleMtpInterfaceKeyboard && !mK.DescriptorReady) {
      APPLE_MTP_INIT_BLOCK_ITERATOR It; APPLE_MTP_INIT_BLOCK_VIEW B; APPLE_MTP_STATUS S;
      if (!mK.Initialized) {
        if (AppleMtpBootstrapInitialize(&mK.Bootstrap,AppleMtpInterfaceKeyboard,E.Init.Interface)!=AppleMtpBootstrapSuccess) { mK.Failed=TRUE; return; }
        mK.Initialized=TRUE;
      }
      if (mK.Bootstrap.Interface!=E.Init.Interface || AppleMtpInitBlockIteratorInitialize(&E.Init,&It)!=AppleMtpSuccess) { mK.Failed=TRUE; return; }
      while ((S=AppleMtpInitBlockIteratorNext(&It,&B))==AppleMtpSuccess) {
        if (B.Type==APPLE_MTP_INIT_HID_DESCRIPTOR) {
          if (mK.DescriptorSize || !B.Length || B.Length>sizeof(mK.Descriptor)) { mK.Failed=TRUE; return; }
          CopyMem(mK.Descriptor,B.Data,B.Length); mK.DescriptorSize=B.Length;
        } else if (B.Type==APPLE_MTP_INIT_GPIO_REQUEST) {
          // No inferred GPIO toggle: keyboard descriptors must not require one.
          mK.Failed=TRUE; return;
        }
      }
      if (S!=AppleMtpEndOfBlocks) { mK.Failed=TRUE; return; }
      if (!E.Init.MorePackets) {
        if (apple_mtp_boot_keyboard_parse(mK.Descriptor,mK.DescriptorSize,&mK.Layout) ||
            AppleMtpBootstrapResourcesReady(&mK.Bootstrap,1,0)!=AppleMtpBootstrapSuccess) { mK.Failed=TRUE; return; }
        mK.DescriptorReady=TRUE;
      }
    }
  } else if (mK.DescriptorReady && mK.Bootstrap.State==AppleMtpBootstrapReady) {
    APPLE_MTP_HID_REPORT_VIEW View; UINT8 Boot[8];
    if (AppleMtpExtractKeyboardReport(P,mK.Bootstrap.Interface,mK.Layout.uses_report_ids,1025,&View)==AppleMtpSuccess &&
        apple_mtp_boot_keyboard_decode(&mK.Layout,View.WireReport,View.WireReportLength,Boot)==0) Deliver(Boot);
  }
}
STATIC VOID EFIAPI Poll (EFI_EVENT Event, VOID *Context)
{
  if (mK.Failed || !mK.Booted) return;
  for (UINTN I=0; I<8; I++) {
    int Rc=ntasi_rtkit_runtime_service(&mK.Rt,NULL);
    if (Rc==NTASI_RTKIT_RUNTIME_NO_MESSAGE) break;
    if (Rc<0) { mK.Failed=TRUE; break; }
  }
  // Bound work per tick; remaining FIFO bytes are serviced on the next tick.
  for (UINTN Budget=0; Budget<2048;) {
    UINT32 Count=MmioRead32(FIFO+APPLE_DOCKCHANNEL_DATA_RX_COUNT);
    if (!Count) break;
    UINTN Width=Count>=4 && 2048-Budget>=4 ? 4 : 1;
    UINT32 Value=MmioRead32(FIFO+(Width==4 ? APPLE_DOCKCHANNEL_DATA_RX32 : APPLE_DOCKCHANNEL_DATA_RX8));
    if (Width==1) Value >>= 8;
    Budget+=Width;
    for (UINTN J=0; J<Width; J++) {
      APPLE_DOCKCHANNEL_PACKET_VIEW P; int Ready=0;
      if (AppleMtpStreamPushByte(&mK.Stream,(UINT8)(Value>>(J*8)),&P,&Ready)!=AppleDockChannelSuccess) {
        AppleMtpStreamReset(&mK.Stream); continue;
      }
      if (Ready) { Packet(&P); AppleMtpStreamConsume(&mK.Stream); }
    }
  }
  if (mK.Failed) { UINT8 Empty[8]={0}; Deliver(Empty); }
}
STATIC EFI_STATUS EFIAPI Register (HID_KEYBOARD_PROTOCOL *This, KEYBOARD_HID_REPORT_CALLBACK Cb, VOID *Context)
{
  if (!Cb) return EFI_INVALID_PARAMETER;
  if (mK.Callback) return EFI_ALREADY_STARTED;
  mK.CallbackContext=Context; mK.Callback=Cb;
  Cb(BootKeyboard,mK.Last,8,Context); return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Unregister (HID_KEYBOARD_PROTOCOL *This)
{ mK.Callback=NULL; mK.CallbackContext=NULL; return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI Output (HID_KEYBOARD_PROTOCOL *This, KEYBOARD_HID_INTERFACE Interface, UINT8 *Data, UINTN Size)
{ return EFI_UNSUPPORTED; } // Caps/Num lock are handled by Mu; physical LED output is not implemented.
STATIC HID_KEYBOARD_PROTOCOL mProtocol={Register,Unregister,Output};

EFI_STATUS J714MtpKeyboardStop (VOID)
{
  UINT8 Empty[8]={0}; EFI_STATUS Status;
  if (mK.Timer) { gBS->CloseEvent(mK.Timer); mK.Timer=NULL; }
  Deliver(Empty);
  if (mK.Published) {
    EfiBootManagerUpdateConsoleVariable(ConIn,NULL,(EFI_DEVICE_PATH_PROTOCOL *)&mPath);
    gBS->DisconnectController(mK.Handle,NULL,NULL);
    Status=gBS->UninstallMultipleProtocolInterfaces(mK.Handle,&gHidKeyboardProtocolGuid,&mProtocol,
                    &gEfiDevicePathProtocolGuid,&mPath,NULL);
    if (EFI_ERROR(Status)) return Status; // application code cannot be unloaded yet
    mK.Published=FALSE; mK.Handle=NULL; mK.Callback=NULL;
  }
  if (mK.Booted || mK.Rt.boot_step!=NTASI_RTKIT_BOOT_STEP_IDLE) {
    // No DMA release or OS takeover if the coprocessor fails to quiesce.
    mK.SlowWait=TRUE;
    if (ntasi_rtkit_runtime_quiesce(&mK.Rt)!=NTASI_RTKIT_RUNTIME_OK) return EFI_DEVICE_ERROR;
    CpuWrite(NULL,NTASI_ASC_CPU_CONTROL,0); Barrier(NULL);
    if (CpuRead(NULL,NTASI_ASC_CPU_CONTROL)&NTASI_ASC_CPU_CONTROL_START) return EFI_DEVICE_ERROR;
    mK.Booted=FALSE; mK.Rt.boot_step=NTASI_RTKIT_BOOT_STEP_IDLE;
  }
  if (mK.Dart) {
    UINTN Budget=8192;
    while (Budget-- && MmioRead32(FIFO+APPLE_DOCKCHANNEL_DATA_RX_COUNT))
      MmioRead32(FIFO+APPLE_DOCKCHANNEL_DATA_RX8);
    if (MmioRead32(FIFO+APPLE_DOCKCHANNEL_DATA_RX_COUNT)) return EFI_DEVICE_ERROR;
  }
  for (UINTN I=0; I<mK.NumBuffers; I++) {
    BUFFER *B=&mK.Buffers[I];
    if (B->Mapped) {
      if (DartUnmap(3,0,B->Dma,B->Size,0,0)) return EFI_DEVICE_ERROR;
      B->Mapped=FALSE;
    }
    if (B->Cpu) {
      if (EFI_ERROR(mK.Cpu->SetMemoryAttributes(mK.Cpu,(UINTN)B->Cpu,B->Size,EFI_MEMORY_WB))) return EFI_DEVICE_ERROR;
      FreeAlignedPages(B->Cpu,EFI_SIZE_TO_PAGES(B->Size)); B->Cpu=NULL;
    }
  }
  if (mK.Dart && DartEnable(3,0,0,0,0,0)) return EFI_DEVICE_ERROR;
  ZeroMem(&mK,sizeof(mK)); return EFI_SUCCESS;
}

EFI_STATUS J714MtpKeyboardStart (VOID)
{
  EFI_STATUS Status;
  // Appended, read-only readiness ABI. Old x1n1 returns zero here. Never touch
  // the direct MTP aliases unless the host has validated and mapped all of them.
  if (MmioRead64(0x61f00000)!=0x78316e31 || MmioRead64(0x61f00008)!=1 ||
      MmioRead64(0x61f000d8)!=0x4d54504b00000001UL) return EFI_UNSUPPORTED;
  if (mK.Booted) return EFI_ALREADY_STARTED;
  if (mK.NumBuffers || mK.Published || mK.Dart) return EFI_DEVICE_ERROR;
  // The candidate takes a cold/stopped helper only. It never resets an active owner.
  if (CpuRead(NULL,NTASI_ASC_CPU_CONTROL)&NTASI_ASC_CPU_CONTROL_START) return EFI_NOT_READY;
  // Mask before starting RTKit: INIT may arrive as soon as the helper runs.
  // Polling owns the dock channel in the menu; Windows reconnects IRQ 660.
  MmioAnd32(IRQ+APPLE_DOCKCHANNEL_IRQ_MASK_OFFSET,~(BIT1|BIT17));
  MmioWrite32(IRQ+APPLE_DOCKCHANNEL_IRQ_FLAG_OFFSET,BIT1|BIT17);
  Status=gBS->LocateProtocol(&gEfiCpuArchProtocolGuid,NULL,(VOID **)&mK.Cpu);
  if (EFI_ERROR(Status)) return Status;
  if (DartInit(3,0,0,0,0,0) || DartEnable(3,0,0,0,0,0) ||
      DartAttach(3,0,DVA,1,0,0) || DartAttach(3,0,DVA,2,0,0) || DartEnable(3,0,1,0,0,0)) return EFI_DEVICE_ERROR;
  mK.Dart=TRUE;
  CONST struct ntasi_asc_ops Asc={CpuRead,CpuWrite,MailRead,MailRead64,MailWrite64,Barrier,Barrier,Idle};
  CONST struct ntasi_rtkit_runtime_ops Rt={.allocate_shared=Allocate,.release_shared=Release,.crashed=Crash,.map_firmware_buffer=FirmwareBuffer};
  if (ntasi_asc_init(&mK.Asc,&Asc,NULL,5000) || ntasi_rtkit_runtime_init(&mK.Rt,&mK.Asc,&Rt,NULL,5000)) return EFI_DEVICE_ERROR;
  mK.SlowWait=TRUE;
  mK.Rt.boot_mode=NTASI_RTKIT_BOOT_MODE_M1N1; mK.Rt.cpu_start_exclusive=true;
  if (ntasi_rtkit_runtime_boot(&mK.Rt)) return EFI_DEVICE_ERROR;
  mK.Booted=TRUE;
  for (UINTN Tick=0; Tick<5000 && !mK.Failed; Tick++) {
    Poll(NULL,NULL);
    if (mK.Bootstrap.State==AppleMtpBootstrapReady) break;
    if (mK.DescriptorReady && mK.Pending==AppleMtpBootstrapCommandNone) {
      APPLE_MTP_BOOTSTRAP_COMMAND C=AppleMtpBootstrapNextCommand(&mK.Bootstrap);
      UINT8 Bytes[64]; size_t Size=0; APPLE_MTP_STATUS Rc;
      if (C==AppleMtpBootstrapCommandEnable) {
        Rc=AppleMtpBuildEnableInterfacePacket(mK.Sequence,mK.Bootstrap.Interface,Bytes,sizeof(Bytes),&Size);
        mK.PendingReport=APPLE_MTP_COMMAND_ENABLE_INTERFACE;
      } else if (C==AppleMtpBootstrapCommandResetAssertWill || C==AppleMtpBootstrapCommandResetAssertHas ||
                 C==AppleMtpBootstrapCommandResetReleaseWill || C==AppleMtpBootstrapCommandResetReleaseHas) {
        if (C==AppleMtpBootstrapCommandResetReleaseHas) MicroSecondDelay(50000);
        Rc=AppleMtpBuildResetInterfacePacket(mK.Sequence,mK.Bootstrap.Interface,
             (C==AppleMtpBootstrapCommandResetAssertWill || C==AppleMtpBootstrapCommandResetAssertHas)?0:2,mK.Bootstrap.PowerMethod,
             (C==AppleMtpBootstrapCommandResetAssertHas || C==AppleMtpBootstrapCommandResetReleaseHas)?
               APPLE_MTP_POWER_PHASE_HAS_CHANGED:APPLE_MTP_POWER_PHASE_WILL_CHANGE,0,Bytes,sizeof(Bytes),&Size);
        mK.PendingReport=APPLE_MTP_COMMAND_RESET_INTERFACE;
      } else if (C==AppleMtpBootstrapCommandNone) { MicroSecondDelay(1000); continue; }
      else { mK.Failed=TRUE; break; }
      if (Rc!=AppleMtpSuccess) { mK.Failed=TRUE; break; }
      if (Send(Bytes,Size)) {
        if (AppleMtpBootstrapCommandSent(&mK.Bootstrap,C)!=AppleMtpBootstrapSuccess) { mK.Failed=TRUE; break; }
        mK.Pending=C; mK.PendingSequence=mK.Sequence++;
      }
    }
    MicroSecondDelay(1000);
  }
  if (mK.Failed || mK.Bootstrap.State!=AppleMtpBootstrapReady) return EFI_DEVICE_ERROR;
  mK.SlowWait=FALSE;
  Status=gBS->InstallMultipleProtocolInterfaces(&mK.Handle,&gHidKeyboardProtocolGuid,&mProtocol,
            &gEfiDevicePathProtocolGuid,&mPath,NULL);
  if (EFI_ERROR(Status)) return Status;
  mK.Published=TRUE;
  Status=gBS->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,Poll,NULL,&mK.Timer);
  if (EFI_ERROR(Status)) return Status;
  Status=gBS->SetTimer(mK.Timer,TimerPeriodic,100000); // 10 ms, UEFI only
  if (EFI_ERROR(Status)) return Status;
  Status=gBS->ConnectController(mK.Handle,NULL,NULL,TRUE);
  DEBUG((DEBUG_INFO,"J714_MTP: keyboard descriptor validated, Mu HID frontend %r\n",Status));
  return Status;
}

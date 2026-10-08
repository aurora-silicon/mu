/* SPDX-License-Identifier: MIT */
#include <Uefi.h>
STATIC UINT64 FakeHvc(UINTN Op, UINT64 A, UINT64 B, UINT64 C, UINT64 D, UINT64 E, UINT64 F);
#define HVC(Name, Imm) STATIC UINT64 Name(UINT64 A,UINT64 B,UINT64 C,UINT64 D,UINT64 E,UINT64 F) { return FakeHvc(Imm,A,B,C,D,E,F); }
#define ntasi_rtkit_runtime_quiesce FakeQuiesce
#include "../Drivers/J714MtpKeyboardDxe/J714MtpKeyboard.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

STATIC UINTN FreeCount, Unmaps, Calls;
STATIC int QuiesceResult;
STATIC BOOLEAN UnmapFails, StuckRun;
STATIC UINT32 CpuControl;
STATIC UINT8 LastReport[8];
STATIC EFI_BOOT_SERVICES Services;
STATIC EFI_CPU_ARCH_PROTOCOL CpuProtocol;
EFI_BOOT_SERVICES *gBS=&Services;
EFI_GUID gHidKeyboardProtocolGuid,gEfiDevicePathProtocolGuid;
VOID *EFIAPI CopyMem(VOID *D,CONST VOID *S,UINTN N) { return memcpy(D,S,N); }
VOID *EFIAPI ZeroMem(VOID *D,UINTN N) { return memset(D,0,N); }
INTN EFIAPI CompareMem(CONST VOID *A,CONST VOID *B,UINTN N) { return memcmp(A,B,N); }
VOID EFIAPI FreeAlignedPages(VOID *P,UINTN Pages) { FreeCount++; free(P); }
VOID EFIAPI ArmDataMemoryBarrier(VOID) {}
UINT64 EFIAPI MmioRead64(UINTN A) { assert(0); return 0; }
UINT64 EFIAPI MmioWrite64(UINTN A,UINT64 V) { assert(0); return V; }
UINTN EFIAPI MicroSecondDelay(UINTN Us) { assert(0); return Us; }
VOID *EFIAPI AllocateAlignedReservedPages(UINTN Pages,UINTN Alignment) { assert(0); return NULL; }
UINT32 EFIAPI MmioRead32(UINTN A) { return A==CPU+NTASI_ASC_CPU_CONTROL ? CpuControl : 0; }
UINT32 EFIAPI MmioWrite32(UINTN A,UINT32 V) { if (!StuckRun && A==CPU+NTASI_ASC_CPU_CONTROL) CpuControl=V; return V; }
int FakeQuiesce(struct ntasi_rtkit_runtime *R) { return QuiesceResult; }
STATIC UINT64 FakeHvc(UINTN Op,UINT64 A,UINT64 B,UINT64 C,UINT64 D,UINT64 E,UINT64 F) {
  assert(A==3 && B==0);
  if (Op==0xa514) { Unmaps++; assert(C==DVA && D==PAGE16); return UnmapFails ? MAX_UINT64 : 0; }
  assert(Op==0xa515 && C==0); return 0;
}
EFI_STATUS EFIAPI EfiBootManagerUpdateConsoleVariable(CONSOLE_TYPE T,EFI_DEVICE_PATH_PROTOCOL *Add,EFI_DEVICE_PATH_PROTOCOL *Remove) { return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI Attributes(EFI_CPU_ARCH_PROTOCOL *P,EFI_PHYSICAL_ADDRESS Base,UINT64 Size,UINT64 Attr) {
  assert(Attr==EFI_MEMORY_WB && Size==PAGE16); return EFI_SUCCESS;
}
STATIC VOID EFIAPI Report(KEYBOARD_HID_INTERFACE I,UINT8 *B,UINTN Size,VOID *Context) {
  assert(I==BootKeyboard && Size==8); memcpy(LastReport,B,8); Calls++;
}
STATIC VOID Inject(UINT8 Channel,UINT8 Interface,UINT8 Sequence,APPLE_MTP_REPORT_TYPE Type,CONST UINT8 *Payload,UINTN Size) {
  UINT8 Bytes[1024]; size_t Length;
  APPLE_DOCKCHANNEL_PACKET_VIEW P;
  assert(AppleMtpBuildMessagePacket(Channel,Sequence,Interface,Type,AppleMtpSetReport,0,Payload,Size,Bytes,sizeof(Bytes),&Length)==AppleMtpSuccess);
  assert(AppleDockChannelParsePacket(Bytes,Length,&P)==AppleDockChannelSuccess);
  Packet(&P);
}
int main(void) {
  // Use a conventional keyboard descriptor with an extra consumer byte,
  // mirroring the descriptor-defined shape rather than hardcoded report size.
  CONST UINT8 Descriptor[]={0x05,1,0x09,6,0xa1,1,0x85,1,0x05,7,0x19,0xe0,0x29,0xe7,
    0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,
    0x19,0,0x29,0x65,0x25,0x65,0x75,8,0x95,6,0x81,0,0x05,0x0c,0x75,8,0x95,1,0x81,1,0xc0};
  UINT8 Init[128]={0xf0,1,0,2};
  memcpy(Init+4,"keyboard",8);
  Init[22]=APPLE_MTP_INIT_HID_DESCRIPTOR; Init[24]=sizeof(Descriptor);
  memcpy(Init+26,Descriptor,sizeof(Descriptor));
  UINTN End=26+sizeof(Descriptor); Init[End]=APPLE_MTP_INIT_TERMINATOR; Init[End+2]=6;
  Inject(0x12,0,0,AppleMtpInputReport,Init,End+10);
  assert(!mK.Failed && mK.Initialized && mK.DescriptorReady && mK.Bootstrap.Interface==2);
  assert(mK.Bootstrap.State==AppleMtpBootstrapNeedEnable);
  assert(AppleMtpBootstrapCommandSent(&mK.Bootstrap,AppleMtpBootstrapCommandEnable)==AppleMtpBootstrapSuccess);
  mK.Pending=AppleMtpBootstrapCommandEnable; mK.PendingSequence=7; mK.PendingReport=APPLE_MTP_COMMAND_ENABLE_INTERFACE;
  UINT8 Ack[]={APPLE_MTP_COMMAND_ENABLE_INTERFACE};
  Inject(0x11,0,6,AppleMtpFeatureReport,Ack,sizeof(Ack)); assert(mK.Pending!=AppleMtpBootstrapCommandNone);
  Inject(0x11,0,7,AppleMtpFeatureReport,Ack,sizeof(Ack));
  assert(!mK.Failed && mK.Pending==AppleMtpBootstrapCommandNone && mK.Bootstrap.State==AppleMtpBootstrapReady);
  mK.Callback=Report;
  UINT8 Key[]={1,2,0,4,0,0,0,0,0,0};
  Inject(0x12,2,1,AppleMtpInputReport,Key,sizeof(Key)); assert(Calls==1 && LastReport[0]==2 && LastReport[2]==4);
  Inject(0x12,2,2,AppleMtpInputReport,Key,sizeof(Key)); assert(Calls==1); // held-key duplicates
  Key[0]=0x52; Inject(0x12,2,3,AppleMtpInputReport,Key,sizeof(Key)); assert(Calls==1 && LastReport[2]==4);
  Key[0]=1; Inject(0x12,2,4,AppleMtpInputReport,Key,sizeof(Key)-1); assert(Calls==1);
  memset(Key+1,0,sizeof(Key)-1); Inject(0x12,2,5,AppleMtpInputReport,Key,sizeof(Key)); assert(Calls==2 && LastReport[2]==0);
  // Teardown must retain pages when quiesce, CPU stop, or unmap fails.
  memset(&mK,0,sizeof(mK)); mK.Booted=TRUE; mK.Dart=TRUE; mK.NumBuffers=1;
  mK.Buffers[0]=(BUFFER){calloc(1,PAGE16),DVA,PAGE16,TRUE};
  CpuProtocol.SetMemoryAttributes=Attributes; mK.Cpu=&CpuProtocol; CpuControl=NTASI_ASC_CPU_CONTROL_START;
  QuiesceResult=-1; assert(J714MtpKeyboardStop()==EFI_DEVICE_ERROR && FreeCount==0 && Unmaps==0);
  QuiesceResult=0; StuckRun=TRUE; assert(J714MtpKeyboardStop()==EFI_DEVICE_ERROR && FreeCount==0 && Unmaps==0);
  StuckRun=FALSE; UnmapFails=TRUE; assert(J714MtpKeyboardStop()==EFI_DEVICE_ERROR && FreeCount==0 && Unmaps==1);
  UnmapFails=FALSE; assert(J714MtpKeyboardStop()==EFI_SUCCESS && FreeCount==1 && Unmaps==2);
  assert(J714MtpKeyboardStop()==EFI_SUCCESS && FreeCount==1);
  puts("PASS: actual MTP ACK matching, report translation, repeat/release/consumer isolation, failed-quiesce/stop/unmap DMA retention, retry cleanup");
  return 0;
}

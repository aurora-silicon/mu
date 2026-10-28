/* SPDX-License-Identifier: MIT
 * M5 UEFI storage frontend over the Windows portable ANS/ASC/RTKit cores.
 * Only the host's validated resident SPTM ABI owns queue and DMA admission.
 * Host-only candidate: the Windows reserved-queue handoff consumer is still
 * required before enabling this driver in an OS-booting hardware profile.
 */
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/HardwareInterrupt.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "Shared/AppleAscCore.h"
#include "Shared/AppleRtkitRuntimeCore.h"
#include "Shared/AppleNvmeControllerCore.h"
#include "Shared/AppleNvmeBlockCore.h"
#include "Shared/AppleNvmePrpCore.h"

#define BRIDGE 0x61f00000UL
#define NVME 0x60300000UL
#define CPU 0x60200000UL
#define MAILBOX 0x60208000UL
#define PAGE16 0x4000U
#define MAX_TRANSFER 0x100000U
#define POLL_LIMIT 1000000U
#define ANS_IRQ 2338

typedef struct { UINT32 Op, Flags; UINT64 Arg[6], Ret; } OP;
typedef struct { UINT32 Count, Flags; INT32 Status; UINT32 Done; OP Ops[6]; } BATCH;
STATIC_ASSERT (sizeof (OP) == 64 && OFFSET_OF (BATCH, Ops) == 16, "SPTM batch ABI");
typedef struct { VOID *Cpu; UINTN Size; UINT64 Dma; } BUFFER;
typedef struct {
  BATCH *Batch;
  BUFFER Buffers[24]; UINTN NumBuffers;
  BOOLEAN Published, Pinned, Fatal, Stopping, Busy, Retry[2][64];
  struct ntasi_ans_queue_memory Admin, Io;
  struct ntasi_ans_controller Controller;
  struct ntasi_asc_transport Asc;
  struct ntasi_rtkit_runtime Rtkit;
  struct ntasi_ans_block_device Block;
  BUFFER *Bounce, *Prps;
  EFI_BLOCK_IO_PROTOCOL Protocol; EFI_BLOCK_IO_MEDIA Media;
  EFI_HANDLE Handle; EFI_EVENT ExitEvent;
  EFI_HARDWARE_INTERRUPT_PROTOCOL *Interrupt;
} ANS;
STATIC ANS mAns;
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   {0x873714a5, 0x1820, 0x4aab, {0x82,0x10,0x8d,0x27,0x90,0x13,0x3e,0x01}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4,0}}
};

STATIC VOID Barrier (VOID *Opaque) { ArmDataMemoryBarrier (); }
STATIC VOID Idle (VOID *Opaque) { MicroSecondDelay (1); }
STATIC VOID Clear (ANS *A) { ZeroMem (A->Batch, sizeof (*A->Batch)); }
STATIC INTN Exec (ANS *A, UINTN Doorbell, UINT32 Count)
{
  BATCH *B = A->Batch;
  if (Count == 0 || Count > ARRAY_SIZE (B->Ops)) return -1;
  B->Count = Count; B->Status = -1; B->Done = 0;
  Barrier (A);
  MmioWrite64 (BRIDGE + Doorbell, (UINTN)B);
  Barrier (A);
  return B->Status == 0 && B->Done == Count ? 0 : -1;
}
STATIC BUFFER *Allocate (ANS *A, UINTN Size)
{
  BUFFER *B;
  if (!Size || Size > 16 * MAX_TRANSFER || A->NumBuffers == ARRAY_SIZE (A->Buffers)) return NULL;
  Size = ALIGN_VALUE (Size, PAGE16);
  B = &A->Buffers[A->NumBuffers];
  B->Cpu = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (Size), PAGE16);
  if (!B->Cpu) return NULL;
  B->Size = Size;
  A->NumBuffers++;
  ZeroMem (B->Cpu, Size);
  Clear (A); A->Batch->Ops[0].Op = 8;
  A->Batch->Ops[0].Arg[0] = (UINTN)B->Cpu; A->Batch->Ops[0].Arg[1] = Size;
  if (Exec (A, 0x1010, 1) || !A->Batch->Ops[0].Ret || (A->Batch->Ops[0].Ret & (PAGE16-1))) return NULL;
  B->Dma = A->Batch->Ops[0].Ret;
  return B;
}
STATIC INTN Grant (ANS *A, BUFFER *B)
{
  Clear (A); A->Batch->Ops[0].Op = 2;
  A->Batch->Ops[0].Arg[0] = (UINTN)B->Cpu;
  A->Batch->Ops[0].Arg[1] = B->Size; A->Batch->Ops[0].Arg[2] = 1;
  INTN Result = Exec (A, 0x1000, 1);
  if (A->Batch->Done) A->Published = TRUE;
  return Result;
}
STATIC UINT32 CpuRead (VOID *O, UINT32 R) { return MmioRead32 (CPU + R); }
STATIC VOID CpuWrite (VOID *O, UINT32 R, UINT32 V) { MmioWrite32 (CPU + R, V); }
STATIC UINT32 MailRead (VOID *O, UINT32 R) { return MmioRead32 (MAILBOX + R); }
STATIC uint64_t MailRead64 (VOID *O, uint32_t R) { return MmioRead64 (MAILBOX + R); }
STATIC VOID MailWrite64 (VOID *O, uint32_t R, uint64_t V) { MmioWrite64 (MAILBOX + R, V); }
STATIC UINT32 NvRead (VOID *O, UINT32 R) { return MmioRead32 (NVME + R); }
STATIC VOID NvWrite (VOID *O, UINT32 R, UINT32 V) { MmioWrite32 (NVME + R, V); }
STATIC VOID Crash (VOID *O, CONST struct ntasi_rtkit_shared_buffer *B) { ((ANS *)O)->Fatal = TRUE; }
STATIC VOID Service (VOID *O)
{
  ANS *A = O;
  if (!A->Fatal && ntasi_rtkit_runtime_service (&A->Rtkit, NULL) < 0) A->Fatal = TRUE;
  Idle (O);
}
STATIC int SharedAllocate (VOID *O, uint8_t Endpoint, size_t Size, struct ntasi_rtkit_shared_buffer *Out)
{
  ANS *A = O; BUFFER *B = Allocate (A, Size);
  if (!B || Grant (A, B)) return -1;
  Out->cpu_address = B->Cpu; Out->device_address = B->Dma; Out->size = B->Size;
  return 0;
}
STATIC VOID SharedRelease (VOID *O, uint8_t Endpoint, struct ntasi_rtkit_shared_buffer *B)
{
  ANS *A = O;
  for (UINTN I = 0; I < A->NumBuffers; I++) {
    BUFFER *Owned = &A->Buffers[I];
    if (Owned->Cpu != B->cpu_address || Owned->Dma != B->device_address || Owned->Size != B->size) continue;
    Clear (A); A->Batch->Ops[0].Op = 3;
    A->Batch->Ops[0].Arg[0] = (UINTN)Owned->Cpu; A->Batch->Ops[0].Arg[1] = Owned->Size;
    if (Exec (A, 0x1000, 1)) A->Fatal = TRUE;
    // Published pages stay reserved even when the firmware releases a grant.
    return;
  }
  A->Fatal = TRUE;
}
STATIC int Queues (VOID *O, CONST struct ntasi_ans_queue_memory *Admin,
                   CONST struct ntasi_ans_queue_memory *Io, uint32_t Ad, uint32_t Id)
{
  ANS *A = O; BATCH *B = A->Batch;
  if (A->Pinned || Ad != 2 || Id != 64 || (NvRead (A, NTASI_ANS_REG_CC) & 1) ||
      (NvRead (A, NTASI_ANS_REG_CSTS) & 1)) return -1;
  Clear (A);
  B->Ops[0].Op = 3; B->Ops[0].Arg[0] = 253; B->Ops[0].Arg[1] = 2;
  B->Ops[1].Op = 0;
  B->Ops[2].Op = 4; B->Ops[2].Flags = 1;
  B->Ops[2].Arg[0] = Admin->commands_dma; B->Ops[2].Arg[1] = Ad-1;
  B->Ops[2].Arg[2] = Admin->completions_dma; B->Ops[2].Arg[3] = Ad-1;
  B->Ops[3].Op = 5; B->Ops[3].Arg[0] = B->Ops[3].Arg[1] = Id-1;
  B->Ops[4].Op = 7; B->Ops[4].Flags = 1; B->Ops[4].Arg[0] = Io->completions_dma;
  B->Ops[5].Op = 6; B->Ops[5].Flags = 1; B->Ops[5].Arg[0] = Io->commands_dma;
  INTN Result = Exec (A, 0x1010, 6);
  if (B->Done >= 3) A->Pinned = TRUE;
  return (int)Result;
}
STATIC int Map (VOID *O, bool Admin, uint8_t Tag, uint64_t Tcb, CONST struct ntasi_ans_sqe *Cmd)
{
  ANS *A = O; UINT64 Bytes = 0;
  if (Tag >= 64 || A->Retry[Admin ? 0 : 1][Tag] || A->Fatal) return -1;
  if (Admin && Cmd->opcode == NTASI_ANS_ADMIN_CMD_IDENTIFY) Bytes = 4096;
  else if (Admin && (Cmd->opcode == NTASI_ANS_ADMIN_CMD_CREATE_CQ ||
                     Cmd->opcode == NTASI_ANS_ADMIN_CMD_CREATE_SQ)) Bytes = 0;
  else if (!Admin && Cmd->opcode == NTASI_ANS_CMD_READ && A->Block.identified)
    Bytes = ((UINT64)(Cmd->cdw12 & 0xffff) + 1) * A->Block.media.block_size;
  else return -1; // This firmware frontend publishes read-only media.
  if (Bytes > MAX_TRANSFER) return -1;
  (Admin ? A->Admin.tcbs : A->Io.tcbs)[Tag].opcode = 0;
  Clear (A); OP *P = &A->Batch->Ops[0]; P->Op = 1; P->Flags = 1;
  P->Arg[0] = Admin ? 0 : 1; P->Arg[1] = Tag; P->Arg[2] = Tcb; P->Arg[3] = Bytes;
  return (int)Exec (A, 0x1010, 1);
}
STATIC int Unmap (VOID *O, bool Admin, uint16_t Tag)
{
  ANS *A = O; UINTN Q = Admin ? 0 : 1;
  if (Tag >= 64) return -1;
  Clear (A); OP *P = &A->Batch->Ops[0]; P->Op = 2; P->Flags = 2;
  P->Arg[0] = Q; P->Arg[1] = Tag; P->Arg[2] = A->Retry[Q][Tag] ? 1 : 0;
  if (Exec (A, 0x1010, 1) || P->Ret > 1) { A->Fatal = TRUE; return -1; }
  A->Retry[Q][Tag] = P->Ret != 0;
  return (int)P->Ret;
}
STATIC int Execute (VOID *O, bool Admin, CONST struct ntasi_ans_sqe *Cmd,
                    enum ntasi_ans_dma_direction Direction, uint64_t *Result)
{
  ANS *A = O;
  if (A->Fatal || A->Stopping) return NTASI_ANS_CONTROLLER_ERR_NOT_STARTED;
  int Status = ntasi_ans_controller_execute (&A->Controller,
                 Admin ? &A->Controller.admin : &A->Controller.io, Cmd, Direction, Result);
  if (Status || A->Fatal) { A->Fatal = TRUE; return Status ? Status : NTASI_ANS_CONTROLLER_ERR_FATAL_STATE; }
  return 0;
}
STATIC EFI_STATUS EFIAPI Reset (EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN Extended)
{
  // No destructive controller reset or replacement of pinned queue storage.
  return This != &mAns.Protocol ? EFI_INVALID_PARAMETER :
    (mAns.Fatal || mAns.Stopping ? EFI_DEVICE_ERROR : EFI_SUCCESS);
}
STATIC EFI_STATUS EFIAPI Read (EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId,
                              EFI_LBA Lba, UINTN Size, VOID *Buffer)
{
  ANS *A = &mAns;
  if (This != &A->Protocol) return EFI_INVALID_PARAMETER;
  if (MediaId != A->Media.MediaId) return EFI_MEDIA_CHANGED;
  if (A->Fatal || A->Stopping) return EFI_DEVICE_ERROR;
  if (!Size) return EFI_SUCCESS;
  if (!Buffer) return EFI_INVALID_PARAMETER;
  if ((UINTN)Buffer > MAX_UINTN - Size) return EFI_INVALID_PARAMETER;
  if (Size % A->Media.BlockSize) return EFI_BAD_BUFFER_SIZE;
  UINT64 Blocks = Size / A->Media.BlockSize;
  if (Lba > A->Media.LastBlock || Blocks-1 > A->Media.LastBlock-Lba) return EFI_INVALID_PARAMETER;
  if (A->Busy) return EFI_NOT_READY;
  A->Busy = TRUE;
  EFI_STATUS Status = EFI_SUCCESS;
  while (Size) {
    UINTN Chunk = MIN (Size, MAX_TRANSFER);
    struct ntasi_ans_dma_segment Segment = {A->Bounce->Dma, Chunk};
    struct ntasi_ans_prp_list_page List = {A->Prps->Cpu, A->Prps->Dma};
    struct ntasi_ans_prp_mapping Mapping;
    struct ntasi_ans_sqe Cmd;
    int Rc = ntasi_ans_prp_build (&Segment, 1, Chunk, &List, 1, &Mapping);
    if (Rc) { Status = EFI_DEVICE_ERROR; break; }
    ntasi_ans_cmd_rw (&Cmd, NTASI_ANS_CMD_READ, A->Block.media.nsid, Lba,
                      (UINT32)(Chunk / A->Media.BlockSize - 1), Mapping.prp1, Mapping.prp2);
    Rc = Execute (A, false, &Cmd, NTASI_ANS_DMA_FROM_DEVICE, NULL);
    if (Rc) {
      DEBUG ((DEBUG_ERROR, "J714_ANS: read failed rc=%d stage=%u status=%x\n",
              Rc, A->Controller.last_substage, A->Controller.last_completion_status));
      Status = EFI_DEVICE_ERROR; break;
    }
    CopyMem (Buffer, A->Bounce->Cpu, Chunk);
    Buffer = (UINT8 *)Buffer + Chunk; Size -= Chunk; Lba += Chunk / A->Media.BlockSize;
  }
  A->Busy = FALSE;
  return Status;
}
STATIC EFI_STATUS EFIAPI Write (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *Buffer)
{ return EFI_WRITE_PROTECTED; }
STATIC EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *This)
{ return Reset (This, FALSE); }
STATIC VOID EFIAPI Exit (EFI_EVENT Event, VOID *Context)
{
  ANS *A = Context;
  A->Stopping = TRUE;
  // Every successful synchronous read already consumed and released its CQE.
  // Preserve CC.EN/RDY and live RTKit. This alone is NOT the complete SPTM
  // ownership handoff: the next owner must also reuse the pinned queue pages.
  // All published DMA pages are EfiReservedMemoryType; never reuse at EBS.
  A->Interrupt->DisableInterruptSource (A->Interrupt, ANS_IRQ);
  Barrier (A);
  DEBUG ((DEBUG_INFO, "J714_ANS: EBS preserve controller CC=%x CSTS=%x fatal=%u busy=%u\n",
          NvRead (A, NTASI_ANS_REG_CC), NvRead (A, NTASI_ANS_REG_CSTS), A->Fatal, A->Busy));
}
STATIC BOOLEAN AllocateQueue (ANS *A, struct ntasi_ans_queue_memory *Q)
{
  BUFFER *Sq = Allocate (A, PAGE16), *Cq = Allocate (A, PAGE16), *Tcb = Allocate (A, PAGE16);
  if (!Sq || !Cq || !Tcb || Grant (A, Sq) || Grant (A, Cq) || Grant (A, Tcb)) return FALSE;
  Q->commands = Sq->Cpu; Q->commands_dma = Sq->Dma;
  Q->completions = Cq->Cpu; Q->completions_dma = Cq->Dma;
  Q->tcbs = Tcb->Cpu; Q->tcbs_dma = Tcb->Dma;
  return TRUE;
}
EFI_STATUS EFIAPI J714AnsEntry (EFI_HANDLE Image, EFI_SYSTEM_TABLE *System)
{
  ANS *A = &mAns;
  EFI_STATUS Status;
  int Rc;
  if (MmioRead64 (BRIDGE) != 0x78316e31 || MmioRead64 (BRIDGE+8) != 1 ||
      (MmioRead64 (BRIDGE+0x18) & 0x1a) != 0x1a || MmioRead64 (BRIDGE+0x20) != 1 ||
      MmioRead64 (BRIDGE+0x28) != 2338) return EFI_UNSUPPORTED;
  Status = gBS->LocateProtocol (&gHardwareInterruptProtocolGuid, NULL, (VOID **)&A->Interrupt);
  if (EFI_ERROR (Status)) return Status;
  // No new INIT/HELLO or cold reset of inherited storage firmware.
  if (!(CpuRead (A, NTASI_ASC_CPU_CONTROL) & NTASI_ASC_CPU_CONTROL_START) ||
      NvRead (A, NTASI_ANS_REG_BOOT_STATUS) != NTASI_ANS_BOOT_STATUS_OK ||
      !(NvRead (A, NTASI_ANS_REG_CC) & 1) || (NvRead (A, NTASI_ANS_REG_CSTS) & 3) != 1)
    return EFI_NOT_READY;
  A->Batch = AllocateAlignedReservedPages (EFI_SIZE_TO_PAGES (PAGE16), PAGE16);
  if (!A->Batch) return EFI_OUT_OF_RESOURCES;
  Clear (A); A->Batch->Ops[0].Op = 9;
  if (Exec (A, 0x1010, 1) || !(A->Batch->Ops[0].Ret & 2)) goto Fail;
  Clear (A); A->Batch->Ops[0].Op = 1; A->Batch->Ops[0].Arg[0] = 1;
  if (Exec (A, 0x1000, 1)) goto Fail;
  if (!AllocateQueue (A, &A->Admin) || !AllocateQueue (A, &A->Io)) goto Fail;
  A->Bounce = Allocate (A, MAX_TRANSFER); A->Prps = Allocate (A, PAGE16);
  if (!A->Bounce || !A->Prps) goto Fail;
  CONST struct ntasi_asc_ops AscOps = {
    .cpu_read32=CpuRead, .cpu_write32=CpuWrite, .mailbox_read32=MailRead,
    .mailbox_read64=MailRead64, .mailbox_write64=MailWrite64,
    .dma_read_barrier=Barrier, .dma_write_barrier=Barrier, .service=Idle
  };
  CONST struct ntasi_rtkit_runtime_ops RtOps = {
    .allocate_shared=SharedAllocate, .release_shared=SharedRelease, .crashed=Crash, .poll_idle=Idle
  };
  CONST struct ntasi_ans_controller_ops NvOps = {
    .read32=NvRead, .write32=NvWrite, .dma_read_barrier=Barrier,
    .dma_write_barrier=Barrier, .service=Service, .configure_queues=Queues,
    .map_command=Map, .unmap_command=Unmap
  };
  if (ntasi_asc_init_variant (&A->Asc, &AscOps, &ntasi_asc_hw_v4, A, POLL_LIMIT) ||
      ntasi_rtkit_runtime_init (&A->Rtkit, &A->Asc, &RtOps, A, POLL_LIMIT)) goto Fail;
  A->Rtkit.booted = true; A->Rtkit.iop_power = A->Rtkit.ap_power = NTASI_RTKIT_POWER_ON;
  Status = A->Interrupt->DisableInterruptSource (A->Interrupt, ANS_IRQ);
  if (EFI_ERROR (Status)) goto Fail;
  Rc = ntasi_ans_controller_start_variant (&A->Controller, &NvOps, &ntasi_ans_hw_t8142,
                                          A, 64, POLL_LIMIT, &A->Admin, &A->Io);
  if (Rc || A->Fatal) goto Fail;
  if (ntasi_ans_block_device_init (&A->Block, Execute, A, A->Bounce->Cpu, A->Bounce->Dma, MAX_TRANSFER) ||
      ntasi_ans_block_identify (&A->Block, 1)) goto Fail;
  if (A->Block.media.block_size > MAX_TRANSFER || MAX_TRANSFER % A->Block.media.block_size) goto Fail;
  A->Media = (EFI_BLOCK_IO_MEDIA) {
    .MediaId=1, .MediaPresent=TRUE, .ReadOnly=TRUE, .BlockSize=A->Block.media.block_size,
    .IoAlign=1, .LastBlock=A->Block.media.block_count-1, .LogicalBlocksPerPhysicalBlock=1,
    .OptimalTransferLengthGranularity=MAX_TRANSFER / A->Block.media.block_size
  };
  A->Protocol = (EFI_BLOCK_IO_PROTOCOL) {EFI_BLOCK_IO_PROTOCOL_REVISION3, &A->Media, Reset, Read, Write, Flush};
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Exit, A,
                             &gEfiEventExitBootServicesGuid, &A->ExitEvent);
  if (EFI_ERROR (Status)) goto Fail;
  Status = gBS->InstallMultipleProtocolInterfaces (&A->Handle, &gEfiBlockIoProtocolGuid, &A->Protocol,
                                                   &gEfiDevicePathProtocolGuid, &mPath, NULL);
  if (EFI_ERROR (Status)) { gBS->CloseEvent (A->ExitEvent); goto Fail; }
  DEBUG ((DEBUG_INFO, "J714_ANS: namespace 1 blocks=%Lu blocksize=%u, read-only UEFI media\n",
          A->Block.media.block_count, A->Media.BlockSize));
  return EFI_SUCCESS;
Fail:
  A->Fatal = TRUE;
  DEBUG ((DEBUG_ERROR, "J714_ANS: failed stage=%u op=%u reg=%x value=%x pinned=%u\n",
          A->Controller.last_substage, A->Controller.failure_operation,
          A->Controller.failure_register, A->Controller.failure_observed, A->Pinned));
  if (!A->Published && !A->Pinned) {
    for (UINTN I=0; I<A->NumBuffers; I++)
      FreeAlignedPages (A->Buffers[I].Cpu, EFI_SIZE_TO_PAGES (A->Buffers[I].Size));
    FreeAlignedPages (A->Batch, EFI_SIZE_TO_PAGES (PAGE16));
  }
  return EFI_DEVICE_ERROR;
}

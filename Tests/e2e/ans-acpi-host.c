/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute the production publisher; ADT/AML/PMGR are the modeled boundary. */
#include "memory-peim-host.h"
#include <Drivers/AppleAnsHardware.h>
#define EFI_NOT_FOUND 14
#define EFI_DEVICE_ERROR 7
#define EFI_UNSUPPORTED 3
#define MAX_UINT8 UINT8_MAX
#define APPLE_ANS_PMGR_RESET_SIZE 4
#define APPLE_ANS_ACPI_OEM_ID "APPLE "
#define APPLE_ANS_ACPI_OEM_TABLE_ID "ANS "
typedef void *AML_ROOT_NODE_HANDLE;
typedef void *AML_OBJECT_NODE_HANDLE;
typedef struct { UINT32 Length; } EFI_ACPI_DESCRIPTION_HEADER;
typedef struct { int id; } dt_node_t;
typedef struct EFI_ACPI_TABLE_PROTOCOL EFI_ACPI_TABLE_PROTOCOL;
struct EFI_ACPI_TABLE_PROTOCOL {
  EFI_STATUS (*InstallAcpiTable)(EFI_ACPI_TABLE_PROTOCOL *, void *, UINTN, UINTN *);
};
static dt_node_t Ans = {1}, Sart = {2}, Root = {3};
static bool Published, Missing;
static unsigned CtrlReads, MemoryCount, PropertyCount;
static UINT64 Memory[4][2], CtrlBase = 0x4c5cc0000ULL, CtrlSize = 0x10000;
static UINT32 SartVersion = 3, InterruptIndex = 4, Interrupts[5] = {0, 0, 0, 0, 1155};
static const char *HardwareId;
static UINT64 Sta, Cca, Irq;
static const char *mAppleAnsAcpiTag = "host";
static EFI_GUID gAppleAnsDsdPropertiesGuid;
static bool PcdAppleAnsPublishAcpiDevice = true;
static UINT32 PcdAppleAnsPublishedInterrupt = 996, PcdAppleAnsExpectedPhysicalInterrupt = 1155;
static UINT64 PcdAppleAnsPmgrResetBase = 0x380700300ULL;
static UINT64 PcdAppleAnsPmgrApcieStBase = 0x380700410ULL;
static UINT64 PcdAppleAnsPmgrApcieStSysBase = 0x380700520ULL;
static UINT64 PcdAppleAnsPmgrApcieSt1SysBase;
#define FixedPcdGetBool(x) (x)
#define FixedPcdGet32(x) (x)
static dt_node_t *dt_get(const char *path) {
  if (!strcmp(path, "/arm-io/ans")) return &Ans;
  if (!strcmp(path, "/arm-io/sart-ans")) return &Sart;
  if (!strcmp(path, "/")) return &Root;
  return NULL;
}
static int dt_node_reg(dt_node_t *Node, int Index, UINT64 *Base, UINT64 *Size) {
  if (Node == &Sart && Index == 0) { *Base = 0x485c50000ULL; *Size = 0xc000; return 0; }
  if (Node == &Ans && Index == 0) { *Base = 0x481600000ULL; *Size = 0x88000; return 0; }
  if (Node == &Ans && Index == 3) { *Base = 0x485cc0000ULL; *Size = 0x60000; return 0; }
  if (Node == &Ans && Index == 9) {
    ++CtrlReads; if (Missing) return -1; *Base = CtrlBase; *Size = CtrlSize; return 0;
  }
  return -1;
}
static void *dt_node_prop(dt_node_t *Node, const char *Name, UINTN *Size) {
  (void)Node;
  if (!strcmp(Name, "sart-version")) { *Size = 4; return &SartVersion; }
  if (!strcmp(Name, "nvme-interrupt-idx")) { *Size = 4; return &InterruptIndex; }
  if (!strcmp(Name, "interrupts")) { *Size = sizeof(Interrupts); return Interrupts; }
  *Size = 0; return NULL;
}
static bool AppleAnsPropertyContains(dt_node_t *Node, const char *Property, const char *Needle) {
  (void)Node; (void)Property; (void)Needle; return false;
}
static EFI_STATUS AppleAnsPmgrResolveDomain(const char *Tag, const char *Name, UINT64 *Base) {
  (void)Tag;
  *Base = !strcmp(Name, "APCIE_ST") ? PcdAppleAnsPmgrApcieStBase : 0;
  return *Base ? EFI_SUCCESS : EFI_NOT_FOUND;
}
static EFI_STATUS AppleAnsPmgrSelectDomain(const char *Tag, const char *First,
  const char *Second, const char **Selected, UINT64 *Base) {
  (void)Tag; (void)Second; *Selected = First;
  *Base = !strcmp(First, "ANS2") ? PcdAppleAnsPmgrResetBase : PcdAppleAnsPmgrApcieStSysBase;
  return EFI_SUCCESS;
}
static EFI_STATUS AmlCodeGenDefinitionBlock(const char *Sig, const char *Oem,
  const char *Id, UINT32 Rev, void **Out) { (void)Sig; (void)Oem; (void)Id; (void)Rev; *Out = &Root; return 0; }
static EFI_STATUS AmlCodeGenScope(const char *Name, void *Parent, void **Out) {
  (void)Name; (void)Parent; *Out = &Root; return 0;
}
static EFI_STATUS AmlCodeGenDevice(const char *Name, void *Parent, void **Out) {
  assert(!strcmp(Name, "ANS0")); (void)Parent; *Out = &Root; return 0;
}
static EFI_STATUS AmlCodeGenNameString(const char *Name, const char *Value, void *Parent, void **Out) {
  (void)Parent; (void)Out; assert(!strcmp(Name, "_HID")); HardwareId = Value; return 0;
}
static EFI_STATUS AmlCodeGenNameInteger(const char *Name, UINT64 Value, void *Parent, void **Out) {
  (void)Parent; (void)Out; if (!strcmp(Name, "_STA")) Sta = Value; if (!strcmp(Name, "_CCA")) Cca = Value; return 0;
}
static EFI_STATUS AmlCodeGenNameResourceTemplate(const char *Name, void *Parent, void **Out) {
  (void)Name; (void)Parent; *Out = &Root; return 0;
}
static EFI_STATUS AppleAnsAddMemoryResource(void *Node, UINT64 Base, UINT64 Size) {
  (void)Node; assert(MemoryCount < 4); Memory[MemoryCount][0] = Base; Memory[MemoryCount++][1] = Size; return 0;
}
static EFI_STATUS AmlCodeGenRdInterrupt(bool Consumer, bool Edge, bool Low, bool Shared,
  UINT32 *Interrupt, UINT32 Count, void *Node, void **Out) {
  assert(Consumer && !Edge && !Low && !Shared && Count == 1); (void)Node; (void)Out; Irq = *Interrupt; return 0;
}
static EFI_STATUS AmlCodeGenNamePackage(const char *Name, void *Parent, void **Out) {
  (void)Name; (void)Parent; *Out = &Root; return 0;
}
static EFI_STATUS AmlAddDeviceDataDescriptorPackage(EFI_GUID *Guid, void *Parent, void **Out) {
  (void)Guid; (void)Parent; *Out = &Root; return 0;
}
static EFI_STATUS AmlAddNameIntegerPackage(const char *Name, UINT64 Value, void *Parent) {
  (void)Name; (void)Value; (void)Parent; ++PropertyCount; return 0;
}
static EFI_STATUS AmlSerializeDefinitionBlock(void *Node, EFI_ACPI_DESCRIPTION_HEADER **Table) {
  (void)Node; *Table = calloc(1, sizeof(**Table)); assert(*Table); return 0;
}
static EFI_STATUS AmlDeleteTree(void *Node) { (void)Node; return 0; }
static void FreePool(void *Ptr) { free(Ptr); }
static EFI_STATUS Install(EFI_ACPI_TABLE_PROTOCOL *This, void *Table, UINTN Size, UINTN *Handle) {
  (void)This; (void)Table; (void)Size; (void)Handle; Published = true; return 0;
}
#include "publication.inc"
int main(int argc, char **argv) {
  assert(argc == 2);
  if (!strcmp(argv[1], "missing")) Missing = true;
  if (!strcmp(argv[1], "zero")) CtrlBase = 0;
  if (!strcmp(argv[1], "short")) CtrlSize = 0xf000;
  if (!strcmp(argv[1], "unaligned-base")) CtrlBase++;
  if (!strcmp(argv[1], "unaligned-size")) CtrlSize++;
  if (!strcmp(argv[1], "wrap")) CtrlBase = UINT64_MAX - 0xfff;
  if (!strcmp(argv[1], "overlap-asc")) CtrlBase = 0x481600000ULL;
  if (!strcmp(argv[1], "overlap-ans")) CtrlBase = 0x485cc0000ULL;
  if (!strcmp(argv[1], "overlap-sart")) { CtrlBase = 0x485c50000ULL; }
  if (!strcmp(argv[1], "sart2")) SartVersion = 2;
  if (!strcmp(argv[1], "disabled")) PcdAppleAnsPublishAcpiDevice = false;
  EFI_ACPI_TABLE_PROTOCOL Protocol = {Install};
  EFI_STATUS Status = AcpiPlatformInstallAppleAnsTable(&Protocol);
  printf("{\"status\":%d,\"published\":%s,\"ctrl_reads\":%u,\"hid\":\"%s\",\"sta\":%llu,\"cca\":%llu,\"irq\":%llu,\"dsd_count\":%u,\"memory\":[",
    Status, Published ? "true" : "false", CtrlReads, HardwareId ? HardwareId : "",
    (unsigned long long)Sta, (unsigned long long)Cca, (unsigned long long)Irq, PropertyCount);
  for (unsigned i = 0; i < MemoryCount; ++i) printf("%s[%llu,%llu]", i ? "," : "",
    (unsigned long long)Memory[i][0], (unsigned long long)Memory[i][1]);
  puts("]}"); return 0;
}

/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual compiled MTP table and publication gate; ADT property is modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STATIC static
#define IN
#define CONST const
#define VOID void
#define TRUE true
#define FALSE false
#define DEBUG(x) ((void)0)
#define DEBUG_INFO 0
#define DEBUG_ERROR 0
#define CompareMem memcmp
#define CopyMem memcpy
#define SIGNATURE_32(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
typedef bool BOOLEAN;
typedef size_t UINTN;
typedef uint32_t UINT32;
typedef struct __attribute__((packed)) {
    uint32_t Signature, Length;
    uint8_t Revision, Checksum, OemId[6];
    uint64_t OemTableId;
    uint32_t OemRevision, CreatorId, CreatorRevision;
} EFI_ACPI_DESCRIPTION_HEADER;
static uint32_t Property[2];
static size_t PropertyLength;
static bool Present, Freed;
static const void *dt_get_prop(const char *path, const char *name, size_t *length) {
    assert(!strcmp(path,"/chosen"));
    assert(!strcmp(name,"aura,windows-mtp-disabled"));
    *length=PropertyLength;
    return Present ? Property : NULL;
}
static bool AppleN1TableAdmitted(const EFI_ACPI_DESCRIPTION_HEADER *table, UINTN size) {
    (void)table; (void)size; return true;
}
static void FreePool(void *p) { (void)p; Freed=true; }
static const struct { void (*FreePool)(void *); } Services={FreePool}, *gBS=&Services;
#include "mtp-gate.inc"
int main(int argc, char **argv) {
    assert(argc==3);
    unsigned char bytes[8192]; FILE *f=fopen(argv[1],"rb"); assert(f);
    UINTN Size=fread(bytes,1,sizeof(bytes),f); fclose(f);
    EFI_ACPI_DESCRIPTION_HEADER *header=(void *)bytes;
    assert(Size>=sizeof(*header) && Size==header->Length);
    int test=atoi(argv[2]); Present=test!=0; PropertyLength=4;
    Property[0]=test==1?0:1;
    if(test==3) PropertyLength=1;
    if(test==4) PropertyLength=8;
    if(test==5) Property[0]=2;
    if(test==6) memcpy(&header->OemTableId,"OTHERMTP",8);
    if(test==7) header->Signature=SIGNATURE_32('D','S','D','T');
    if(test==8) PropertyLength=0;
    VOID *CurrentTable=bytes;
    int Published=0;
    for (int Instance=0;Instance<1;Instance++) {
#include "mtp-publication.inc"
        Published++;
    }
    printf("{\"case\":%d,\"published\":%d,\"freed\":%s}\n",test,Published,Freed?"true":"false");
}

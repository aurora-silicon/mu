/* SPDX-License-Identifier: MIT */
/*
 * Host harness for AcpiPlatformDxe/AuraJ813DisplayDevice.c. Feeds the real
 * DISP.aml (compiled by run.py with iasl) through
 * AppleJ813DisplayTableAdmitted() with a fixture ADT, one fault per case.
 * The table is never patched, so every admitted copy must equal the input.
 *
 * usage: harness <DISP.aml>
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/AppleDTLib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BOOLEAN AppleJ813DisplayTableAdmitted(const EFI_ACPI_DESCRIPTION_HEADER *Table,
                                      UINTN Size);

/* No /chosen aura,windows-unified-display-v1 marker: AuraJ813UnifiedDisplay.c
 * keeps the legacy AURA8180 selection this harness checks. */
dt_node_t *dt_get(const char *name)
{
    (void)name;
    return NULL;
}

void *dt_prop(dt_node_t *node, const char *key, size_t *lenp)
{
    (void)node;
    (void)key;
    (void)lenp;
    return NULL;
}

static int checks, failures;
static void check(int ok, const char *what)
{
    checks++;
    failures += !ok;
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

void HostDebugPrint(UINTN level, const char *format, ...)
{
    char fixed[512];
    size_t j = 0;
    va_list ap;
    (void)level;
    for (size_t i = 0; format[i] && j + 2 < sizeof(fixed); i++)
    {
        fixed[j++] = format[i];
        if (format[i] == '%' && format[i + 1] == 'a')
            fixed[j++] = 's', i++;
    }
    fixed[j] = 0;
    va_start(ap, format);
    printf("  log: ");
    vprintf(fixed, ap);
    va_end(ap);
}

/* ---- Fixture ADT ---- */
struct prop
{
    const char *node, *name;
    uint32_t value;
    int width;
    int present;
};
static struct prop props[] = {
    {"/chosen", "chip-id", 0x8142, 4, 1},
    {"/chosen", "board-id", 0x24, 4, 1},
    {"/arm-io/dcp", "compatible", 0x00706364, 4, 1},   /* "dcp" */
    {"/arm-io/disp0", "compatible", 0x70736964, 4, 1}, /* "disp" */
};
#define NPROPS (sizeof(props) / sizeof(props[0]))
static struct prop saved[NPROPS];
static uint8_t scratch[NPROPS][4];

void *dt_get_prop(const char *device, const char *prop, size_t *size)
{
    for (size_t i = 0; i < NPROPS; i++)
        if (props[i].present && !strcmp(device, props[i].node) && !strcmp(prop, props[i].name))
        {
            memcpy(scratch[i], &props[i].value, 4);
            *size = (size_t)props[i].width;
            return scratch[i];
        }
    return NULL;
}

#if HOST_PCD_PcdAppleJ813DisplayDevice
static struct prop *find(const char *node, const char *name)
{
    for (size_t i = 0; i < NPROPS; i++)
        if (!strcmp(props[i].node, node) && !strcmp(props[i].name, name))
            return &props[i];
    abort();
}
#endif

static uint8_t *aml;
static size_t aml_size;
static int try_table(uint8_t *copy, size_t size)
{
    memcpy(copy, aml, aml_size);
    return AppleJ813DisplayTableAdmitted((EFI_ACPI_DESCRIPTION_HEADER *)copy, size);
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *copy;
    if (argc != 2 || !(f = fopen(argv[1], "rb")))
        return 2;
    fseek(f, 0, SEEK_END);
    aml_size = (size_t)ftell(f);
    rewind(f);
    aml = malloc(aml_size);
    copy = malloc(aml_size);
    if (fread(aml, 1, aml_size, f) != aml_size)
        return 2;
    fclose(f);
    memcpy(saved, props, sizeof(props));

#if HOST_PCD_PcdAppleJ813DisplayDevice
    check(try_table(copy, aml_size) == 1 && memcmp(copy, aml, aml_size) == 0,
          "J813 with ADT dcp/disp0: admitted, unchanged");

#define CASE(mutation, what)                          \
    do                                                \
    {                                                 \
        memcpy(props, saved, sizeof(props));          \
        mutation;                                     \
        check(try_table(copy, aml_size) == 0, what);  \
    } while (0)
    CASE(find("/chosen", "chip-id")->value = 0x8152, "other chip: withheld");
    CASE(find("/chosen", "board-id")->value = 0x22, "other board: withheld");
    CASE(find("/chosen", "chip-id")->present = 0, "no chip-id: withheld");
    CASE(find("/chosen", "board-id")->width = 8, "malformed board-id: withheld");
    CASE(find("/arm-io/dcp", "compatible")->present = 0, "no ADT dcp: withheld");
    CASE(find("/arm-io/disp0", "compatible")->present = 0, "no ADT disp0: withheld");
    CASE(find("/arm-io/dcp", "compatible")->width = 0, "empty dcp compatible: withheld");
    memcpy(props, saved, sizeof(props));
    check(try_table(copy, aml_size - 1) == 0, "table longer than buffer: withheld");
    {
        /* Any other table passes through untouched. */
        EFI_ACPI_DESCRIPTION_HEADER other = {0};
        memcpy(&other.Signature, "SSDT", 4);
        memcpy(&other.OemTableId, "AURA817G", 8);
        other.Length = sizeof(other);
        props[0].value = 0x8103; /* even on another machine */
        check(AppleJ813DisplayTableAdmitted(&other, sizeof(other)) == 1,
              "other tables pass through");
    }
#else
    check(try_table(copy, aml_size) == 0 && memcmp(copy, aml, aml_size) == 0,
          "PcdAppleJ813DisplayDevice FALSE: withheld and untouched");
#endif
    printf("RESULT %s %d\n", failures ? "FAIL" : "PASS", checks);
    return failures != 0;
}

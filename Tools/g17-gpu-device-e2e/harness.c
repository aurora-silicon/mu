/* SPDX-License-Identifier: MIT */
/*
 * Host harness for AcpiPlatformDxe/AuraG17GpuDevice.c. Feeds the real, built
 * GPU.aml through AppleG17GpuTableAdmitted() with a fixture ADT (/chosen,
 * /arm-io/sgx) and a fixture GCD map, one fault per case.
 *
 * usage: harness <GPU.aml> <patched-output.aml>
 */
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Library/DxeServicesTableLib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BOOLEAN AppleG17GpuTableAdmitted(EFI_ACPI_DESCRIPTION_HEADER *Table, UINTN Size);

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

UINTN AsciiSPrint(CHAR8 *buffer, UINTN size, const CHAR8 *format, ...)
{
    char fixed[256];
    size_t j = 0;
    va_list ap;
    int n;
    for (size_t i = 0; format[i] && j + 2 < sizeof(fixed); i++)
    {
        fixed[j++] = format[i];
        if (format[i] == '%' && format[i + 1] == 'a')
            fixed[j++] = 's', i++;
    }
    fixed[j] = 0;
    va_start(ap, format);
    n = vsnprintf(buffer, size, fixed, ap);
    va_end(ap);
    return n < 0 ? 0 : (UINTN)n;
}

/* ---- Fixture ADT (values from the saved J813 resident ADT) ---- */
struct prop
{
    const char *node, *name;
    uint64_t value;
    int width;
    int present;
};
static struct prop props[] = {
    {"/chosen", "chip-id", 0x8142, 4, 1},
    {"/chosen", "board-id", 0x24, 4, 1},
    {"/arm-io/sgx", "gfx-handoff-base", 0x103fff30000ULL, 8, 1},
    {"/arm-io/sgx", "gfx-handoff-size", 0x4000, 8, 1},
    {"/arm-io/sgx", "gpu-region-base", 0x103fffb8000ULL, 8, 1},
    {"/arm-io/sgx", "gpu-region-size", 0x4000, 8, 1},
    {"/arm-io/sgx", "gfx-shared-region-base", 0x103fff38000ULL, 8, 1},
    {"/arm-io/sgx", "gfx-shared-region-size", 0x80000, 8, 1},
    {"/arm-io/sgx", "gfx-shared-l2-region-base", 0x103fff34000ULL, 8, 1},
    {"/arm-io/sgx", "gfx-shared-l2-region-size", 0x4000, 8, 1},
    {"/arm-io/sgx", "gfx-data-base", 0x1000248c000ULL, 8, 1},
    {"/arm-io/sgx", "gfx-data-size", 0x134000, 8, 1},
    {"/arm-io/sgx", "gfx1-data-base", 0x100025c0000ULL, 8, 1},
    {"/arm-io/sgx", "gfx1-data-size", 0x130000, 8, 1},
};
#define NPROPS (sizeof(props) / sizeof(props[0]))
static struct prop saved[NPROPS];
static uint8_t scratch[NPROPS][8];

void *dt_get_prop(const char *device, const char *prop, size_t *size)
{
    for (size_t i = 0; i < NPROPS; i++)
        if (props[i].present && !strcmp(device, props[i].node) && !strcmp(prop, props[i].name))
        {
            memcpy(scratch[i], &props[i].value, 8);
            *size = (size_t)props[i].width;
            return scratch[i];
        }
    return NULL;
}
#if HOST_PCD_G17_GPU_PROBE
static struct prop *find(const char *name)
{
    for (size_t i = 0; i < NPROPS; i++)
        if (!strcmp(props[i].name, name))
            return &props[i];
    abort();
}
#endif

/* ---- Fixture GCD map: Mu system RAM of the saved boot ---- */
static uint64_t ram_base = 0x100196d8000ULL, ram_end = 0x103dc600000ULL;
static EFI_STATUS map(UINTN *count, EFI_GCD_MEMORY_SPACE_DESCRIPTOR **out)
{
    EFI_GCD_MEMORY_SPACE_DESCRIPTOR *m = calloc(3, sizeof(*m));
    m[0] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){0x10000000000ULL, ram_base - 0x10000000000ULL, 0, 0,
                                             EfiGcdMemoryTypeNonExistent};
    m[1] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){ram_base, ram_end - ram_base, 0, 0,
                                             EfiGcdMemoryTypeSystemMemory};
    m[2] = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){0x580000000ULL, 0x10000000ULL, 0, 0,
                                             EfiGcdMemoryTypeMemoryMappedIo};
    *count = 3;
    *out = m;
    return EFI_SUCCESS;
}
static HOST_DXE_SERVICES services = {map};
HOST_DXE_SERVICES *gDS = &services;

static uint8_t *aml;
static size_t aml_size;
static int try_table(uint8_t *copy)
{
    memcpy(copy, aml, aml_size);
    return AppleG17GpuTableAdmitted((EFI_ACPI_DESCRIPTION_HEADER *)copy, aml_size);
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *copy;
    if (argc != 3 || !(f = fopen(argv[1], "rb")))
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

#if HOST_PCD_G17_GPU_PROBE
    check(try_table(copy) == 1 && memcmp(copy, aml, aml_size) != 0, "valid J813 regions: admitted and patched");
    f = fopen(argv[2], "wb");
    fwrite(copy, 1, aml_size, f);
    fclose(f);

#define CASE(mutation, what)                         \
    do                                               \
    {                                                \
        memcpy(props, saved, sizeof(props));         \
        mutation;                                    \
        check(try_table(copy) == 0, what);           \
    } while (0)
    CASE(find("chip-id")->value = 0x8152, "other chip: withheld");
    CASE(find("board-id")->value = 0x22, "other board: withheld");
    CASE(find("gpu-region-base")->present = 0, "missing region: withheld");
    CASE(find("gfx-data-size")->value = 0x130000, "wrong DATA size: withheld");
    CASE(find("gfx-shared-region-size")->value = 0x40000, "short page-table region: withheld");
    CASE(find("gfx-handoff-base")->value += 0x1000, "misaligned region: withheld");
    CASE(find("gfx-shared-l2-region-base")->value = 0x103fff38000ULL, "overlapping regions: withheld");
    CASE(find("gfx-shared-region-base")->value = 0x103dc000000ULL, "region inside system RAM: withheld");
    CASE(find("gfx-data-base")->width = 4, "malformed property width: withheld");
    memcpy(props, saved, sizeof(props));
    {
        /* Any other table passes through untouched. */
        EFI_ACPI_DESCRIPTION_HEADER other = {0};
        memcpy(&other.Signature, "SSDT", 4);
        memcpy(&other.OemTableId, "J813CPUF", 8);
        other.Length = sizeof(other);
        check(AppleG17GpuTableAdmitted(&other, sizeof(other)) == 1, "other tables pass through");
    }
#else
    check(try_table(copy) == 0 && memcmp(copy, aml, aml_size) == 0,
          "PcdAppleG17GpuProbe FALSE: withheld and untouched");
    (void)argv;
#endif
    printf("RESULT %s %d\n", failures ? "FAIL" : "PASS", checks);
    return failures != 0;
}

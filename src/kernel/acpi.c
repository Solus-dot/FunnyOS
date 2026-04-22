#include "acpi.h"
#include "kstring.h"

#define ACPI_RSDP_SIGNATURE_0 'R'
#define ACPI_RSDP_SIGNATURE_1 'S'
#define ACPI_RSDP_SIGNATURE_2 'D'
#define ACPI_RSDP_SIGNATURE_3 ' '
#define ACPI_RSDP_SIGNATURE_4 'P'
#define ACPI_RSDP_SIGNATURE_5 'T'
#define ACPI_RSDP_SIGNATURE_6 'R'
#define ACPI_RSDP_SIGNATURE_7 ' '

#define ACPI_SIG_RSDT 0x54445352u
#define ACPI_SIG_XSDT 0x54445358u
#define ACPI_SIG_FADT 0x50434146u
#define ACPI_SIG_MADT 0x43495041u

#define ACPI_MADT_ENTRY_IOAPIC 1u
#define ACPI_MAX_MADT_SCAN_BYTES 4096u
#define ACPI_MAX_IDENTITY_ADDRESS 0x0000000100000000ull

typedef struct __attribute__((packed)) AcpiRsdpV1 {
    uint8_t signature[8];
    uint8_t checksum;
    uint8_t oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} AcpiRsdpV1;

typedef struct __attribute__((packed)) AcpiRsdpV2 {
    AcpiRsdpV1 first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} AcpiRsdpV2;

typedef struct __attribute__((packed)) AcpiSdtHeader {
    uint32_t signature;
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oem_id[6];
    uint8_t oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} AcpiSdtHeader;

typedef struct __attribute__((packed)) AcpiMadt {
    AcpiSdtHeader header;
    uint32_t local_apic_address;
    uint32_t flags;
} AcpiMadt;

typedef struct __attribute__((packed)) AcpiMadtEntryHeader {
    uint8_t type;
    uint8_t length;
} AcpiMadtEntryHeader;

typedef struct __attribute__((packed)) AcpiMadtIoApic {
    AcpiMadtEntryHeader header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} AcpiMadtIoApic;

static AcpiPlatformInfo g_acpi = {0};

static bool acpi_memory_readable(uintptr_t address, size_t size)
{
    if (address == 0u || size == 0u)
        return false;
    if (address + size < address)
        return false;
    return (uint64_t)(address + size) <= ACPI_MAX_IDENTITY_ADDRESS;
}

static bool acpi_checksum_ok(const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint8_t sum = 0u;
    size_t i;

    if (data == NULL || size == 0u)
        return false;
    for (i = 0u; i < size; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0u;
}

static bool acpi_rsdp_signature_valid(const AcpiRsdpV1* rsdp)
{
    if (rsdp == NULL)
        return false;
    return rsdp->signature[0] == ACPI_RSDP_SIGNATURE_0
        && rsdp->signature[1] == ACPI_RSDP_SIGNATURE_1
        && rsdp->signature[2] == ACPI_RSDP_SIGNATURE_2
        && rsdp->signature[3] == ACPI_RSDP_SIGNATURE_3
        && rsdp->signature[4] == ACPI_RSDP_SIGNATURE_4
        && rsdp->signature[5] == ACPI_RSDP_SIGNATURE_5
        && rsdp->signature[6] == ACPI_RSDP_SIGNATURE_6
        && rsdp->signature[7] == ACPI_RSDP_SIGNATURE_7;
}

static const AcpiSdtHeader* acpi_validate_header(uintptr_t address, uint32_t expected_signature)
{
    const AcpiSdtHeader* header = (const AcpiSdtHeader*)address;

    if (!acpi_memory_readable(address, sizeof(AcpiSdtHeader)))
        return NULL;
    if (header->signature != expected_signature)
        return NULL;
    if (header->length < sizeof(AcpiSdtHeader))
        return NULL;
    if (!acpi_memory_readable(address, header->length))
        return NULL;
    if (!acpi_checksum_ok(header, header->length))
        return NULL;
    return header;
}

static void acpi_scan_madt(const AcpiMadt* madt)
{
    uintptr_t cursor;
    uintptr_t end;

    if (madt == NULL)
        return;

    g_acpi.lapic_address = madt->local_apic_address;
    cursor = (uintptr_t)madt + sizeof(AcpiMadt);
    end = (uintptr_t)madt + madt->header.length;
    if (end < cursor)
        return;
    if (end - cursor > ACPI_MAX_MADT_SCAN_BYTES)
        end = cursor + ACPI_MAX_MADT_SCAN_BYTES;

    while (cursor + sizeof(AcpiMadtEntryHeader) <= end) {
        const AcpiMadtEntryHeader* entry = (const AcpiMadtEntryHeader*)cursor;

        if (entry->length < sizeof(AcpiMadtEntryHeader))
            break;
        if (cursor + entry->length > end)
            break;
        if (entry->type == ACPI_MADT_ENTRY_IOAPIC && entry->length >= sizeof(AcpiMadtIoApic)) {
            const AcpiMadtIoApic* ioapic = (const AcpiMadtIoApic*)entry;

            g_acpi.ioapic_id = ioapic->ioapic_id;
            g_acpi.ioapic_address = ioapic->ioapic_address;
            g_acpi.ioapic_gsi_base = ioapic->global_system_interrupt_base;
            break;
        }

        cursor += entry->length;
    }
}

bool acpi_init(const BootInfo* boot_info)
{
    const AcpiRsdpV1* rsdp;
    uintptr_t sdt_root = 0u;
    bool use_xsdt = false;
    const AcpiSdtHeader* root;
    uintptr_t entries_base;
    uint32_t entries_count;
    uint32_t i;

    k_memset(&g_acpi, 0, sizeof(g_acpi));
    if (boot_info == NULL || boot_info->acpi_rsdp == 0u)
        return false;
    if (!acpi_memory_readable(boot_info->acpi_rsdp, sizeof(AcpiRsdpV1)))
        return false;

    rsdp = (const AcpiRsdpV1*)boot_info->acpi_rsdp;
    if (!acpi_rsdp_signature_valid(rsdp))
        return false;
    if (!acpi_checksum_ok(rsdp, sizeof(AcpiRsdpV1)))
        return false;

    if (rsdp->revision >= 2u) {
        const AcpiRsdpV2* rsdp2;

        if (!acpi_memory_readable(boot_info->acpi_rsdp, sizeof(AcpiRsdpV2)))
            return false;
        rsdp2 = (const AcpiRsdpV2*)boot_info->acpi_rsdp;
        if (rsdp2->length < sizeof(AcpiRsdpV2))
            return false;
        if (!acpi_memory_readable(boot_info->acpi_rsdp, rsdp2->length))
            return false;
        if (!acpi_checksum_ok(rsdp2, rsdp2->length))
            return false;
        if (rsdp2->xsdt_address != 0u && rsdp2->xsdt_address < ACPI_MAX_IDENTITY_ADDRESS) {
            sdt_root = (uintptr_t)rsdp2->xsdt_address;
            use_xsdt = true;
        }
    }

    if (sdt_root == 0u && rsdp->rsdt_address != 0u)
        sdt_root = (uintptr_t)rsdp->rsdt_address;
    if (sdt_root == 0u)
        return false;

    root = acpi_validate_header(sdt_root, use_xsdt ? ACPI_SIG_XSDT : ACPI_SIG_RSDT);
    if (root == NULL)
        return false;

    entries_base = sdt_root + sizeof(AcpiSdtHeader);
    entries_count = (root->length - sizeof(AcpiSdtHeader)) / (use_xsdt ? 8u : 4u);

    g_acpi.present = true;
    g_acpi.rsdp = boot_info->acpi_rsdp;
    g_acpi.sdt_root = sdt_root;

    for (i = 0u; i < entries_count; ++i) {
        uintptr_t table_addr;

        if (use_xsdt) {
            const uint64_t* entries = (const uint64_t*)entries_base;

            table_addr = (uintptr_t)entries[i];
            if (entries[i] >= ACPI_MAX_IDENTITY_ADDRESS)
                continue;
        } else {
            const uint32_t* entries = (const uint32_t*)entries_base;

            table_addr = (uintptr_t)entries[i];
        }

        if (!acpi_memory_readable(table_addr, sizeof(AcpiSdtHeader)))
            continue;

        if (((const AcpiSdtHeader*)table_addr)->signature == ACPI_SIG_MADT) {
            const AcpiMadt* madt = (const AcpiMadt*)acpi_validate_header(table_addr, ACPI_SIG_MADT);

            if (madt != NULL) {
                g_acpi.madt = table_addr;
                acpi_scan_madt(madt);
            }
        } else if (((const AcpiSdtHeader*)table_addr)->signature == ACPI_SIG_FADT) {
            if (acpi_validate_header(table_addr, ACPI_SIG_FADT) != NULL)
                g_acpi.fadt = table_addr;
        }
    }

    return g_acpi.madt != 0u && g_acpi.lapic_address != 0u;
}

const AcpiPlatformInfo* acpi_platform_info(void)
{
    return &g_acpi;
}

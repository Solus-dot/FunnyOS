#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu
#define PCI_INVALID_VENDOR_ID 0xFFFFu
#define PCI_MAX_DEVICE_COUNT 32u
#define PCI_MAX_FUNCTION_COUNT 8u
#define PCI_MAX_BAR_COUNT 6u

static bool g_pci_available = false;

static uint32_t pci_config_address(const PciAddress* address, uint8_t offset)
{
    return 0x80000000u
        | ((uint32_t)address->bus << 16u)
        | ((uint32_t)address->device << 11u)
        | ((uint32_t)address->function << 8u)
        | ((uint32_t)offset & 0xFCu);
}

bool pci_init(void)
{
    g_pci_available = true;
    return true;
}

bool pci_available(void)
{
    return g_pci_available;
}

uint8_t pci_read_config8(const PciAddress* address, uint8_t offset)
{
    if (!g_pci_available || address == NULL)
        return 0xFFu;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    return io_in8((uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 0x3u)));
}

uint16_t pci_read_config16(const PciAddress* address, uint8_t offset)
{
    if (!g_pci_available || address == NULL)
        return 0xFFFFu;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    return io_in16((uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 0x2u)));
}

uint32_t pci_read_config32(const PciAddress* address, uint8_t offset)
{
    if (!g_pci_available || address == NULL)
        return 0xFFFFFFFFu;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    return io_in32(PCI_CONFIG_DATA_PORT);
}

void pci_write_config8(const PciAddress* address, uint8_t offset, uint8_t value)
{
    if (!g_pci_available || address == NULL)
        return;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    io_out8((uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 0x3u)), value);
}

void pci_write_config16(const PciAddress* address, uint8_t offset, uint16_t value)
{
    if (!g_pci_available || address == NULL)
        return;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    io_out16((uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 0x2u)), value);
}

void pci_write_config32(const PciAddress* address, uint8_t offset, uint32_t value)
{
    if (!g_pci_available || address == NULL)
        return;

    io_out32(PCI_CONFIG_ADDRESS_PORT, pci_config_address(address, offset));
    io_out32(PCI_CONFIG_DATA_PORT, value);
}

bool pci_read_device_info(const PciAddress* address, PciDeviceInfo* out)
{
    uint16_t vendor_id;

    if (!g_pci_available || address == NULL || out == NULL)
        return false;
    if (address->device >= PCI_MAX_DEVICE_COUNT || address->function >= PCI_MAX_FUNCTION_COUNT)
        return false;

    vendor_id = pci_read_config16(address, 0x00u);
    if (vendor_id == PCI_INVALID_VENDOR_ID)
        return false;

    out->address = *address;
    out->vendor_id = vendor_id;
    out->device_id = pci_read_config16(address, 0x02u);
    out->revision_id = pci_read_config8(address, 0x08u);
    out->prog_if = pci_read_config8(address, 0x09u);
    out->subclass = pci_read_config8(address, 0x0Au);
    out->class_code = pci_read_config8(address, 0x0Bu);
    out->header_type = pci_read_config8(address, 0x0Eu);
    return true;
}

uint32_t pci_read_bar(const PciAddress* address, uint8_t bar_index)
{
    if (!g_pci_available || address == NULL || bar_index >= PCI_MAX_BAR_COUNT)
        return 0u;

    return pci_read_config32(address, (uint8_t)(0x10u + bar_index * 4u));
}

uint64_t pci_read_bar64(const PciAddress* address, uint8_t bar_index)
{
    uint32_t low;
    uint32_t high = 0u;
    bool is_64_bit;

    if (!g_pci_available || address == NULL || bar_index >= PCI_MAX_BAR_COUNT)
        return 0u;

    low = pci_read_bar(address, bar_index);
    if ((low & 0x1u) != 0u)
        return low;
    is_64_bit = (low & 0x6u) == 0x4u;
    if (is_64_bit && bar_index + 1u >= PCI_MAX_BAR_COUNT)
        return 0u;
    if (is_64_bit)
        high = pci_read_bar(address, (uint8_t)(bar_index + 1u));

    return ((uint64_t)high << 32u) | (uint64_t)low;
}

void pci_enumerate(PciVisitFn visit, void* context)
{
    uint16_t bus;

    if (!g_pci_available || visit == NULL)
        return;

    for (bus = 0u; bus <= 255u; ++bus) {
        uint8_t device;

        for (device = 0u; device < PCI_MAX_DEVICE_COUNT; ++device) {
            PciAddress address = {(uint8_t)bus, device, 0u};
            PciDeviceInfo info;
            uint8_t function_limit = 1u;
            uint8_t function;

            if (!pci_read_device_info(&address, &info))
                continue;

            if (!visit(&info, context))
                return;
            if ((info.header_type & 0x80u) != 0u)
                function_limit = PCI_MAX_FUNCTION_COUNT;

            for (function = 1u; function < function_limit; ++function) {
                address.function = function;
                if (!pci_read_device_info(&address, &info))
                    continue;
                if (!visit(&info, context))
                    return;
            }
        }
    }
}

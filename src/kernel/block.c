#include "block.h"
#include "ahci.h"
#include "ata.h"
#include "driver.h"
#include "pci.h"

#define BLOCK_MAX_PCI_PATH_DEPTH 8u
#define DEVICE_PATH_TYPE_HARDWARE 0x01u
#define DEVICE_PATH_TYPE_MESSAGING 0x03u
#define DEVICE_PATH_TYPE_END 0x7Fu
#define DEVICE_PATH_SUBTYPE_PCI 0x01u
#define DEVICE_PATH_SUBTYPE_ATAPI 0x01u
#define DEVICE_PATH_SUBTYPE_SATA 0x12u
#define DEVICE_PATH_SUBTYPE_END_ENTIRE 0xFFu

typedef struct DevicePathNode {
    uint8_t type;
    uint8_t subtype;
    uint8_t length[2];
} DevicePathNode;

typedef struct AtapiDevicePath {
    DevicePathNode header;
    uint8_t primary_secondary;
    uint8_t slave_master;
    uint16_t lun;
} AtapiDevicePath;

typedef struct PciDevicePath {
    DevicePathNode header;
    uint8_t function;
    uint8_t device;
} PciDevicePath;

typedef struct SataDevicePath {
    DevicePathNode header;
    uint16_t hba_port_number;
    uint16_t port_multiplier_port_number;
    uint16_t lun;
} SataDevicePath;

typedef struct BlockPciPathHop {
    uint8_t device;
    uint8_t function;
} BlockPciPathHop;

typedef enum BlockPathParseResult {
    BLOCK_PATH_PARSE_INVALID = 0u,
    BLOCK_PATH_PARSE_ABSENT = 1u,
    BLOCK_PATH_PARSE_FOUND = 2u
} BlockPathParseResult;

typedef bool (*BlockReadFn)(uint32_t lba, uint8_t count, void* out);
typedef bool (*BlockWriteFn)(uint32_t lba, uint8_t count, const void* data);

typedef struct BlockDevice {
    BlockBackendKind backend;
    BlockReadFn read_sectors;
    BlockWriteFn write_sectors;
} BlockDevice;

static BlockDevice g_block_device = {0};

#define BLOCK_DRIVER_KIND_AHCI 1u
#define BLOCK_DRIVER_KIND_ATA 2u

#define BLOCK_PAYLOAD_KIND 0u
#define BLOCK_PAYLOAD_0 1u
#define BLOCK_PAYLOAD_1 2u
#define BLOCK_PAYLOAD_2 3u

static bool block_driver_match_ahci(const DriverDevice* device)
{
    return device != NULL
        && device->class_type == DRIVER_DEVICE_CLASS_BLOCK
        && device->payload[BLOCK_PAYLOAD_KIND] == BLOCK_DRIVER_KIND_AHCI;
}

static bool block_driver_probe_ahci(const DriverDevice* device)
{
    AhciDeviceAddress address;

    if (device == NULL)
        return false;

    address.controller.bus = (uint8_t)device->payload[BLOCK_PAYLOAD_0];
    address.controller.device = (uint8_t)device->payload[BLOCK_PAYLOAD_1];
    address.controller.function = (uint8_t)device->payload[BLOCK_PAYLOAD_2];
    address.port = (uint8_t)device->prog_if;
    if (!ahci_init(&address))
        return false;

    g_block_device.backend = BLOCK_BACKEND_AHCI;
    g_block_device.read_sectors = ahci_read_sectors;
    g_block_device.write_sectors = ahci_write_sectors;
    return true;
}

static bool block_driver_match_ata(const DriverDevice* device)
{
    return device != NULL
        && device->class_type == DRIVER_DEVICE_CLASS_BLOCK
        && device->payload[BLOCK_PAYLOAD_KIND] == BLOCK_DRIVER_KIND_ATA;
}

static bool block_driver_probe_ata(const DriverDevice* device)
{
    AtaDeviceAddress address;

    if (device == NULL)
        return false;

    address.channel = (uint8_t)device->payload[BLOCK_PAYLOAD_0];
    address.drive = (uint8_t)device->payload[BLOCK_PAYLOAD_1];
    if (!ata_init(&address))
        return false;

    g_block_device.backend = BLOCK_BACKEND_ATA_PIO;
    g_block_device.read_sectors = ata_read_sectors;
    g_block_device.write_sectors = ata_write_sectors;
    return true;
}

static const DriverOps g_block_driver_ahci = {
    .name = "ahci",
    .match = block_driver_match_ahci,
    .probe = block_driver_probe_ahci,
};

static const DriverOps g_block_driver_ata = {
    .name = "ata",
    .match = block_driver_match_ata,
    .probe = block_driver_probe_ata,
};

static uint16_t device_path_length(const DevicePathNode* node)
{
    return (uint16_t)node->length[0] | ((uint16_t)node->length[1] << 8u);
}

static bool device_path_is_end(const DevicePathNode* node)
{
    return node->type == DEVICE_PATH_TYPE_END && node->subtype == DEVICE_PATH_SUBTYPE_END_ENTIRE;
}

static const DevicePathNode* next_device_path_node(const DevicePathNode* node)
{
    return (const DevicePathNode*)((const uint8_t*)node + device_path_length(node));
}

static BlockPathParseResult block_boot_path_to_ata(const BootInfo* boot_info, AtaDeviceAddress* out)
{
    const DevicePathNode* node;
    const DevicePathNode* end;

    if (boot_info == NULL || out == NULL)
        return BLOCK_PATH_PARSE_INVALID;
    if (boot_info->boot_device_path_size < sizeof(DevicePathNode))
        return BLOCK_PATH_PARSE_ABSENT;

    node = (const DevicePathNode*)boot_info->boot_device_path;
    end = (const DevicePathNode*)(boot_info->boot_device_path + boot_info->boot_device_path_size);
    while ((const uint8_t*)node + sizeof(DevicePathNode) <= (const uint8_t*)end
        && (const uint8_t*)node < (const uint8_t*)end) {
        uint16_t node_length = device_path_length(node);

        if (node_length < sizeof(DevicePathNode))
            return BLOCK_PATH_PARSE_INVALID;
        if ((const uint8_t*)node + node_length > (const uint8_t*)end)
            return BLOCK_PATH_PARSE_INVALID;
        if (device_path_is_end(node))
            break;
        if (node->type == DEVICE_PATH_TYPE_MESSAGING
            && node->subtype == DEVICE_PATH_SUBTYPE_ATAPI
            && node_length >= sizeof(AtapiDevicePath)) {
            const AtapiDevicePath* atapi = (const AtapiDevicePath*)node;

            if (atapi->primary_secondary > 1u || atapi->slave_master > 1u)
                return BLOCK_PATH_PARSE_INVALID;
            out->channel = atapi->primary_secondary;
            out->drive = atapi->slave_master;
            return BLOCK_PATH_PARSE_FOUND;
        }
        node = next_device_path_node(node);
    }

    return BLOCK_PATH_PARSE_ABSENT;
}

static void block_default_ata_device(AtaDeviceAddress* out)
{
    out->channel = 0u;
    out->drive = 0u;
}

static bool block_resolve_pci_path(const BlockPciPathHop* hops, uint8_t hop_count, PciAddress* out)
{
    uint8_t bus = 0u;
    uint8_t hop_index;

    if (hops == NULL || out == NULL || hop_count == 0u)
        return false;

    for (hop_index = 0u; hop_index < hop_count; ++hop_index) {
        PciAddress address = {bus, hops[hop_index].device, hops[hop_index].function};
        PciDeviceInfo info;

        if (!pci_read_device_info(&address, &info))
            return false;
        if (hop_index + 1u == hop_count) {
            *out = address;
            return true;
        }
        if (info.class_code != 0x06u || info.subclass != 0x04u)
            return false;

        bus = pci_read_config8(&address, 0x19u);
        if (bus == 0xFFu)
            return false;
    }

    return false;
}

static bool block_boot_path_to_ahci(const BootInfo* boot_info, AhciDeviceAddress* out)
{
    const DevicePathNode* node;
    const DevicePathNode* end;
    BlockPciPathHop hops[BLOCK_MAX_PCI_PATH_DEPTH];
    uint8_t hop_count = 0u;
    bool found_sata = false;

    if (boot_info == NULL || out == NULL)
        return false;
    if (boot_info->boot_device_path_size < sizeof(DevicePathNode))
        return false;

    node = (const DevicePathNode*)boot_info->boot_device_path;
    end = (const DevicePathNode*)(boot_info->boot_device_path + boot_info->boot_device_path_size);
    while ((const uint8_t*)node + sizeof(DevicePathNode) <= (const uint8_t*)end
        && (const uint8_t*)node < (const uint8_t*)end) {
        uint16_t node_length = device_path_length(node);

        if (node_length < sizeof(DevicePathNode))
            return false;
        if ((const uint8_t*)node + node_length > (const uint8_t*)end)
            return false;
        if (device_path_is_end(node))
            break;

        if (node->type == DEVICE_PATH_TYPE_HARDWARE
            && node->subtype == DEVICE_PATH_SUBTYPE_PCI
            && node_length >= sizeof(PciDevicePath)) {
            const PciDevicePath* pci_node = (const PciDevicePath*)node;

            if (hop_count >= BLOCK_MAX_PCI_PATH_DEPTH)
                return false;
            hops[hop_count].device = pci_node->device;
            hops[hop_count].function = pci_node->function;
            ++hop_count;
        } else if (node->type == DEVICE_PATH_TYPE_MESSAGING
            && node->subtype == DEVICE_PATH_SUBTYPE_SATA
            && node_length >= sizeof(SataDevicePath)) {
            const SataDevicePath* sata = (const SataDevicePath*)node;

            if (sata->hba_port_number >= 32u)
                return false;
            if (sata->port_multiplier_port_number != 0u
                && sata->port_multiplier_port_number != 0xFFFFu)
                return false;
            if (sata->lun != 0u)
                return false;

            out->port = (uint8_t)sata->hba_port_number;
            found_sata = true;
        }

        node = next_device_path_node(node);
    }

    if (!found_sata || hop_count == 0u)
        return false;
    return block_resolve_pci_path(hops, hop_count, &out->controller);
}

bool block_init(const BootInfo* boot_info)
{
    DriverDevice candidates[3];
    size_t candidate_count = 0u;
    const DriverOps* bound_driver;
    const DriverDevice* bound_device;
    AhciDeviceAddress ahci_device;
    AtaDeviceAddress ata_device;
    BlockPathParseResult ata_result;

    g_block_device.backend = BLOCK_BACKEND_NONE;
    g_block_device.read_sectors = NULL;
    g_block_device.write_sectors = NULL;
    driver_core_reset();
    if (!driver_core_register(&g_block_driver_ahci))
        return false;
    if (!driver_core_register(&g_block_driver_ata))
        return false;

    if (block_boot_path_to_ahci(boot_info, &ahci_device)) {
        DriverDevice* candidate = &candidates[candidate_count++];

        candidate->bus_type = DRIVER_BUS_PCI;
        candidate->class_type = DRIVER_DEVICE_CLASS_BLOCK;
        candidate->vendor_id = 0u;
        candidate->device_id = 0u;
        candidate->class_code = 0u;
        candidate->subclass = 0u;
        candidate->prog_if = ahci_device.port;
        candidate->reserved0 = 0u;
        candidate->payload[BLOCK_PAYLOAD_KIND] = BLOCK_DRIVER_KIND_AHCI;
        candidate->payload[BLOCK_PAYLOAD_0] = ahci_device.controller.bus;
        candidate->payload[BLOCK_PAYLOAD_1] = ahci_device.controller.device;
        candidate->payload[BLOCK_PAYLOAD_2] = ahci_device.controller.function;
    }

    ata_result = block_boot_path_to_ata(boot_info, &ata_device);
    if (ata_result == BLOCK_PATH_PARSE_INVALID)
        return false;
    if (ata_result == BLOCK_PATH_PARSE_FOUND) {
        DriverDevice* candidate = &candidates[candidate_count++];

        candidate->bus_type = DRIVER_BUS_FIRMWARE;
        candidate->class_type = DRIVER_DEVICE_CLASS_BLOCK;
        candidate->vendor_id = 0u;
        candidate->device_id = 0u;
        candidate->class_code = 0u;
        candidate->subclass = 0u;
        candidate->prog_if = 0u;
        candidate->reserved0 = 0u;
        candidate->payload[BLOCK_PAYLOAD_KIND] = BLOCK_DRIVER_KIND_ATA;
        candidate->payload[BLOCK_PAYLOAD_0] = ata_device.channel;
        candidate->payload[BLOCK_PAYLOAD_1] = ata_device.drive;
        candidate->payload[BLOCK_PAYLOAD_2] = 0u;
    }

    if (ata_result == BLOCK_PATH_PARSE_ABSENT) {
        DriverDevice* candidate = &candidates[candidate_count++];

        block_default_ata_device(&ata_device);
        candidate->bus_type = DRIVER_BUS_FIRMWARE;
        candidate->class_type = DRIVER_DEVICE_CLASS_BLOCK;
        candidate->vendor_id = 0u;
        candidate->device_id = 0u;
        candidate->class_code = 0u;
        candidate->subclass = 0u;
        candidate->prog_if = 0u;
        candidate->reserved0 = 0u;
        candidate->payload[BLOCK_PAYLOAD_KIND] = BLOCK_DRIVER_KIND_ATA;
        candidate->payload[BLOCK_PAYLOAD_0] = ata_device.channel;
        candidate->payload[BLOCK_PAYLOAD_1] = ata_device.drive;
        candidate->payload[BLOCK_PAYLOAD_2] = 0u;
    }

    if (!driver_core_probe(candidates, candidate_count, &bound_driver, &bound_device))
        return false;
    (void)bound_driver;
    (void)bound_device;
    return g_block_device.read_sectors != NULL && g_block_device.write_sectors != NULL;
}

bool block_read_sectors(uint32_t lba, uint8_t count, void* out)
{
    if (g_block_device.read_sectors == NULL)
        return false;

    return g_block_device.read_sectors(lba, count, out);
}

bool block_write_sectors(uint32_t lba, uint8_t count, const void* data)
{
    if (g_block_device.write_sectors == NULL)
        return false;

    return g_block_device.write_sectors(lba, count, data);
}

BlockBackendKind block_backend_kind(void)
{
    return g_block_device.backend;
}

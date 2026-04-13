#ifndef FUNNYOS_KERNEL_PCI_H
#define FUNNYOS_KERNEL_PCI_H

#include "../common/types.h"

typedef struct PciAddress {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} PciAddress;

typedef struct PciDeviceInfo {
    PciAddress address;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
} PciDeviceInfo;

typedef bool (*PciVisitFn)(const PciDeviceInfo* info, void* context);

bool pci_init(void);
bool pci_available(void);
uint8_t pci_read_config8(const PciAddress* address, uint8_t offset);
uint16_t pci_read_config16(const PciAddress* address, uint8_t offset);
uint32_t pci_read_config32(const PciAddress* address, uint8_t offset);
void pci_write_config8(const PciAddress* address, uint8_t offset, uint8_t value);
void pci_write_config16(const PciAddress* address, uint8_t offset, uint16_t value);
void pci_write_config32(const PciAddress* address, uint8_t offset, uint32_t value);
void pci_enumerate(PciVisitFn visit, void* context);
bool pci_read_device_info(const PciAddress* address, PciDeviceInfo* out);
uint32_t pci_read_bar(const PciAddress* address, uint8_t bar_index);
uint64_t pci_read_bar64(const PciAddress* address, uint8_t bar_index);

#endif

#ifndef FUNNYOS_KERNEL_DRIVER_H
#define FUNNYOS_KERNEL_DRIVER_H

#include "../common/types.h"

#define DRIVER_MAX_DEVICE_PAYLOAD_WORDS 4u

typedef enum DriverBusType {
    DRIVER_BUS_FIRMWARE = 1u,
    DRIVER_BUS_PCI = 2u
} DriverBusType;

typedef enum DriverDeviceClass {
    DRIVER_DEVICE_CLASS_BLOCK = 1u
} DriverDeviceClass;

typedef struct DriverDevice {
    DriverBusType bus_type;
    DriverDeviceClass class_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t reserved0;
    uint32_t payload[DRIVER_MAX_DEVICE_PAYLOAD_WORDS];
} DriverDevice;

typedef bool (*DriverMatchFn)(const DriverDevice* device);
typedef bool (*DriverProbeFn)(const DriverDevice* device);

typedef struct DriverOps {
    const char* name;
    DriverMatchFn match;
    DriverProbeFn probe;
} DriverOps;

void driver_core_reset(void);
bool driver_core_register(const DriverOps* driver);
bool driver_core_probe(
    const DriverDevice* devices,
    size_t device_count,
    const DriverOps** bound_driver_out,
    const DriverDevice** bound_device_out);

#endif

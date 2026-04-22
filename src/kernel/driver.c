#include "driver.h"

#define DRIVER_MAX_REGISTERED 16u

static const DriverOps* g_registered[DRIVER_MAX_REGISTERED];
static size_t g_registered_count = 0u;

void driver_core_reset(void)
{
    size_t i;

    for (i = 0u; i < g_registered_count; ++i)
        g_registered[i] = NULL;
    g_registered_count = 0u;
}

bool driver_core_register(const DriverOps* driver)
{
    if (driver == NULL || driver->match == NULL || driver->probe == NULL)
        return false;
    if (g_registered_count >= DRIVER_MAX_REGISTERED)
        return false;

    g_registered[g_registered_count++] = driver;
    return true;
}

bool driver_core_probe(
    const DriverDevice* devices,
    size_t device_count,
    const DriverOps** bound_driver_out,
    const DriverDevice** bound_device_out)
{
    size_t device_index;
    size_t driver_index;

    if (bound_driver_out != NULL)
        *bound_driver_out = NULL;
    if (bound_device_out != NULL)
        *bound_device_out = NULL;
    if (devices == NULL || device_count == 0u)
        return false;

    for (device_index = 0u; device_index < device_count; ++device_index) {
        const DriverDevice* device = &devices[device_index];

        for (driver_index = 0u; driver_index < g_registered_count; ++driver_index) {
            const DriverOps* driver = g_registered[driver_index];

            if (driver == NULL || !driver->match(device))
                continue;
            if (!driver->probe(device))
                continue;

            if (bound_driver_out != NULL)
                *bound_driver_out = driver;
            if (bound_device_out != NULL)
                *bound_device_out = device;
            return true;
        }
    }

    return false;
}

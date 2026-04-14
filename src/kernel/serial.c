#include "serial.h"
#include "io.h"

#define SERIAL_DATA_PORT 0x3F8
#define SERIAL_INT_ENABLE_PORT 0x3F9
#define SERIAL_FIFO_CTRL_PORT 0x3FA
#define SERIAL_LINE_CTRL_PORT 0x3FB
#define SERIAL_MODEM_CTRL_PORT 0x3FC
#define SERIAL_LINE_STATUS_PORT 0x3FD
#define SERIAL_TX_TIMEOUT_SPINS 100000u
#define SERIAL_RX_TIMEOUT_SPINS 100000u

static bool g_serial_enabled = false;

void serial_init(void)
{
    io_out8(SERIAL_INT_ENABLE_PORT, 0x00);
    io_out8(SERIAL_LINE_CTRL_PORT, 0x80);
    io_out8(SERIAL_DATA_PORT, 0x03);
    io_out8(SERIAL_INT_ENABLE_PORT, 0x00);
    io_out8(SERIAL_LINE_CTRL_PORT, 0x03);
    io_out8(SERIAL_FIFO_CTRL_PORT, 0xC7);
    io_out8(SERIAL_MODEM_CTRL_PORT, 0x0B);

    g_serial_enabled = true;
}

void serial_write_byte(uint8_t value)
{
    uint32_t spins;

    if (!g_serial_enabled)
        return;

    for (spins = 0u; spins < SERIAL_TX_TIMEOUT_SPINS; ++spins) {
        uint8_t status = io_in8(SERIAL_LINE_STATUS_PORT);

        if (status == 0xFFu) {
            g_serial_enabled = false;
            return;
        }
        if ((status & 0x20u) != 0u)
            break;
        cpu_pause();
    }
    if (spins == SERIAL_TX_TIMEOUT_SPINS) {
        g_serial_enabled = false;
        return;
    }

    io_out8(SERIAL_DATA_PORT, value);
}

void serial_write(const char* s)
{
    while (*s != '\0') {
        serial_write_byte((uint8_t)*s);
        ++s;
    }
}

bool serial_has_byte(void)
{
    uint8_t status;

    if (!g_serial_enabled)
        return false;

    status = io_in8(SERIAL_LINE_STATUS_PORT);
    if (status == 0xFFu) {
        g_serial_enabled = false;
        return false;
    }

    return (status & 0x01u) != 0;
}

uint8_t serial_read_byte(void)
{
    uint32_t spins = 0u;

    if (!g_serial_enabled)
        return 0u;

    while (!serial_has_byte()) {
        if (spins++ >= SERIAL_RX_TIMEOUT_SPINS)
            return 0u;
        cpu_pause();
    }

    return io_in8(SERIAL_DATA_PORT);
}

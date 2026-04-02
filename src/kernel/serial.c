#include "serial.h"
#include "io.h"

#define SERIAL_DATA_PORT 0x3F8
#define SERIAL_INT_ENABLE_PORT 0x3F9
#define SERIAL_FIFO_CTRL_PORT 0x3FA
#define SERIAL_LINE_CTRL_PORT 0x3FB
#define SERIAL_MODEM_CTRL_PORT 0x3FC
#define SERIAL_LINE_STATUS_PORT 0x3FD

void serial_init(void)
{
    io_out8(SERIAL_INT_ENABLE_PORT, 0x00);
    io_out8(SERIAL_LINE_CTRL_PORT, 0x80);
    io_out8(SERIAL_DATA_PORT, 0x03);
    io_out8(SERIAL_INT_ENABLE_PORT, 0x00);
    io_out8(SERIAL_LINE_CTRL_PORT, 0x03);
    io_out8(SERIAL_FIFO_CTRL_PORT, 0xC7);
    io_out8(SERIAL_MODEM_CTRL_PORT, 0x0B);
}

void serial_write_byte(uint8_t value)
{
    while ((io_in8(SERIAL_LINE_STATUS_PORT) & 0x20u) == 0)
        cpu_pause();

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
    return (io_in8(SERIAL_LINE_STATUS_PORT) & 0x01u) != 0;
}

uint8_t serial_read_byte(void)
{
    while (!serial_has_byte())
        cpu_pause();

    return io_in8(SERIAL_DATA_PORT);
}

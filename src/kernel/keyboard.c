#include "keyboard.h"
#include "console.h"
#include "io.h"
#include "serial.h"

#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_DATA_PORT 0x60

static bool keyboard_has_scancode(void)
{
    return (io_in8(KEYBOARD_STATUS_PORT) & 0x01u) != 0;
}

static char translate_scancode(uint8_t scancode)
{
    switch (scancode) {
    case 0x02: return '1';
    case 0x03: return '2';
    case 0x04: return '3';
    case 0x05: return '4';
    case 0x06: return '5';
    case 0x07: return '6';
    case 0x08: return '7';
    case 0x09: return '8';
    case 0x0A: return '9';
    case 0x0B: return '0';
    case 0x0C: return '-';
    case 0x0E: return '\b';
    case 0x10: return 'q';
    case 0x11: return 'w';
    case 0x12: return 'e';
    case 0x13: return 'r';
    case 0x14: return 't';
    case 0x15: return 'y';
    case 0x16: return 'u';
    case 0x17: return 'i';
    case 0x18: return 'o';
    case 0x19: return 'p';
    case 0x1C: return '\n';
    case 0x1E: return 'a';
    case 0x1F: return 's';
    case 0x20: return 'd';
    case 0x21: return 'f';
    case 0x22: return 'g';
    case 0x23: return 'h';
    case 0x24: return 'j';
    case 0x25: return 'k';
    case 0x26: return 'l';
    case 0x2C: return 'z';
    case 0x2D: return 'x';
    case 0x2E: return 'c';
    case 0x2F: return 'v';
    case 0x30: return 'b';
    case 0x31: return 'n';
    case 0x32: return 'm';
    case 0x34: return '.';
    case 0x35: return '/';
    case 0x39: return ' ';
    default: return '\0';
    }
}

size_t keyboard_read_line(char* buffer, size_t capacity)
{
    size_t len = 0;

    if (capacity == 0)
        return 0;

    for (;;) {
        char c = '\0';

        if (serial_has_byte()) {
            c = (char)serial_read_byte();
            if (c == '\r')
                c = '\n';
            else if ((uint8_t)c == 0x7Fu)
                c = '\b';
        } else if (keyboard_has_scancode()) {
            uint8_t scancode = io_in8(KEYBOARD_DATA_PORT);

            if (scancode == 0xE0u)
                continue;
            if ((scancode & 0x80u) != 0)
                continue;

            c = translate_scancode(scancode);
        } else {
            cpu_pause();
            continue;
        }

        if (c == '\0')
            continue;

        if (c == '\b') {
            if (len != 0) {
                --len;
                buffer[len] = '\0';
                console_backspace();
            }
            continue;
        }

        if (c == '\n') {
            buffer[len] = '\0';
            console_write_char('\n');
            return len;
        }

        if (len + 1u >= capacity)
            continue;

        buffer[len++] = c;
        buffer[len] = '\0';
        console_write_char(c);
    }
}

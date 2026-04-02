#include "console.h"
#include "kstring.h"
#include "serial.h"

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_ATTRIBUTE 0x07u

static volatile uint16_t* const g_vga = (volatile uint16_t*)0xB8000;
static uint16_t g_row = 0;
static uint16_t g_col = 0;

static void put_vga_at(uint16_t row, uint16_t col, char c)
{
    g_vga[row * VGA_WIDTH + col] = (uint16_t)VGA_ATTRIBUTE << 8 | (uint8_t)c;
}

static void scroll_if_needed(void)
{
    uint16_t row;
    uint16_t col;

    if (g_row < VGA_HEIGHT)
        return;

    for (row = 1; row < VGA_HEIGHT; ++row) {
        for (col = 0; col < VGA_WIDTH; ++col)
            g_vga[(row - 1) * VGA_WIDTH + col] = g_vga[row * VGA_WIDTH + col];
    }

    for (col = 0; col < VGA_WIDTH; ++col)
        put_vga_at((uint16_t)(VGA_HEIGHT - 1), col, ' ');

    g_row = (uint16_t)(VGA_HEIGHT - 1);
}

void console_init(void)
{
    serial_init();
    console_clear();
}

void console_write_char(char c)
{
    if (c == '\r') {
        g_col = 0;
        serial_write_byte('\r');
        return;
    }

    if (c == '\n') {
        g_col = 0;
        ++g_row;
        scroll_if_needed();
        serial_write_byte('\r');
        serial_write_byte('\n');
        return;
    }

    put_vga_at(g_row, g_col, c);
    serial_write_byte((uint8_t)c);
    ++g_col;

    if (g_col >= VGA_WIDTH) {
        g_col = 0;
        ++g_row;
        scroll_if_needed();
    }
}

void console_write_n(const char* s, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i)
        console_write_char(s[i]);
}

void console_write(const char* s)
{
    console_write_n(s, k_strlen(s));
}

void console_write_line(const char* s)
{
    console_write(s);
    console_write_char('\n');
}

void console_clear(void)
{
    uint16_t row;
    uint16_t col;

    for (row = 0; row < VGA_HEIGHT; ++row) {
        for (col = 0; col < VGA_WIDTH; ++col)
            put_vga_at(row, col, ' ');
    }

    g_row = 0;
    g_col = 0;
}

void console_backspace(void)
{
    if (g_col == 0) {
        if (g_row == 0)
            return;

        --g_row;
        g_col = (uint16_t)(VGA_WIDTH - 1);
    } else {
        --g_col;
    }

    put_vga_at(g_row, g_col, ' ');
    serial_write("\b \b");
}

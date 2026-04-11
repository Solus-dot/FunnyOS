#include "console.h"
#include "io.h"
#include "kstring.h"
#include "serial.h"

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_ATTRIBUTE 0x07u
#define VGA_CRTC_INDEX_PORT 0x3D4u
#define VGA_CRTC_DATA_PORT 0x3D5u
#define VGA_CURSOR_HIGH_INDEX 14u
#define VGA_CURSOR_LOW_INDEX 15u
#define FB_CHAR_WIDTH 12u
#define FB_CHAR_HEIGHT 20u
#define FB_GLYPH_WIDTH 5u
#define FB_GLYPH_HEIGHT 7u
#define FB_GLYPH_SCALE_X 2u
#define FB_GLYPH_SCALE_Y 2u
#define FB_GLYPH_OFFSET_X 1u
#define FB_GLYPH_OFFSET_Y 2u

static volatile uint16_t* const g_vga = (volatile uint16_t*)0xB8000;
static uint16_t g_row = 0;
static uint16_t g_col = 0;
static bool g_console_vga_enabled = true;
static bool g_console_framebuffer_enabled = false;
static uint32_t* g_framebuffer = NULL;
static uint32_t g_framebuffer_width = 0;
static uint32_t g_framebuffer_height = 0;
static uint32_t g_framebuffer_pitch = 0;
static uint32_t g_console_columns = VGA_WIDTH;
static uint32_t g_console_rows = VGA_HEIGHT;
static uint32_t g_framebuffer_fg = 0x00E6E6E6u;
static uint32_t g_framebuffer_bg = 0x00111111u;

static uint32_t framebuffer_color(const BootInfo* boot_info, uint8_t r, uint8_t g, uint8_t b)
{
    if (boot_info != NULL && boot_info->framebuffer_format == BOOTINFO_FRAMEBUFFER_FORMAT_BGRX)
        return (uint32_t)b | ((uint32_t)g << 8u) | ((uint32_t)r << 16u);
    return (uint32_t)r | ((uint32_t)g << 8u) | ((uint32_t)b << 16u);
}

static uint8_t glyph_row_bits(char c, uint8_t row)
{
    if (c >= 'a' && c <= 'z')
        c = k_toupper(c);

    switch (c) {
    case 'A': return (const uint8_t[]){0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}[row];
    case 'B': return (const uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}[row];
    case 'C': return (const uint8_t[]){0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}[row];
    case 'D': return (const uint8_t[]){0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}[row];
    case 'E': return (const uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}[row];
    case 'F': return (const uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}[row];
    case 'G': return (const uint8_t[]){0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}[row];
    case 'H': return (const uint8_t[]){0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}[row];
    case 'I': return (const uint8_t[]){0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}[row];
    case 'J': return (const uint8_t[]){0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}[row];
    case 'K': return (const uint8_t[]){0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}[row];
    case 'L': return (const uint8_t[]){0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}[row];
    case 'M': return (const uint8_t[]){0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}[row];
    case 'N': return (const uint8_t[]){0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}[row];
    case 'O': return (const uint8_t[]){0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}[row];
    case 'P': return (const uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}[row];
    case 'Q': return (const uint8_t[]){0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}[row];
    case 'R': return (const uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}[row];
    case 'S': return (const uint8_t[]){0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}[row];
    case 'T': return (const uint8_t[]){0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}[row];
    case 'U': return (const uint8_t[]){0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}[row];
    case 'V': return (const uint8_t[]){0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}[row];
    case 'W': return (const uint8_t[]){0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}[row];
    case 'X': return (const uint8_t[]){0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}[row];
    case 'Y': return (const uint8_t[]){0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}[row];
    case 'Z': return (const uint8_t[]){0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}[row];
    case '0': return (const uint8_t[]){0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}[row];
    case '1': return (const uint8_t[]){0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}[row];
    case '2': return (const uint8_t[]){0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}[row];
    case '3': return (const uint8_t[]){0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}[row];
    case '4': return (const uint8_t[]){0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}[row];
    case '5': return (const uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}[row];
    case '6': return (const uint8_t[]){0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}[row];
    case '7': return (const uint8_t[]){0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}[row];
    case '8': return (const uint8_t[]){0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}[row];
    case '9': return (const uint8_t[]){0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}[row];
    case '.': return (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}[row];
    case ',': return (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x08}[row];
    case ':': return (const uint8_t[]){0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}[row];
    case ';': return (const uint8_t[]){0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x08}[row];
    case '/': return (const uint8_t[]){0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}[row];
    case '\\': return (const uint8_t[]){0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}[row];
    case '-': return (const uint8_t[]){0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}[row];
    case '_': return (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}[row];
    case '=': return (const uint8_t[]){0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}[row];
    case '+': return (const uint8_t[]){0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}[row];
    case '(': return (const uint8_t[]){0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}[row];
    case ')': return (const uint8_t[]){0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}[row];
    case '[': return (const uint8_t[]){0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}[row];
    case ']': return (const uint8_t[]){0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}[row];
    case '<': return (const uint8_t[]){0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}[row];
    case '>': return (const uint8_t[]){0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}[row];
    case '?': return (const uint8_t[]){0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}[row];
    case '!': return (const uint8_t[]){0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}[row];
    case '\'': return (const uint8_t[]){0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}[row];
    case '\"': return (const uint8_t[]){0x0A, 0x0A, 0x04, 0x00, 0x00, 0x00, 0x00}[row];
    case ' ': return 0x00;
    default: return (const uint8_t[]){0x0E, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04}[row];
    }
}

static void put_framebuffer_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_console_framebuffer_enabled || x >= g_framebuffer_width || y >= g_framebuffer_height)
        return;
    g_framebuffer[y * g_framebuffer_pitch + x] = color;
}

static void clear_framebuffer_cell(uint32_t row, uint32_t col)
{
    uint32_t y;
    uint32_t x;
    uint32_t base_x = col * FB_CHAR_WIDTH;
    uint32_t base_y = row * FB_CHAR_HEIGHT;

    for (y = 0; y < FB_CHAR_HEIGHT; ++y) {
        for (x = 0; x < FB_CHAR_WIDTH; ++x)
            put_framebuffer_pixel(base_x + x, base_y + y, g_framebuffer_bg);
    }
}

static void put_framebuffer_char_at(uint32_t row, uint32_t col, char c)
{
    uint32_t base_x = col * FB_CHAR_WIDTH;
    uint32_t base_y = row * FB_CHAR_HEIGHT;
    uint32_t y;
    uint32_t x;

    clear_framebuffer_cell(row, col);
    if (c == ' ')
        return;

    for (y = 0; y < FB_GLYPH_HEIGHT; ++y) {
        uint8_t bits = glyph_row_bits(c, (uint8_t)y);
        uint32_t draw_y;

        for (x = 0; x < FB_GLYPH_WIDTH; ++x) {
            uint32_t draw_x;

            if ((bits & (1u << (FB_GLYPH_WIDTH - 1u - x))) == 0u)
                continue;

            for (draw_y = 0; draw_y < FB_GLYPH_SCALE_Y; ++draw_y) {
                for (draw_x = 0; draw_x < FB_GLYPH_SCALE_X; ++draw_x) {
                    put_framebuffer_pixel(
                        base_x + FB_GLYPH_OFFSET_X + x * FB_GLYPH_SCALE_X + draw_x,
                        base_y + FB_GLYPH_OFFSET_Y + y * FB_GLYPH_SCALE_Y + draw_y,
                        g_framebuffer_fg);
                }
            }
        }
    }
}

static void put_vga_at(uint16_t row, uint16_t col, char c)
{
    if (!g_console_vga_enabled)
        return;
    g_vga[row * VGA_WIDTH + col] = (uint16_t)VGA_ATTRIBUTE << 8 | (uint8_t)c;
}

static void update_cursor(void)
{
    uint16_t position = (uint16_t)(g_row * VGA_WIDTH + g_col);

    if (!g_console_vga_enabled)
        return;

    io_out8(VGA_CRTC_INDEX_PORT, VGA_CURSOR_HIGH_INDEX);
    io_out8(VGA_CRTC_DATA_PORT, (uint8_t)(position >> 8));
    io_out8(VGA_CRTC_INDEX_PORT, VGA_CURSOR_LOW_INDEX);
    io_out8(VGA_CRTC_DATA_PORT, (uint8_t)(position & 0xFFu));
}

static void scroll_if_needed(void)
{
    uint32_t row;
    uint32_t col;
    uint32_t y;

    if (g_row < g_console_rows)
        return;

    if (g_console_framebuffer_enabled) {
        for (y = FB_CHAR_HEIGHT; y < g_framebuffer_height; ++y) {
            uint32_t* dst = g_framebuffer + (y - FB_CHAR_HEIGHT) * g_framebuffer_pitch;
            uint32_t* src = g_framebuffer + y * g_framebuffer_pitch;

            for (col = 0; col < g_framebuffer_width; ++col)
                dst[col] = src[col];
        }
        for (y = g_framebuffer_height - FB_CHAR_HEIGHT; y < g_framebuffer_height; ++y) {
            uint32_t* row_pixels = g_framebuffer + y * g_framebuffer_pitch;

            for (col = 0; col < g_framebuffer_width; ++col)
                row_pixels[col] = g_framebuffer_bg;
        }
    }

    if (g_console_vga_enabled) {
        for (row = 1; row < VGA_HEIGHT; ++row) {
            for (col = 0; col < VGA_WIDTH; ++col)
                g_vga[(row - 1) * VGA_WIDTH + col] = g_vga[row * VGA_WIDTH + col];
        }

        for (col = 0; col < VGA_WIDTH; ++col)
            put_vga_at((uint16_t)(VGA_HEIGHT - 1), (uint16_t)col, ' ');
    }

    g_row = (uint16_t)(g_console_rows - 1u);
    update_cursor();
}

void console_init(const BootInfo* boot_info)
{
    g_console_vga_enabled = boot_info != NULL && (boot_info->console_flags & BOOTINFO_CONSOLE_VGA_TEXT) != 0;
    g_console_framebuffer_enabled = boot_info != NULL
        && (boot_info->console_flags & BOOTINFO_CONSOLE_FRAMEBUFFER) != 0
        && boot_info->framebuffer_base != 0u
        && boot_info->framebuffer_width >= FB_CHAR_WIDTH
        && boot_info->framebuffer_height >= FB_CHAR_HEIGHT
        && boot_info->framebuffer_pixels_per_scanline >= boot_info->framebuffer_width;
    if (g_console_framebuffer_enabled) {
        g_framebuffer = (uint32_t*)(uintptr_t)boot_info->framebuffer_base;
        g_framebuffer_width = boot_info->framebuffer_width;
        g_framebuffer_height = boot_info->framebuffer_height;
        g_framebuffer_pitch = boot_info->framebuffer_pixels_per_scanline;
        g_framebuffer_fg = framebuffer_color(boot_info, 0xE6u, 0xE6u, 0xE6u);
        g_framebuffer_bg = framebuffer_color(boot_info, 0x11u, 0x11u, 0x11u);
        g_console_columns = g_framebuffer_width / FB_CHAR_WIDTH;
        g_console_rows = g_framebuffer_height / FB_CHAR_HEIGHT;
    } else {
        g_framebuffer = NULL;
        g_framebuffer_width = 0u;
        g_framebuffer_height = 0u;
        g_framebuffer_pitch = 0u;
        g_console_columns = VGA_WIDTH;
        g_console_rows = VGA_HEIGHT;
    }
    serial_init();
    console_clear();
}

void console_write_char(char c)
{
    if (c == '\r') {
        g_col = 0;
        serial_write_byte('\r');
        update_cursor();
        return;
    }

    if (c == '\n') {
        g_col = 0;
        ++g_row;
        scroll_if_needed();
        serial_write_byte('\r');
        serial_write_byte('\n');
        update_cursor();
        return;
    }

    put_vga_at(g_row, g_col, c);
    if (g_console_framebuffer_enabled)
        put_framebuffer_char_at(g_row, g_col, c);
    serial_write_byte((uint8_t)c);
    ++g_col;

    if (g_col >= g_console_columns) {
        g_col = 0;
        ++g_row;
        scroll_if_needed();
    }

    update_cursor();
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

    if (g_console_vga_enabled) {
        for (row = 0; row < VGA_HEIGHT; ++row) {
            for (col = 0; col < VGA_WIDTH; ++col)
                put_vga_at(row, col, ' ');
        }
    }
    if (g_console_framebuffer_enabled) {
        for (row = 0; row < g_console_rows; ++row) {
            for (col = 0; col < g_console_columns; ++col)
                clear_framebuffer_cell(row, col);
        }
    }

    g_row = 0;
    g_col = 0;
    update_cursor();
}

void console_backspace(void)
{
    if (g_col == 0) {
        if (g_row == 0)
            return;

        --g_row;
        g_col = (uint16_t)(g_console_columns - 1u);
    } else {
        --g_col;
    }

    put_vga_at(g_row, g_col, ' ');
    if (g_console_framebuffer_enabled)
        put_framebuffer_char_at(g_row, g_col, ' ');
    serial_write("\b \b");
    update_cursor();
}

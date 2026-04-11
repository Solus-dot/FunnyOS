#include "panic.h"
#include "console.h"
#include "trap.h"

static bool g_panic_active = false;

static const char* panic_exception_name(uint64_t vector)
{
    static const char* const names[] = {
        "Divide Error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack-Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Hypervisor Injection Exception",
        "VMM Communication Exception",
        "Security Exception",
        "Reserved",
    };

    if (vector < sizeof(names) / sizeof(names[0]))
        return names[vector];
    return "Unexpected Interrupt";
}

static void panic_write_hex_digit(uint8_t value)
{
    if (value < 10u)
        console_write_char((char)('0' + value));
    else
        console_write_char((char)('A' + (value - 10u)));
}

static void panic_write_hex64(uint64_t value)
{
    int shift;

    console_write("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        panic_write_hex_digit((uint8_t)((value >> shift) & 0xFu));
}

static void panic_write_register_line(const char* name, uint64_t value)
{
    console_write(name);
    console_write(": ");
    panic_write_hex64(value);
    console_write_char('\n');
}

static uint64_t panic_read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void panic_halt(void) __attribute__((noreturn));

static void panic_halt(void)
{
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void panic(const char* message)
{
    panic_with_frame(message, NULL);
}

void panic_with_frame(const char* message, const struct TrapFrame* frame)
{
    if (g_panic_active)
        panic_halt();

    g_panic_active = true;

    console_write_char('\n');
    console_write_line("=== KERNEL PANIC ===");
    if (message != NULL)
        console_write_line(message);

    if (frame != NULL) {
        console_write("Exception: ");
        console_write_line(panic_exception_name(frame->vector));
        panic_write_register_line("VECTOR", frame->vector);
        panic_write_register_line("ERROR", frame->error_code);
        panic_write_register_line("RIP", frame->rip);
        panic_write_register_line("CS", frame->cs);
        panic_write_register_line("RFLAGS", frame->rflags);
        panic_write_register_line("RSP", frame->rsp);
        if (frame->vector == 14u)
            panic_write_register_line("CR2", panic_read_cr2());
        panic_write_register_line("RAX", frame->rax);
        panic_write_register_line("RBX", frame->rbx);
        panic_write_register_line("RCX", frame->rcx);
        panic_write_register_line("RDX", frame->rdx);
        panic_write_register_line("RBP", frame->rbp);
        panic_write_register_line("RDI", frame->rdi);
        panic_write_register_line("RSI", frame->rsi);
        panic_write_register_line("R8", frame->r8);
        panic_write_register_line("R9", frame->r9);
        panic_write_register_line("R10", frame->r10);
        panic_write_register_line("R11", frame->r11);
        panic_write_register_line("R12", frame->r12);
        panic_write_register_line("R13", frame->r13);
        panic_write_register_line("R14", frame->r14);
        panic_write_register_line("R15", frame->r15);
    }

    panic_halt();
}

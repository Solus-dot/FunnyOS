#ifndef FUNNYOS_KERNEL_PANIC_H
#define FUNNYOS_KERNEL_PANIC_H

struct TrapFrame;

void panic(const char* message) __attribute__((noreturn));
void panic_with_frame(const char* message, const struct TrapFrame* frame) __attribute__((noreturn));

#endif

#include "trap.h"
#include "panic.h"
#include "process.h"
#include "../common/program_api.h"

#define TRAP_TEST_PAGE_FAULT_ADDRESS 0x00007FFF80000000ull

extern void program_return_to_kernel(void) __attribute__((noreturn));

void interrupt_dispatch(TrapFrame* frame)
{
    if (frame != NULL && frame->vector == PROGRAM_SYSCALL_VECTOR) {
        if (process_handle_syscall(frame)) {
            if (process_should_return_to_kernel())
                program_return_to_kernel();
            return;
        }
    }

    if (process_handle_fault(frame)) {
        if (process_should_return_to_kernel())
            program_return_to_kernel();
        return;
    }

    if (frame != NULL && (frame->cs & 0x3u) == 0x3u) {
        if (process_should_return_to_kernel()) {
            program_return_to_kernel();
            return;
        }
    }

    panic_with_frame("CPU exception", frame);
}

void trap_trigger_invalid_opcode(void)
{
    __asm__ volatile("ud2");
}

void trap_trigger_page_fault(void)
{
    volatile uint64_t* ptr = (volatile uint64_t*)(uintptr_t)TRAP_TEST_PAGE_FAULT_ADDRESS;

    *ptr = 0x46554E4Eu;
}

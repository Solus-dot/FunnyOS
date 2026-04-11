#include "trap.h"
#include "panic.h"

#define TRAP_TEST_PAGE_FAULT_ADDRESS 0x00007FFF80000000ull

void interrupt_dispatch(TrapFrame* frame)
{
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

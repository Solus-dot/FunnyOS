#include "../common/runtime.h"

uint32_t program_main(const ProgramInfo* info)
{
    (void)info;

    program_write_line("Hello from HELLO.ELF");
    return 0;
}

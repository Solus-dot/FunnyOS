#include "../common/runtime.h"

uint32_t program_main(const ProgramApi* api, const ProgramInfo* info)
{
    (void)api;
    (void)info;

    program_write_line("Hello from HELLO.ELF");
    return 0;
}

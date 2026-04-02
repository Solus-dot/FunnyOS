#include "../common/runtime.h"

uint32_t program_main(const ProgramApi* api, const ProgramInfo* info)
{
    uint32_t* argv;
    uint32_t i;

    (void)api;

    program_write("argc=", 5);
    program_write_u32(info->argc);
    program_write("\n", 1);

    argv = (uint32_t*)(uintptr_t)info->argv_addr;
    for (i = 0; i < info->argc; ++i) {
        program_write("argv[", 5);
        program_write_u32(i);
        program_write("]=", 2);
        program_write_line((const char*)(uintptr_t)argv[i]);
    }

    return 0;
}

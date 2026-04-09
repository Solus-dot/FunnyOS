#ifndef FUNNYOS_PROGRAM_RUNTIME_H
#define FUNNYOS_PROGRAM_RUNTIME_H

#include "../../common/program_api.h"
#include "../../common/types.h"

extern const ProgramApi* g_program_api;
extern const ProgramInfo* g_program_info;

size_t program_strlen(const char* s);
void program_write(const char* data, size_t len);
void program_write_str(const char* s);
void program_write_line(const char* s);
void program_write_u32(uint32_t value);
size_t program_read_line(char* buf, size_t cap);

#endif

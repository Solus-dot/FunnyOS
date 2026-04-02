#ifndef FUNNYOS_KERNEL_SERIAL_H
#define FUNNYOS_KERNEL_SERIAL_H

#include "../common/types.h"

void serial_init(void);
void serial_write_byte(uint8_t value);
void serial_write(const char* s);
bool serial_has_byte(void);
uint8_t serial_read_byte(void);

#endif

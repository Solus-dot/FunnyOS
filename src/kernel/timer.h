#ifndef FUNNYOS_KERNEL_TIMER_H
#define FUNNYOS_KERNEL_TIMER_H

#include "../common/types.h"

bool timer_init(void);
uint64_t timer_tsc_hz(void);

#endif

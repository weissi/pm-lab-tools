#ifndef SYNC_H
#define SYNC_H

#include <time.h>

#define TIME_MS ((unsigned long int)1000000L)
#define TIME_S ((unsigned long int)(1000L*(TIME_MS)))

#define WAIT_TIMEOUT ((struct timespec){1, 500L*TIME_MS})

void abs_wait_timeout(struct timespec *abs_timeout);

void wait_read_barrier(void);
void notify_read_barrier(void);

uint64_t wait_data_available(uint64_t last_data);
void notify_data_available(uint64_t new_da);
void set_ready(uint64_t da);

void inc_available_handlers(void);
void dec_available_handlers(void);
unsigned int get_available_handlers(void);

void reset_ready_handlers(void);

#endif

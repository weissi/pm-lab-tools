#ifndef SYNC_H
#define SYNC_H

#include <time.h>
#include <pthread.h>

#define TIME_MS ((unsigned long int)1000000L)
#define TIME_S ((unsigned long int)(1000L*(TIME_MS)))

#define WAIT_TIMEOUT ((struct timespec){1, 500L*TIME_MS})

void abs_wait_timeout(struct timespec *abs_timeout);

void init_sync(void);
void finish_sync(void);

void wait_read_barrier(void);
void notify_read_barrier(void);

void wait_data_available(void);
void notify_data_available(void);
void set_ready(void);

void inc_available_handlers(void);
void dec_available_handlers(void);

void reset_ready_handlers(void);

#endif

#ifndef COMMON_H
#define COMMON_H

#include <time.h>

#define TIME_MS (1000000L)
#define TIME_S (1000*(TIME_MS))

#define WAIT_TIMEOUT ((struct timespec){1, 500L*TIME_MS})

#endif

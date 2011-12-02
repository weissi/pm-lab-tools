#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

extern volatile bool running;

typedef struct {
    uint64_t timestamp_nanos;
    unsigned int points_per_channel;
    unsigned int num_channels;

    /* analog inputs */
    double *analog_data;

    /* digital inputs */
    digival_t *digital_data;
} input_data_t;

typedef struct {
    int fd;
    input_data_t *data_info; /* const */
} handler_thread_info_t;

#endif

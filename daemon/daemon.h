#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>

extern volatile bool running;

typedef struct {
    unsigned int points_per_channel;

    /* analog inputs */
    double *analog_data;
    unsigned int analog_channels;

    /* digital inputs */
    bool *digital_data;
    unsigned int digital_channels;
} input_data_t;

typedef struct {
    int fd;
    input_data_t *data_info;
} handler_thread_info_t;

#endif

/*
 *  Records analog data from a NI USB-6218 and send it to connected clients
 *
 *  Copyright (C)2011-2012, Johannes Weiß <weiss@tux4u.de>
 *                        , Jonathan Dimond <jonny@dimond.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef DAEMON_H
#define DAEMON_H

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

extern volatile bool running;

typedef struct {
    pthread_mutex_t lock;
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
/* vim: set fileencoding=utf8 : */

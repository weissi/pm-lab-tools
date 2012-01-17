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

bool have_alive_threads(void);
pthread_t *wait_dead_handler(void);

#endif
/* vim: set fileencoding=utf8 : */

/*
 *  Records data from a NI USB-6218 and send it to connected clients
 *
 *  Copyright (C)2011, Johannes Weiß <weiss@tux4u.de>
 *                   , Jonathan Dimond <jonny@dimond.de>
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>

#define DAQmx_Val_GroupByChannel 0
#define SERVER_PORT 12345
#define LISTEN_QUEUE_LEN 8

typedef struct {
    double *data;
    unsigned int points_per_channel;
} data_info_t;

typedef struct {
    int fd;
    data_info_t *data_info;
} handler_thread_info_t;

static const double TEST_DATA[] =
    { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, /* channel 1 */
      0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, /* channel 2 */
      0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0  /* channel 3 */
    };

static const struct timespec WAIT_TIMEOUT = { 1, 500000000L };

static pthread_mutex_t __mutex_read = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t __cond_read = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t __mutex_data = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t __cond_data = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t __mutex_handlers = PTHREAD_MUTEX_INITIALIZER;
static time_t __data_available = 0;
static unsigned int __ready_handlers = 0;
static volatile unsigned int __available_handlers = 0;

static volatile bool running = true;
static void sig_hnd() {
    printf("Ctrl+C caught, exiting...\n");
    running = false;
}

static int read_dummy(void *handle, unsigned int sampling_rate, time_t timeout,
                      int format, double *data, size_t data_size,
                      unsigned int *points_per_channel, void *unused) {
    (void)handle;
    (void)sampling_rate;
    (void)timeout;
    (void)unused;
    assert(DAQmx_Val_GroupByChannel == format);

    assert(30 <= data_size);
    *points_per_channel = 10;
    sleep(1);
    memcpy(data, TEST_DATA, 30 * sizeof(double));

    return 0;
}

static unsigned int get_available_handlers(void) {
    int err;
    unsigned int handlers;

    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);

    handlers = __available_handlers;

    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);

    return handlers;
}

static void get_abs_wait_timeout(struct timespec *abs_timeout) {
    struct timespec wait_timeout = WAIT_TIMEOUT;
    int err = clock_gettime(CLOCK_REALTIME, abs_timeout);
    assert(0 == err);
    if(abs_timeout->tv_nsec >= 500000000L) {
        /* take care tv_nsec does not get greater or equal one second */
        abs_timeout->tv_sec++;
        wait_timeout.tv_nsec -= 500000000L;
    }
    abs_timeout->tv_sec += wait_timeout.tv_sec;
    abs_timeout->tv_nsec += wait_timeout.tv_nsec;
}

#define START_TIMING(t) (t) = time(NULL)
#define STOP_TIMING(t) (t) = (time(NULL) - (t))
#define PRINT_TIMING(t,n) printf("[%ld] "n": %lds\n", pthread_self(), (long int)(t))

static void wait_read_barrier(void) {
    int err;
    time_t timer;
    struct timespec abs_timeout;

    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);
    while(running && __ready_handlers < get_available_handlers()) {
        get_abs_wait_timeout(&abs_timeout);
        err = pthread_cond_timedwait(&__cond_read, &__mutex_read, &abs_timeout);
        assert(0 == err || ETIMEDOUT == err);
        printf("pthread_cond_timedwait: %s (av handlers: %u, rd handlers: %u)\n",
               strerror(err),
               __available_handlers,
               __ready_handlers);
    }
    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_read_barrier");
}

static void notify_read_barrier(void) {
    int err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);
    __ready_handlers++;
    pthread_cond_broadcast(&__cond_read);
    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
    printf("NOTIFIED READ BARRIER\n");
}

static time_t wait_data_available(time_t last_data) {
    int err;
    struct timespec timeout = WAIT_TIMEOUT;
    time_t timer, data_at = 0;
    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex_data);
    assert(0 == err);
    while(running && last_data >= __data_available) {
        err = pthread_cond_timedwait(&__cond_data, &__mutex_data, &timeout);
        assert(0 == err || ETIMEDOUT == err);
    }
    data_at = __data_available;
    err = pthread_mutex_unlock(&__mutex_data);
    assert(0 == err);
    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_data_available");
    return data_at;
}

static void notify_data_available(void) {
    printf("DA\n");
    fflush(stdout);
    pthread_mutex_lock(&__mutex_data);
    __data_available = time(NULL);
    pthread_mutex_unlock(&__mutex_data);
}

static void notify_data_unavailable(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_data);
    assert(0 == err);
    __data_available = 0;
    err = pthread_mutex_unlock(&__mutex_data);
    assert(0 == err);
}

static void inc_available_handlers(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);
    __available_handlers++;
    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);
    notify_read_barrier();
}

static void dec_available_handlers(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);
    __available_handlers--;
    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);
    notify_read_barrier();
}

static void *ni_thread_main(void *opaque_info) {
    data_info_t *info = (data_info_t *)opaque_info;
    double data[1000];
    unsigned int points_pc;

    while(running) {
        wait_read_barrier();
        if(!running) {
            break;
        }
        __ready_handlers = 0;
        notify_data_unavailable();
        read_dummy(NULL, 50000, 0, DAQmx_Val_GroupByChannel, data, 1000, &points_pc, NULL);
        info->points_per_channel = points_pc;
        info->data = data;
        printf("NI: read successful\n");
        notify_data_available();
    }
    return NULL;
}

static void *handler_thread_main(void *opaque_info) {
    char buf[1024];
    handler_thread_info_t *info = (handler_thread_info_t *)opaque_info;
    int conn_fd = info->fd;
    int err;
    time_t last_data = 0;

    printf("Handler thread accepted %d\n", conn_fd);
    inc_available_handlers();
    err = write(conn_fd, "WELCOME ABOARD\n", 15);
    assert(15 == err);

    while(running) {
        last_data = wait_data_available(last_data);
        if(!running) {
            break;
        }
        printf("Client %ld received data %ld\n", pthread_self(), (long int)last_data);
        err = write(conn_fd, "DATA AVAILABLE, PRESS ENTER\n", 28);
        if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(err));
            break;
        }
        assert(28 == err);

        err = read(conn_fd, buf, 1);
        if(0 == err) {
            /* EOF */
            printf("Client %ld left, EOF\n", pthread_self());
            break;
        } else if(0 > err) {
            /* ERROR */
            printf("Client %ld read failed: %s\n", pthread_self(), strerror(err));
            break;
        }

        err = write(conn_fd, "THANKS\n", 7);
        if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(err));
            break;
        }
        assert(7 == err);

        notify_read_barrier();
    }

    dec_available_handlers();

    err = close(conn_fd);
    assert(0 == err);
    free(opaque_info);
    return NULL;
}

static void launch_handler_thread(data_info_t *data_info, int conn_fd) {
    pthread_t handler_thread;
    int err;
    handler_thread_info_t *handler_info =
        (handler_thread_info_t *)malloc(sizeof(handler_thread_info_t));
    assert(NULL != handler_info);

    handler_info->fd = conn_fd;
    handler_info->data_info = data_info;
    printf("handling conn fd %d\n", handler_info->fd);

    err = pthread_create(&handler_thread,
                         NULL,
                         handler_thread_main,
                         handler_info);
    assert(0 == err);
}

static void wait_for_connections(data_info_t *data_info) {
    struct sockaddr_in servaddr;
    int err;
    int conn;
    struct pollfd poll_cfg;
    int sock_opt;
    struct timespec timeout = WAIT_TIMEOUT;
    const int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(0 <= server_sock);

    sock_opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERVER_PORT);

    err = bind(server_sock, (struct sockaddr *) &servaddr, sizeof(servaddr));
    perror("bind");
    assert(0 <= err);

    err = listen(server_sock, LISTEN_QUEUE_LEN);
    assert(0 <= err);

    poll_cfg.fd = server_sock;
    poll_cfg.events = POLLIN;

    while(running) {
        err = ppoll(&poll_cfg, 1, &timeout, NULL);
        if (0 == err || (-1 == err && EINTR == errno)) {
            continue;
        }
        assert(0 < err);

        conn = accept(server_sock, NULL, NULL);
        launch_handler_thread(data_info, conn);
    }

    err = close(server_sock);
    assert(0 == err);
    printf("server socket closed\n");

    return;
}

int main(int argc, char **argv) {
    data_info_t data_info;
    pthread_t acquire_data_thread;

    int err;

    signal(SIGINT, (void (*)(int))sig_hnd);
    signal(SIGPIPE, SIG_IGN);

    err = pthread_create(&acquire_data_thread, NULL, ni_thread_main, &data_info);
    assert(0 == err);

    wait_for_connections(&data_info);

    err = pthread_join(acquire_data_thread, NULL);
    assert(0 == err);

    return 0;
}
/* vim: set fileencoding=utf8 : */

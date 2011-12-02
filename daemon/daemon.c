/*
 *  Records analog data from a NI USB-6218 and send it to connected clients
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
#include <string.h>
#include <pthread.h>
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

#include "common.h"
#include "daemon.h"
#include "sync.h"
#include "handler.h"

#define DAQmx_Val_GroupByChannel 0
#define SERVER_PORT 12345
#define LISTEN_QUEUE_LEN 8
#define BUFFER_SAMPLES_PER_CHANNEL 1024
#define SAMPLING_RATE 50000


static const double TEST_ANALOG_DATA[] =
    { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, /* channel 1 */
      0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, /* channel 2 */
      0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0  /* channel 3 */
    };

static const digival_t TEST_DIGITAL_DATA[] =
    { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, /* channel 1 */
      0, 1, 0, 0, 0, 0, 1, 0, 0, 0, /* channel 2 */
      0, 0, 1, 0, 0, 0, 1, 0, 0, 0  /* channel 3 */
    };

volatile bool running = true;
static void sig_hnd() {
    printf("Ctrl+C caught, exiting...\n");
    running = false;
}

static double timediff(struct timespec *start, struct timespec *end) {
    double d;
    struct timespec temp;
    if ((end->tv_nsec-start->tv_nsec)<0) {
        temp.tv_sec = end->tv_sec-start->tv_sec-1;
        temp.tv_nsec = 1000000000+end->tv_nsec-start->tv_nsec;
    } else {
        temp.tv_sec = end->tv_sec-start->tv_sec;
        temp.tv_nsec = end->tv_nsec-start->tv_nsec;
    }
    d = temp.tv_sec + (temp.tv_nsec / 1000000000.0);
    /*
    printf("%ld.%ld - %ld.%ld = %ld.%ld = %f = %e\n", end.tv_sec, end.tv_nsec,
           start.tv_sec, start.tv_nsec, temp.tv_sec, temp.tv_nsec, d, d);
           */
    return d;
}

static int read_dummy(void *handle, unsigned int sampling_rate, time_t timeout,
                      int format, double *buffer, size_t data_size,
                      unsigned int *points_per_channel, void *unused) {
    static struct timespec last = { 0, 0};
    struct timespec current;
    (void)handle;
    (void)sampling_rate;
    (void)timeout;
    (void)unused;
    assert(DAQmx_Val_GroupByChannel == format);

    assert(30 <= data_size);
    *points_per_channel = 10;
    /* sleep(3); */
    memcpy(buffer, TEST_ANALOG_DATA, 30 * sizeof(double));
    /* usleep(200000); */

    clock_gettime(CLOCK_REALTIME, &current);
    if(last.tv_sec != 0) {
        assert(0.1 > timediff(&last, &current));
        last = current;
    }
    usleep(100);

    return 0;
}

static void *ni_thread_main(void *opaque_info) {
    input_data_t *info = (input_data_t *)opaque_info;
    unsigned int points_pc;
    const unsigned int num_channels = 3;
    double analog_data[BUFFER_SAMPLES_PER_CHANNEL * num_channels];
    digival_t digital_data[BUFFER_SAMPLES_PER_CHANNEL * num_channels];
    (void)digital_data;
    uint64_t timestamp = 0;

    while(running) {
        wait_read_barrier();
        if(!running) {
            break;
        }
        reset_ready_handlers();
        /* notify_data_unavailable(); */
        read_dummy(NULL, SAMPLING_RATE, 0, DAQmx_Val_GroupByChannel, analog_data,
                   1000, &points_pc, NULL);
        memcpy(digital_data, TEST_DIGITAL_DATA, 30 * sizeof(digival_t));
        /*
        for i = 1 to n:
            check_trigger_signal(i)
            */
        info->timestamp_nanos = timestamp;
        info->points_per_channel = points_pc;
        info->num_channels = num_channels;
        info->analog_data = analog_data;
        info->digital_data = digital_data;
        printf("NI: read successful\n");
        timestamp += TIME_S * points_pc / SAMPLING_RATE;
        notify_data_available();
    }
    return NULL;
}

static void launch_handler_thread(input_data_t *data_info, int conn_fd) {
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

static void wait_for_connections(input_data_t *data_info) {
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
    assert(0 == err || -1 == err);
    if(0 != err) {
        close(server_sock);
        printf("ERROR: bind: %s\n", strerror(errno));
        exit(1);
    }

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
    input_data_t data_info;
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

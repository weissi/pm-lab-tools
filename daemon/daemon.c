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
#include <inttypes.h>

#ifdef WITH_NI
#include <NIDAQmxBase.h>
#endif

#include <assert.h>

#include "common.h"
#include "daemon.h"
#include "sync.h"
#include "handler.h"
#include <common/conf.h>

#define DAQmx_Val_GroupByChannel 0
#define SERVER_PORT 12345
#define LISTEN_QUEUE_LEN 8
#define BUFFER_SAMPLES_PER_CHANNEL 30000

#include "test_data.h"
static const digival_t TEST_DIGITAL_DATA[240000] = { 0 };

volatile bool running = true;
static void sig_hnd() {
    printf("Ctrl+C caught, exiting...\n");
    running = false;
}

typedef struct {
    void *opaque_task_handle;
    void *opaque_error;
    bool failed;
} data_acq_info_t;

static void finish_ni(data_acq_info_t *dai) {
#ifdef WITH_NI
    TaskHandle *h = (TaskHandle *)dai->opaque_task_handle;
    char errBuff[2048] = { 0 };
    int32 ni_errno = *((int32 *)dai->opaque_error);

    if(DAQmxFailed(ni_errno)) {
        DAQmxBaseGetExtendedErrorInfo(errBuff, 2048);
    }

    if(h != 0) {
        DAQmxBaseStopTask (*h);
        DAQmxBaseClearTask (*h);
    }

    if( DAQmxFailed(ni_errno) ) {
        printf ("DAQmxBase Error %d %s\n", (int)ni_errno, errBuff);
    }

    free(dai->opaque_task_handle);
    free(dai->opaque_error);
    free(dai);
#endif
}

#define \
    CHK(functionCall) { \
        printf("NI call...\n"); fflush(stdout); \
        if( DAQmxFailed(*((int32 *)dai->opaque_error)=(functionCall)) ) { \
            goto err; \
        } \
    }
static data_acq_info_t *init_ni(void) {
    data_acq_info_t *dai = malloc(sizeof *dai);
    dai->failed = false;
#ifdef WITH_NI
    TaskHandle *h = malloc(sizeof *h);
    assert(NULL != h);

    dai->opaque_task_handle = h;
    dai->opaque_error = malloc(sizeof(int32));
    assert(NULL != dai->opaque_error);

    CHK(DAQmxBaseCreateTask("analog-inputs", h));
    CHK(DAQmxBaseCreateAIVoltageChan(*h, ni_channels, NULL, DAQmx_Val_Diff,
                                     U_MIN, U_MAX, DAQmx_Val_Volts, NULL));
    CHK(DAQmxBaseCfgSampClkTiming(*h, CLK_SRC, SAMPLING_RATE,
                                  DAQmx_Val_Rising, DAQmx_Val_ContSamps,
                                  0));
    CHK(DAQmxBaseStartTask(*h));
    goto success;
err:
    dai->failed = true;
    return dai;
success:
#endif
    return dai;
}

int read_dummy(void *handle, unsigned int sampling_rate,
               time_t timeout, int format, double *buffer,
               size_t data_size,
               unsigned int *points_per_channel,
               void *unused) {
    static struct timespec t_next = { 0 };

    assert(NULL == handle);
    assert(30000 == sampling_rate);
    (void)timeout;
    assert(DAQmx_Val_GroupByChannel == format);
    assert(sizeof(TEST_ANALOG_DATA)/sizeof(double) <= data_size);
    *points_per_channel = 30000;
    (void)unused;

    if(0 == t_next.tv_sec) {
        clock_gettime(CLOCK_REALTIME, &t_next);
    }

    memcpy(buffer, TEST_ANALOG_DATA, 30 * sizeof(double));
    t_next.tv_sec += 1;
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &t_next, NULL);

    return 0;
}

static int read_ni(data_acq_info_t *dai, const size_t data_size,
                   double *analog_data, unsigned int *points_pc_long) {
#ifdef WITH_NI
    int32 points_pc = 0;
    TaskHandle *h = (TaskHandle *)dai->opaque_task_handle;
    if(dai->failed) {
        return EIO;
    }
    *((int32 *)dai->opaque_error) =
        DAQmxBaseReadAnalogF64(*h,
                               SAMPLING_RATE,
                               TIMEOUT,
                               DAQmx_Val_GroupByChannel,
                               analog_data,
                               data_size,
                               &points_pc,
                               NULL);
    if(DAQmxFailed(*((int32 *)dai->opaque_error))) {
        dai->failed = true;
        return EIO;
    }
    *points_pc_long = points_pc;
    return 0;
#endif
#ifndef WITH_NI
    read_dummy(dai, SAMPLING_RATE, TIMEOUT,
               DAQmx_Val_GroupByChannel, analog_data,
               data_size, points_pc_long, NULL);
    return 0;
#endif
}

static void *ni_thread_main(void *opaque_info) {
    int err;
    input_data_t *info = (input_data_t *)opaque_info;
    unsigned int points_pc;
    const unsigned int num_channels = 8;
    const size_t data_size = BUFFER_SAMPLES_PER_CHANNEL * num_channels;
    double analog_data[data_size];
    digival_t digital_data[data_size];
    (void)digital_data;
    uint64_t timestamp = 0;
    data_acq_info_t *h = init_ni();

    while(running) {
        wait_read_barrier();
        if(!running) {
            break;
        }
        reset_ready_handlers();
        /* notify_data_unavailable(); */
        err = read_ni(h, data_size, analog_data, &points_pc);
        if (0 != err) {
            break;
        }
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
        timestamp += ((uint64_t)TIME_S) * ((uint64_t)points_pc) / ((uint64_t)SAMPLING_RATE);
        printf("NI: read successful, ts = %"PRIu64"\n", timestamp);
        notify_data_available();
    }

    finish_ni(h);
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
        exit(EXIT_FAILURE);
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

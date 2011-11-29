#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "daemon.h"
#include "sync.h"

void *handler_thread_main(void *opaque_info) {
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
        printf("Client %ld received analog_data %ld\n", pthread_self(), (long int)last_data);
        err = write(conn_fd, "DATA AVAILABLE, PRESS ENTER\n", 28);
        if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(errno));
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
            printf("Client %ld read failed: %s\n", pthread_self(), strerror(errno));
            break;
        }

        err = write(conn_fd, "THANKS\n", 7);
        if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(errno));
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


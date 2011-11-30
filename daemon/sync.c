#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "daemon.h"
#include "sync.h"

#define START_TIMING(t) (t) = time(NULL)
#define STOP_TIMING(t) (t) = (time(NULL) - (t))
#define PRINT_TIMING(t,n) printf("[%ld] "n": %lds\n", pthread_self(), (long int)(t))

static pthread_mutex_t __mutex_read = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t __cond_read = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t __mutex_data = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t __cond_data = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t __mutex_handlers = PTHREAD_MUTEX_INITIALIZER;
static unsigned int __data_available = 0;
static unsigned int __ready_handlers = 0;
static unsigned int __available_handlers = 0;

void abs_wait_timeout(struct timespec *abs_timeout) {
    struct timespec wait_timeout = WAIT_TIMEOUT;
    int err = clock_gettime(CLOCK_REALTIME, abs_timeout);
    assert(0 == err);
    if(abs_timeout->tv_nsec + wait_timeout.tv_nsec >= 1L*TIME_S) {
        /* take care tv_nsec does not get greater or equal one second */
        abs_timeout->tv_sec += wait_timeout.tv_sec + 1;
        abs_timeout->tv_nsec += (wait_timeout.tv_nsec - (1L*TIME_S));
        assert(abs_timeout->tv_nsec + wait_timeout.tv_nsec < (1L*TIME_S));
    } else {
        abs_timeout->tv_sec += wait_timeout.tv_sec;
        abs_timeout->tv_nsec += wait_timeout.tv_nsec;
    }
}

void wait_read_barrier(void) {
    int err;
    time_t timer;
    struct timespec abs_timeout;

    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);
    while(running && __ready_handlers < get_available_handlers()) {
        abs_wait_timeout(&abs_timeout);
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

void notify_read_barrier(void) {
    int err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);
    __ready_handlers++;
    pthread_cond_broadcast(&__cond_read);
    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
    printf("NOTIFIED READ BARRIER\n");
}

time_t wait_data_available(time_t last_data) {
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

void notify_data_available(void) {
    printf("DA\n");
    fflush(stdout);
    pthread_mutex_lock(&__mutex_data);
    __data_available++;
    pthread_mutex_unlock(&__mutex_data);
}
/*
void notify_data_unavailable(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_data);
    assert(0 == err);
    __data_available = 0;
    err = pthread_mutex_unlock(&__mutex_data);
    assert(0 == err);
}
*/
void inc_available_handlers(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);
    __available_handlers++;
    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);
    notify_read_barrier();
}

void dec_available_handlers(void) {
    int err;
    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);
    __available_handlers--;
    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);
    notify_read_barrier();
}

unsigned int get_available_handlers(void) {
    int err;
    unsigned int handlers;

    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);

    handlers = __available_handlers;

    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);

    return handlers;
}

void reset_ready_handlers(void) {
    int err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);
    __ready_handlers = 0;
    pthread_cond_broadcast(&__cond_read);
    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
}

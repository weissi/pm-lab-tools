#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>
#include <stdlib.h>

#include <pbl.h>

#include "common.h"
#include "daemon.h"
#include "sync.h"

#define START_TIMING(t) (t) = time(NULL)
#define STOP_TIMING(t) (t) = (time(NULL) - (t))
#define PRINT_TIMING(t,n) printf("[%lu] "n": %lds\n", \
                                 (unsigned long int)pthread_self(), \
                                 (long int)(t))

static pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t __cond_read = PTHREAD_COND_INITIALIZER;
static pthread_cond_t __cond_data = PTHREAD_COND_INITIALIZER;
static pthread_cond_t __cond_dead_handler = PTHREAD_COND_INITIALIZER;

static PblSet *__done_handler_set = NULL; /* handlers that have done the current
                                             copy operation */
static PblSet *__pending_handler_set = NULL; /* handlers that are active but yet
                                                have to copy the current data */
static PblSet *__active_handler_set = NULL; /* all handlers that get data */
static PblSet *__alive_handler_set = NULL; /* all handlers that are alive */
static PblSet *__dead_handler_set = NULL; /* all handlers that are dead
                                             (pthread_joinable) */

/* __done_handler_set + __pending_handler_set = __active_handler_set
 * __active_handler_set € __alive_handler_set
 */

static int pthread_hash(const void *element) {
    return (int)*(pthread_t *)element;
}

static int pthread_compare(const void *prev, const void *next) {
    pthread_t p;
    pthread_t n;
    int ret;

    if(NULL == prev) {
        return 1;
    }
    if(NULL == next) {
        return -1;
    }

    p = **(pthread_t **)prev;
    n = **(pthread_t **)next;

    if(0 != pthread_equal(p, n)) {
        ret = 0;
    } else if(p < n) {
        ret = -1;
    } else {
        ret = 1;
    }

    return ret;
}

void init_sync(void) {
    __pending_handler_set = pblSetNewHashSet();
    __done_handler_set = pblSetNewHashSet();
    __active_handler_set = pblSetNewHashSet();
    __alive_handler_set = pblSetNewHashSet();
    __dead_handler_set = pblSetNewHashSet();

    pblSetSetCompareFunction(__pending_handler_set, pthread_compare);
    pblSetSetCompareFunction(__done_handler_set, pthread_compare);
    pblSetSetCompareFunction(__active_handler_set, pthread_compare);
    pblSetSetCompareFunction(__alive_handler_set, pthread_compare);
    pblSetSetCompareFunction(__dead_handler_set, pthread_compare);

    pblSetSetHashValueFunction(__pending_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__done_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__active_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__alive_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__dead_handler_set, pthread_hash);
}

void finish_sync(void) {
    pblSetFree(__pending_handler_set);
    pblSetFree(__done_handler_set);
    pblSetFree(__active_handler_set);
    pblSetFree(__alive_handler_set);
    pblSetFree(__dead_handler_set);
}

static void debug_set(PblSet *set) {
    if(0 == pblSetSize(set)) {
        printf("<empty>");
        return;
    }
    for(int i=0; i<pblSetSize(set); i++) {
        pthread_t *e = (pthread_t *)pblSetGet(set, i);
        assert(NULL != e);
        printf("%lu, ", (unsigned long int)*e);
    }
}
static void debug_sets(const char *debug_str) {
    if(NULL != debug_str) {
        printf("%s\n", debug_str);
    }
    printf("---------------------------------------------------------\n");
    printf("ALIVE SET:   ");
    debug_set(__alive_handler_set);
    printf("\n");
    printf("DEAD SET:   ");
    debug_set(__dead_handler_set);
    printf("\n");
    printf("ACTIVE SET:  ");
    debug_set(__active_handler_set);
    printf("\n");
    printf("DONE SET:    ");
    debug_set(__done_handler_set);
    printf("\n");
    printf("PENDING SET: ");
    debug_set(__pending_handler_set);
    printf("\n");
    printf("---------------------------------------------------------\n");
    fflush(stdout);
}

void abs_wait_timeout(struct timespec *abs_timeout) {
#ifndef __MACH__
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
#else
    struct timeval tv;
    struct timespec ts;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + 1;
    ts.tv_nsec = 0;
    *abs_timeout = ts;
#endif
}

void wait_read_barrier(void) {
    int err;
    time_t timer;
    struct timespec abs_timeout;

    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    while(running &&
          pblSetEquals(__done_handler_set, __active_handler_set) == 0) {
        abs_wait_timeout(&abs_timeout);
        err = pthread_cond_timedwait(&__cond_read, &__mutex, &abs_timeout);
        assert(0 == err || ETIMEDOUT == err);

        if(ETIMEDOUT == err) {
            debug_sets("wait_read_barrier (TIMEOUT!)");
        }
    }

    assert(0 == pblSetSize(__pending_handler_set));
    assert(pblSetEquals(__done_handler_set, __active_handler_set) > 0);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);
    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_read_barrier");
}

void notify_read_barrier(void) {
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    err = pthread_cond_broadcast(&__cond_read);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);
}

void set_ready(void) {
    pthread_t self = pthread_self();
    pthread_t *persistent_self;
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    persistent_self = pblSetGetElement(__active_handler_set, &self);
    assert(NULL != persistent_self);
    pblSetAdd(__done_handler_set, persistent_self);

    err = pblSetRemoveElement(__pending_handler_set, persistent_self);
    assert(0 != err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    notify_read_barrier();
}

void wait_data_available(void) {
    int err;
    struct timespec abs_timeout;
    time_t timer;
    pthread_t self = pthread_self();

    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);
    while(running &&
          0 == pblSetContains(__pending_handler_set, &self)) {
        /* waiting here as long daemon is running AND
         * pending does not contain our handler */
        abs_wait_timeout(&abs_timeout);
        err = pthread_cond_timedwait(&__cond_data, &__mutex, &abs_timeout);
        assert(0 == err || ETIMEDOUT == err);

        if(ETIMEDOUT == err) {
            debug_sets("wait_data_available (TIMEOUT!)");
        }
    }
    assert(!running || 0 != pblSetContains(__pending_handler_set, &self));

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_data_available");
}

void notify_data_available(void) {
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    err = pthread_cond_broadcast(&__cond_data);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);
}

void inc_available_handlers(void) {
    pthread_t self = pthread_self();
    int err;
    pthread_t *persistent_self = malloc(sizeof *persistent_self);
    assert(NULL != persistent_self);

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    *persistent_self = self;
    pblSetAdd(__alive_handler_set, persistent_self);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    notify_read_barrier();
}

void dec_available_handlers(void) {
    pthread_t self = pthread_self();
    pthread_t *persistent_self;
    pthread_t *persistent_self_pend;
    pthread_t *persistent_self_done;
    pthread_t *persistent_self_active;
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    persistent_self = pblSetGetElement(__alive_handler_set, &self);
    persistent_self_pend = pblSetGetElement(__pending_handler_set,
                                            persistent_self);
    persistent_self_done = pblSetGetElement(__done_handler_set,
                                            persistent_self);
    persistent_self_active = pblSetGetElement(__active_handler_set,
                                              persistent_self);

    assert(NULL != persistent_self);
    assert(NULL == persistent_self_pend ||
           persistent_self == persistent_self_pend);
    assert(NULL == persistent_self_done ||
           persistent_self == persistent_self_done);
    assert(NULL == persistent_self_active ||
           persistent_self == persistent_self_active);

    err = pblSetRemoveElement(__alive_handler_set, persistent_self);
    assert(0 != err);
    pblSetRemoveElement(__pending_handler_set, persistent_self);
    pblSetRemoveElement(__done_handler_set, persistent_self);
    pblSetRemoveElement(__active_handler_set, persistent_self);

    pblSetAdd(__dead_handler_set, persistent_self);

    err = pthread_cond_broadcast(&__cond_dead_handler);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    notify_read_barrier();
}

void reset_ready_handlers(void) {
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    err = pblSetAddAll(__active_handler_set, __alive_handler_set);
    assert(err >= 0);

    err = pblSetAddAll(__pending_handler_set, __active_handler_set);
    assert(err >= 0);

    pblSetClear(__done_handler_set);

    err = pthread_cond_broadcast(&__cond_read);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);
}

pthread_t *wait_dead_handler(void) {
    pthread_t *dead_thread;
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    while(0 == pblSetSize(__dead_handler_set)) {
        err = pthread_cond_wait(&__cond_dead_handler, &__mutex);
        assert(0 == err);
    }

    dead_thread = (pthread_t *)pblSetGet(__dead_handler_set, 0);
    assert(NULL != dead_thread);

    err = pblSetRemoveElement(__dead_handler_set, dead_thread);
    assert(0 != err);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    return dead_thread;
}

bool have_alive_threads(void) {
    bool ret;
    int err;

    err = pthread_mutex_lock(&__mutex);
    assert(0 == err);

    ret = pblSetSize(__alive_handler_set);

    err = pthread_mutex_unlock(&__mutex);
    assert(0 == err);

    assert(ret >= 0);
    return ret > 0;
}

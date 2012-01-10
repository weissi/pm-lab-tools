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

static pthread_mutex_t __mutex_read = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t __cond_read = PTHREAD_COND_INITIALIZER;
//static pthread_mutex_t __mutex_data = __mutex_read; PTHREAD_MUTEX_INITIALIZER;
#define __mutex_data __mutex_read
static pthread_cond_t __cond_data = PTHREAD_COND_INITIALIZER;
//static pthread_mutex_t __mutex_handlers = __mutex_read; PTHREAD_MUTEX_INITIALIZER;
#define __mutex_handlers __mutex_read

static PblSet *__done_handler_set = NULL; /* handlers that have done the current
                                             copy operation */
static PblSet *__pending_handler_set = NULL; /* handlers that are active but yet
                                                have to copy the current data */
static PblSet *__active_handler_set = NULL; /* all handlers that get data */
static PblSet *__alive_set = NULL; /* all handlers that are alive */

/* __done_handler_set + __pending_handler_set = __active_handler_set
 * __active_handler_set € __alive_set
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

    printf("pthread_compare(%lu, %lu) = %d;\n",
           (unsigned long int)p,
           (unsigned long int)n,
           ret);

    return ret;
}

void init_sync(void) {
    __pending_handler_set = pblSetNewHashSet();
    __done_handler_set = pblSetNewHashSet();
    __active_handler_set = pblSetNewHashSet();
    __alive_set = pblSetNewHashSet();

    pblSetSetCompareFunction(__pending_handler_set, pthread_compare);
    pblSetSetCompareFunction(__done_handler_set, pthread_compare);
    pblSetSetCompareFunction(__active_handler_set, pthread_compare);
    pblSetSetCompareFunction(__alive_set, pthread_compare);

    pblSetSetHashValueFunction(__pending_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__done_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__active_handler_set, pthread_hash);
    pblSetSetHashValueFunction(__alive_set, pthread_hash);
}

void finish_sync(void) {
    pblSetFree(__pending_handler_set);
    pblSetFree(__done_handler_set);
    pblSetFree(__active_handler_set);
    pblSetFree(__alive_set);
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
    debug_set(__alive_set);
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
    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);

    while(running && pblSetEquals(__done_handler_set, __active_handler_set) == 0) {
        abs_wait_timeout(&abs_timeout);
        err = pthread_cond_timedwait(&__cond_read, &__mutex_read, &abs_timeout);
        assert(0 == err || ETIMEDOUT == err);

        debug_sets("wait_read_barrier (while waiting)");
    }

    assert(0 == pblSetSize(__pending_handler_set));
    assert(pblSetEquals(__done_handler_set, __active_handler_set) > 0);

    debug_sets("wait_read_barrier (after waiting)");

    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_read_barrier");
}

void notify_read_barrier(void) {
    int err;

    printf("Thread %lu: NOTIFY_READ_BARRIER\n",
           (unsigned long int)pthread_self());

    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);

    err = pthread_cond_broadcast(&__cond_read);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);

    printf("NOTIFIED READ BARRIER\n");
}

void set_ready(void) {
    pthread_t self = pthread_self();
    pthread_t *persistent_self;
    int err;

    printf("Thread %lu: READY\n",
           (unsigned long int)pthread_self());

    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);

    persistent_self = pblSetGetElement(__active_handler_set, &self);
    assert(NULL != persistent_self);
    pblSetAdd(__done_handler_set, persistent_self);

    err = pblSetRemoveElement(__pending_handler_set, persistent_self);
    assert(0 != err);

    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);

    notify_read_barrier();
}

void wait_data_available(void) {
    int err;
    struct timespec abs_timeout;
    time_t timer;
    pthread_t self = pthread_self();

    START_TIMING(timer);
    err = pthread_mutex_lock(&__mutex_data);
    assert(0 == err);
    while(running &&
          0 == pblSetContains(__pending_handler_set, &self)) {
        /* waiting here as long daemon is running AND
         * pending does not contain our handler */
        abs_wait_timeout(&abs_timeout);
        err = pthread_cond_timedwait(&__cond_data, &__mutex_data, &abs_timeout);
        assert(0 == err || ETIMEDOUT == err);

        debug_sets("wait_data_available (while waiting)");
    }
    assert(0 != pblSetContains(__pending_handler_set, &self));

    err = pthread_mutex_unlock(&__mutex_data);
    assert(0 == err);

    STOP_TIMING(timer);
    PRINT_TIMING(timer, "wait_data_available");
}

void notify_data_available(void) {
    int err;

    err = pthread_mutex_lock(&__mutex_data);
    assert(0 == err);

    err = pthread_cond_broadcast(&__cond_data);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex_data);
    assert(0 == err);
}

void inc_available_handlers(void) {
    pthread_t self = pthread_self();
    int err;
    pthread_t *persistent_self = malloc(sizeof *persistent_self);
    assert(NULL != persistent_self);

    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);

    *persistent_self = self;
    pblSetAdd(__alive_set, persistent_self);

    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);

    notify_read_barrier();
    printf("NEW HANDLER: %ld\n", self);
}

void dec_available_handlers(void) {
    pthread_t self = pthread_self();
    pthread_t *persistent_self;
    pthread_t *persistent_self_pend;
    pthread_t *persistent_self_done;
    pthread_t *persistent_self_active;
    int err;

    err = pthread_mutex_lock(&__mutex_handlers);
    assert(0 == err);

    persistent_self = pblSetGetElement(__alive_set, &self);
    persistent_self_pend = pblSetGetElement(__pending_handler_set, &self);
    persistent_self_done = pblSetGetElement(__done_handler_set, &self);
    persistent_self_active = pblSetGetElement(__active_handler_set, &self);

    assert(NULL != persistent_self);
    assert(NULL == persistent_self_pend || persistent_self == persistent_self_pend);
    assert(NULL == persistent_self_done || persistent_self == persistent_self_done);
    assert(NULL == persistent_self_active || persistent_self == persistent_self_active);

    assert(0 != pblSetRemoveElement(__alive_set, &self));
    pblSetRemoveElement(__pending_handler_set, &self);
    pblSetRemoveElement(__done_handler_set, &self);
    pblSetRemoveElement(__active_handler_set, &self);

    free(persistent_self);

    err = pthread_mutex_unlock(&__mutex_handlers);
    assert(0 == err);

    notify_read_barrier();
    printf("DEL HANDLER: %ld\n", self);
}

void reset_ready_handlers(void) {
    int err;

    err = pthread_mutex_lock(&__mutex_read);
    assert(0 == err);

    err = pblSetAddAll(__active_handler_set, __alive_set);
    if(PBL_ERROR_OUT_OF_MEMORY == pbl_errno) {
        printf("--------> OOM\n");
    } else if(PBL_ERROR_PARAM_COLLECTION == pbl_errno) {
        printf("--------> NO ITER\n");
    } else if(PBL_ERROR_CONCURRENT_MODIFICATION == pbl_errno) {
        printf("--------> CONC MOD\n");
    } else {
        printf("--------> UNKNOWN\n");
    }
    assert(err >= 0);

    err = pblSetAddAll(__pending_handler_set, __active_handler_set);
    assert(err >= 0);

    pblSetClear(__done_handler_set);

    debug_sets("reset_ready_handlers (new condition)");

    err = pthread_cond_broadcast(&__cond_read);
    assert(0 == err);

    err = pthread_mutex_unlock(&__mutex_read);
    assert(0 == err);
}

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

#include "measured-data.pb-c.h"

#define MAGIC_DATA_SET "THE MATRIX HAS YOU!!"

static void timestamp_from_timespec(Timestamp *dest, struct timespec *src) {
    timestamp__init(dest);
    if (NULL != src) {
        dest->sec = src->tv_sec;
        dest->nsec = src->tv_nsec;
    }
}
/*
static void timespec_from_timestamp(struct timespec *dest, Timestamp *src) {
    if (NULL != src) {
        dest->tv_sec = src->sec;
        dest->tv_nsec = src->nsec;
    }
}
*/

static void encode_datapoints(unsigned int len,
                              double *analog_data,
                              digival_t *digital_data,
                              DataPoints *msg_dps) {
    data_points__init(msg_dps);

    msg_dps->n_analog_data = len;
    msg_dps->analog_data = analog_data;
    msg_dps->n_digital_data = len;
    assert(sizeof(digival_t) == sizeof(protobuf_c_boolean));
    msg_dps->digital_data = (protobuf_c_boolean *)digital_data;
}


static int write_dataset(int fd,
                         unsigned int num_channels,  /* 3 */
                         unsigned int channel_ids[], /* 1, 4, 16 */
                         unsigned int len,           /* samples per channel */
                         struct timespec time,
                         double *analog_data,        /* chan1val1, chan1val2,
                                                      * chan2val1, chan2val2,
                                                      * ... */
                         digival_t *digital_data) {  /* just as analog_data */
    int err;
    DataSet msg_ds = DATA_SET__INIT;
    Timestamp msg_time;
    DataPoints **msg_dps = alloca(sizeof(DataPoints *) * num_channels);
    assert(NULL != msg_dps);

    for (int i=0; i<num_channels; i++) {
        msg_dps[i] = alloca(sizeof(DataPoints));
        assert(NULL != msg_dps[i]);
    }

    msg_ds.time = &msg_time;
    timestamp_from_timespec(msg_ds.time, &time);

    for (int i=0; i<num_channels; i++) {
        const unsigned int offset = channel_ids[i] * len;
        encode_datapoints(len, analog_data+offset, digital_data+offset, msg_dps[i]);
    }

    msg_ds.n_channel_data = num_channels;
    msg_ds.channel_data = msg_dps;

    const uint32_t msg_len = data_set__get_packed_size(&msg_ds);
    void *buf = malloc(msg_len);
    assert(NULL != buf);

    data_set__pack(&msg_ds, buf);

    err = write(fd, MAGIC_DATA_SET, sizeof(MAGIC_DATA_SET));
    if (err < 0) {
        goto finally;
    } else {
        assert(sizeof(MAGIC_DATA_SET) == err);
    }

    err = write(fd, &msg_len, sizeof(uint32_t));
    if (err < 0) {
        goto finally;
    } else {
        assert(sizeof(uint32_t) == err);
    }

    err = write(fd, buf, msg_len);
    if (err < 0) {
        goto finally;
    } else {
        assert(msg_len == err);
    }

    err = 0;

finally:
    free(buf);
    return err;
}

void *handler_thread_main(void *opaque_info) {
    unsigned int my_channels[] = { 0, 1, 2 };
    unsigned int my_num_channels = 3;
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

        input_data_t *data_info = info->data_info;
        err = write_dataset(conn_fd, my_num_channels, my_channels,
                            data_info->points_per_channel, data_info->time,
                            data_info->analog_data, data_info->digital_data);
        if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(errno));
            break;
        }
        assert(0 == err);

        notify_read_barrier();
    }

    dec_available_handlers();

    err = close(conn_fd);
    assert(0 == err);
    free(opaque_info);
    return NULL;
}


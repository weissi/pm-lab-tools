#define _GNU_SOURCE
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
#define BUF_SIZE 256

typedef struct {
    pthread_mutex_t lock;
    input_data_t *buffer;
    const size_t max_elems;
    input_data_t *start;
    size_t count;
} buffer_desc_t;

typedef struct {
    buffer_desc_t *buffer_desc;
    volatile bool *handler_running;
    int conn_fd;
    unsigned int num_channels;
    unsigned int *channels;
} sender_thread_info_t;

/*
 * PROTOCOL BUFFER ENCODING AND SENDING
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
                         uint64_t timestamp,
                         double *analog_data,        /* chan1val1, chan1val2,
                                                      * chan2val1, chan2val2,
                                                      * ... */
                         digival_t *digital_data) {  /* just as analog_data */
    int err, ret;
    DataSet msg_ds = DATA_SET__INIT;
    DataPoints **msg_dps = alloca(sizeof(DataPoints *) * num_channels);
    assert(NULL != msg_dps);

    for (int i=0; i<num_channels; i++) {
        msg_dps[i] = alloca(sizeof(DataPoints));
        assert(NULL != msg_dps[i]);
    }

    msg_ds.timestamp_nanos = timestamp;

    for (int i=0; i<num_channels; i++) {
        const unsigned int offset = channel_ids[i] * len;
        encode_datapoints(len,
                          analog_data+offset,
                          digital_data+offset,
                          msg_dps[i]);
    }

    msg_ds.n_channel_data = num_channels;
    msg_ds.channel_data = msg_dps;

    const uint32_t msg_len = data_set__get_packed_size(&msg_ds);
    void *buf = malloc(msg_len);
    assert(NULL != buf);

    data_set__pack(&msg_ds, buf);

    ret = 0;
    err = write(fd, MAGIC_DATA_SET, sizeof(MAGIC_DATA_SET));
    if (err < 0) {
        goto finally;
    } else {
        assert(sizeof(MAGIC_DATA_SET) == err);
    }
    ret += err;

    err = write(fd, &msg_len, sizeof(uint32_t));
    if (err < 0) {
        goto finally;
    } else {
        assert(sizeof(uint32_t) == err);
    }
    ret += err;

    err = write(fd, buf, msg_len);
    if (err < 0) {
        goto finally;
    } else {
        assert(msg_len == err);
    }
    ret += err;
    err = ret;

finally:
    free(buf);
    return err;
}

/*
 * BUFFER MANAGEMENT
 */
static int copy_to_buffer(buffer_desc_t *buf,
                          input_data_t *in) {
    int ret, err;

    err = pthread_mutex_lock(&buf->lock);
    assert(0 == err);

    while(true) {
        assert(buf->count <= buf->max_elems);
        assert(buf->start <= buf->buffer+buf->max_elems);

        if(buf->start+buf->count+1 < buf->buffer+buf->max_elems) {
            /* new element will fit in the buffer */
            unsigned int samples = in->num_channels * in->points_per_channel;
            input_data_t *dest = buf->start + buf->count;

            *dest = *in;

            /* copy analog data */
            dest->analog_data = malloc(samples * sizeof(double));
            assert(NULL != dest->analog_data);
            memcpy(dest->analog_data,
                   in->analog_data,
                   samples * sizeof(double));

            /* copy digital data */
            dest->digital_data = malloc(samples * sizeof(digival_t));
            assert(NULL != dest->digital_data);
            memcpy(dest->digital_data,
                   in->digital_data,
                   samples * sizeof(digival_t));

            buf->count++;

            ret = 0;
            break; /* success */
        } else if(buf->start != buf->buffer) {
            /* move to front and try again */
            printf("MOVE TO FRONT!\n");
            buf->start = memmove(buf->buffer,
                                 buf->start,
                                 sizeof(input_data_t) * buf->count);
            continue; /* try again */
        } else {
            /* no buffer space left :-( */
            ret = ENOBUFS;
            break;
        }
        break;
    }

    err = pthread_mutex_unlock(&buf->lock);
    assert(0 == err);

    return ret;
}

static int write_buf_element(int fd,
                             buffer_desc_t *buf,
                             unsigned int channel_ids[],
                             unsigned int channel_count) {
    int err, ret;
    input_data_t in;

    err = pthread_mutex_lock(&buf->lock);
    assert(0 == err);

    if(buf->count > 0) {
        in = *((input_data_t *)buf->start);
        buf->count--;
        buf->start++;

        ret = -1;
    } else {
        ret = 0;
    }

    err = pthread_mutex_unlock(&buf->lock);
    assert(0 == err);

    if(0 != ret) {
        ret = write_dataset(fd,
                            channel_count,
                            channel_ids,
                            in.points_per_channel,
                            in.timestamp_nanos,
                            in.analog_data,
                            in.digital_data);
        free(in.analog_data);
        free(in.digital_data);
    }

    return ret;
}

void free_buffer(buffer_desc_t *buf) {
    int err;

    err = pthread_mutex_lock(&buf->lock);
    assert(0 == err);

    for(size_t i=0; i<buf->count; i++) {
        input_data_t *in = buf->start + i;
        free(in->analog_data);
        free(in->digital_data);
    }
    free(buf->buffer);
    buf->buffer = NULL;

    err = pthread_mutex_unlock(&buf->lock);
    assert(0 == err);
}

/*
 * FUNCTIONALITY
 */
void *handler_sender_main(void *opaque_sender_info) {
    int err;
    sender_thread_info_t *sender_info =
        (sender_thread_info_t *)opaque_sender_info;
    buffer_desc_t *buffer_desc = sender_info->buffer_desc;

    while(*sender_info->handler_running) {
        err = write_buf_element(sender_info->conn_fd,
                                buffer_desc,
                                sender_info->channels,
                                sender_info->num_channels);
        if(0 == err) {
            /* buffer empty */
            err = pthread_yield();
            assert(0 == err);
        } else if(0 > err) {
            /* ERROR */
            printf("Client %ld write failed: %s\n", pthread_self(), strerror(errno));
            break;
        } else {
            assert(0 < err);
        }
    }

    return NULL;
}

void *handler_thread_main(void *opaque_info) {
    unsigned int my_channels[] = { 0, 1, 2 };
    unsigned int my_num_channels = 3;
    handler_thread_info_t *info = (handler_thread_info_t *)opaque_info;
    int err;
    time_t last_data = 0;
    pthread_t sender_thread;
    volatile bool handler_running = true;
    buffer_desc_t buffer_desc = { .buffer = malloc(BUF_SIZE * sizeof(input_data_t))
                                , .lock = PTHREAD_MUTEX_INITIALIZER
                                , .count = 0
                                , .max_elems = BUF_SIZE
                                , .start = 0
                                };
    sender_thread_info_t sender_info = { .buffer_desc = &buffer_desc
                                       , .handler_running = &handler_running
                                       , .conn_fd = info->fd
                                       , .num_channels = my_num_channels
                                       , .channels = my_channels
                                       };
    assert(NULL != buffer_desc.buffer);
    buffer_desc.start = buffer_desc.buffer;

    err = pthread_create(&sender_thread, NULL, handler_sender_main, &sender_info);
    assert(0 == err);

    printf("Handler thread accepted %d\n", info->fd);
    inc_available_handlers();
    err = write(info->fd, "WELCOME ABOARD\n", 15);
    assert(15 == err);

    while(running) {
        last_data = wait_data_available(last_data);
        if(!running) {
            break;
        }

        input_data_t *data_info = info->data_info;
        err = copy_to_buffer(&buffer_desc, data_info);
        if(ENOBUFS == err) {
            /* out of buffer space */
            break;
        }
        assert(0 == err);
        printf("Buffer %p has now %u/%u element(s) starting at %p (offset = %u)\n",
               (void *)buffer_desc.buffer,
               buffer_desc.count,
               buffer_desc.max_elems,
               (void *)buffer_desc.start,
               (buffer_desc.start-buffer_desc.buffer));
        notify_read_barrier();

        err = pthread_tryjoin_np(sender_thread, NULL);
        if(EBUSY != err) {
            break;
        }
    }
    dec_available_handlers();
    handler_running = false;
    err = close(info->fd);
    assert(0 == err);

    err = pthread_join(sender_thread, NULL);
    if(EINVAL != err) {
        assert(0 == err);
    }
    free_buffer(&buffer_desc);

    free(opaque_info);
    return NULL;
}


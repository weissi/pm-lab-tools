/*
 *  Library to read data from pm-lab-tools/daemon
 *
 *  Copyright (C)2011/12, Jonathan Dimond <jonny@dimond.de>
 *                      & Johannes Weiß <uni@tux4u.de>
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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>
#include <netdb.h>

#include <utils.h>

#include "measured-data.pb-c.h"

#include "libpmlab.h"

#define PM_HANDLE_MAGIC_NUMBER 0xC001BABE

typedef struct {
    int magic_number;
    int sockfd;
    uint32_t sampling_rate;
} pm_handle;

void *pm_connect(char *server,
                 char *port,
                 uint32_t *channels,
                 uint32_t num_channels) {
    struct addrinfo hints = { 0 };
    struct addrinfo *result = NULL, *rp = NULL;
    int sockfd;
    int err, i;
    char welcome_msg[sizeof(WELCOME_MSG)];
    uint32_t net_sampling_rate;
    uint32_t net_nc = htonl(num_channels);
    uint32_t *net_channels = alloca(sizeof(uint32_t)*num_channels);
    pm_handle *handle;

    if(NULL==server || NULL==port || NULL==channels) {
        return NULL;
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    err = getaddrinfo(server, port, &hints, &result);
    if(0 != err) {
        return NULL;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype,
                        rp->ai_protocol);
        if(-1 == sockfd) {
            continue;
        }

        if(-1 != connect(sockfd, rp->ai_addr, rp->ai_addrlen)) {
            break;
        }
        close(sockfd);
    }

    if(NULL == rp) {
        return NULL;
    }

    freeaddrinfo(result);

    /* convert integers to network endianess */
    net_nc = htonl(num_channels);
    for(i = 0; i < num_channels; i++) {
        net_channels[i] = htonl(channels[i]);
    }

    /* send channel description */
    err = full_write(sockfd, (char *)&net_nc, sizeof(num_channels));
    assert(sizeof(num_channels) == err);
    err = full_write(sockfd,
                     (char *)net_channels,
                     num_channels*sizeof(uint32_t));

    err = full_read(sockfd, welcome_msg, sizeof(WELCOME_MSG));
    if(sizeof(WELCOME_MSG) != err) {
        return NULL;
    }
    assert(0 == strncmp(WELCOME_MSG, welcome_msg, sizeof(WELCOME_MSG)));

    err = full_read(sockfd, (char *)&net_sampling_rate, sizeof(uint32_t));
    if(sizeof(uint32_t) != err) {
        return NULL;
    }

    /* Only create structure once connection is established */
    handle = (pm_handle *)malloc(sizeof(pm_handle));
    handle->magic_number = PM_HANDLE_MAGIC_NUMBER;
    handle->sockfd = sockfd;
    handle->sampling_rate = ntohl(net_sampling_rate);

    return handle;
}

uint32_t pm_samplingrate(void *h) {
    return ((pm_handle *)h)->sampling_rate;
}

int pm_read(void *h,
            size_t buffer_sizes,
            double *analog_data,
            digival_t *digital_data,
            unsigned int *ret_samples_read,
            uint64_t *ret_timestamp_nanos) {
    int err;
    pm_handle *handle;
    char magic_data_buffer[sizeof(MAGIC_DATA_SET)];
    uint32_t msg_len, net_msg_len;
    void *msg_buffer;
    int i;
    unsigned int offset;
    DataSet *msg_ds;
    ssize_t ret = 0;
    unsigned int samples_read;

    handle = (pm_handle *)h;
    assert(PM_HANDLE_MAGIC_NUMBER == handle->magic_number);

    err = full_read(handle->sockfd, magic_data_buffer, sizeof(MAGIC_DATA_SET));
    if(0 == err) {
        return 0;
    }
    assert(sizeof(MAGIC_DATA_SET)==err);
    ret += err;
    assert(0==strncmp(MAGIC_DATA_SET,magic_data_buffer,sizeof(MAGIC_DATA_SET)));

    err = full_read(handle->sockfd, (char *)&net_msg_len, sizeof(uint32_t));
    if(0 == err) {
        return 0;
    }
    assert(sizeof(uint32_t)==err);
    ret += err;
    msg_len = ntohl(net_msg_len);

    msg_buffer = malloc(msg_len);
    err = full_read(handle->sockfd, msg_buffer, msg_len);
    if(0 == err) {
        return 0;
    }
    assert(msg_len==err);
    ret += err;

    msg_ds = data_set__unpack(NULL, msg_len, msg_buffer);
    free(msg_buffer);

    offset = 0;
    samples_read = 0;
    for(i = 0; i < msg_ds->n_channel_data; i++) {
        DataPoints *points = msg_ds->channel_data[i];
        unsigned int n_samples = points->n_analog_data;

        assert(n_samples == points->n_analog_data);
        assert(n_samples == points->n_digital_data);
        assert(buffer_sizes >= offset+n_samples);

        if(NULL != analog_data) {
            memcpy(analog_data + offset,
                   points->analog_data,
                   n_samples * sizeof(double));
        }
        if(NULL != digital_data) {
            memcpy(digital_data + offset,
                   points->digital_data,
                   n_samples * sizeof(digival_t));
        }
        offset += n_samples;
        assert (0 == samples_read || n_samples == samples_read);
        samples_read = n_samples;
    }

    if(NULL != ret_timestamp_nanos) {
        *ret_timestamp_nanos = msg_ds->timestamp_nanos;
    }
    if(NULL != ret_samples_read) {
        *ret_samples_read = samples_read;
    }

    data_set__free_unpacked(msg_ds, NULL);

    return ret;
}

void pm_close(void *h) {
    int err;
    pm_handle *handle = (pm_handle *)h;

    assert(PM_HANDLE_MAGIC_NUMBER == handle->magic_number);

    err = close(handle->sockfd);
    assert(0 == err);
    free(handle);
}
/* vim: set fileencoding=utf8 : */

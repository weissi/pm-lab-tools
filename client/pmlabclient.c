#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <netdb.h>

#include <utils.h>

#include "measured-data.pb-c.h"

#include "pmlabclient.h"

#define PM_HANDLE_MAGIC_NUMBER 0xC001BABE

typedef struct {
    int magic_number;
    int sockfd;
} pm_handle;

void *pmlc_connect(char *server,
                   char *port,
                   uint32_t *channels,
                   uint32_t num_channels) {
    struct addrinfo hints = { 0 };
    struct addrinfo *result = NULL, *rp = NULL;
    int sockfd;
    int err, i;
    char welcome_msg[sizeof(WELCOME_MSG)];
    uint32_t net_nc = htonl(num_channels);
    uint32_t *net_channels = alloca(sizeof(uint32_t)*num_channels);
    pm_handle *handle;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    err = getaddrinfo(server, port, &hints, &result);
    if(0 != err) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
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
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
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
    err = full_write(sockfd, (char *)net_channels, num_channels*sizeof(uint32_t));

    err = full_read(sockfd, welcome_msg, sizeof(WELCOME_MSG));
    assert(sizeof(WELCOME_MSG) == err);
    printf("%s\n", welcome_msg);
    assert(0 == strncmp(WELCOME_MSG, welcome_msg, sizeof(WELCOME_MSG)));

    printf("waht up\n");

    /* Only create structure once connection is established */
    handle = (pm_handle *)malloc(sizeof(pm_handle));
    handle->magic_number = PM_HANDLE_MAGIC_NUMBER;
    handle->sockfd = sockfd;

    return handle;
}


int pmlc_read(void *h,
              size_t buffer_sizes,
              double *analog_data,
              digival_t *digital_data,
              unsigned int *samples_read,
              uint64_t *timestamp_nanos) {
    int err;
    pm_handle *handle;
    char magic_data_buffer[sizeof(MAGIC_DATA_SET)];
    uint32_t msg_len, net_msg_len;
    void *msg_buffer;
    int i;
    unsigned int offset;
    DataSet *msg_ds;

    handle = (pm_handle *)h;
    assert(PM_HANDLE_MAGIC_NUMBER == handle->magic_number);

    err = full_read(handle->sockfd, magic_data_buffer, sizeof(MAGIC_DATA_SET));
    assert(sizeof(MAGIC_DATA_SET)==err);
    assert(0==strncmp(MAGIC_DATA_SET, magic_data_buffer, sizeof(MAGIC_DATA_SET)));

    err = full_read(handle->sockfd, (char *)&net_msg_len, sizeof(uint32_t));
    assert(sizeof(uint32_t)==err);
    msg_len = ntohl(net_msg_len);

    msg_buffer = malloc(msg_len);
    err = full_read(handle->sockfd, msg_buffer, msg_len);
    assert(msg_len==err);

    msg_ds = data_set__unpack(NULL, msg_len, msg_buffer);
    free(msg_buffer);

    offset = 0;
    for(i = 0; i < msg_ds->n_channel_data; i++) {
        DataPoints *points = msg_ds->channel_data[i];
        unsigned int n_samples = points->n_analog_data;

        assert(n_samples == points->n_analog_data);
        assert(n_samples == points->n_digital_data);
        assert(buffer_sizes >= offset+n_samples);

        memcpy(analog_data + offset, points->analog_data, n_samples);
        memcpy(digital_data + offset, points->digital_data, n_samples);
        offset += n_samples;
    }

    *samples_read = offset;
    *timestamp_nanos = msg_ds->timestamp_nanos;

    data_set__free_unpacked(msg_ds, NULL);

    return 0;
}

void pmlc_close(void *h) {
    int err;
    pm_handle *handle = (pm_handle *)h;

    assert(PM_HANDLE_MAGIC_NUMBER == handle->magic_number);

    err = close(handle->sockfd);
    assert(0 == err);
    free(handle);
}

int main(int argc, char *argv[])
{
    uint32_t channels[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    unsigned int num_channels = sizeof(channels)/sizeof(uint32_t);
    void *handle = NULL;
    const size_t buffer_sizes = 240000;
    double analog_data[buffer_sizes];
    digival_t digital_data[buffer_sizes];
    unsigned int sample_count;
    uint64_t timestamp;

    if(1 == argc) {
        printf("Connecting to localhost:12345\n");
        handle = pmlc_connect("127.0.0.1", "12345", channels, num_channels);
    } else if(3 == argc) {
        printf("Connecting to %s:%s\n", argv[1], argv[2]);
        handle = pmlc_connect(argv[1], argv[2], channels, num_channels);
    }

    for (int i=0; i<10000; i++) {
        pmlc_read(handle,
                  buffer_sizes,
                  analog_data,
                  digital_data,
                  &sample_count,
                  &timestamp);
        printf("Read %u samples starting at second %"PRIu64"\n",
               sample_count,
               timestamp / 1000000000L);
        for(int j=0; j<10; j++) {
            printf("%f, ", analog_data[j]);
        }
        printf("\n");
    }
    pmlc_close(handle);
}

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>

#include "measured-data.pb-c.h"

#include "pmlabclient.h"

#define PM_HANDLE_MAGIC_NUMBER 0xC001BABE

typedef struct {
    int magic_number;
    int sockfd;
} pm_handle;

static int write_fully(int fd, void *buffer, ssize_t count) {
    ssize_t total_written = 0;
    ssize_t bytes_written = 0;
    while(total_written < count) {
        bytes_written = write(fd, (char *)buffer+total_written, count-total_written);
        if (bytes_written <= 0) {
            return -1;
        }
        total_written += bytes_written;
    }
    return 0;
}

static int read_fully(int fd, void *buffer, ssize_t count) {
    ssize_t total_read = 0;
    ssize_t bytes_read = 0;
    while(total_read < count) {
        bytes_read = read(fd, (char *)buffer+total_read, count-total_read);
        if (bytes_read <= 0) {
            return -1;
        }
        total_read += bytes_read;
    }
    return 0;
}

void *pmlc_connect(char *server,
                   unsigned short port,
                   uint32_t *channels,
                   uint32_t num_channels) {
    struct sockaddr_in server_addr;
    int sockfd;
    int err, i;
    char welcome_msg[sizeof(WELCOME_MSG)];
    uint32_t net_nc = htonl(num_channels);
    uint32_t *net_channels = alloca(sizeof(uint32_t)*num_channels);
    pm_handle *handle;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(0 <= sockfd);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server);
    server_addr.sin_port        = htons(port);

    err = connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    assert(0 == err);

    /* convert integers to network endianess */
    net_nc = htonl(num_channels);
    for(i = 0; i < num_channels; i++) {
        net_channels[i] = htonl(channels[i]);
    }
    /* send channel description */
    err = write_fully(sockfd, &net_nc, sizeof(num_channels));
    assert(0 == err);
    err = write_fully(sockfd, net_channels, num_channels*sizeof(uint32_t));


    err = read_fully(sockfd, welcome_msg, sizeof(WELCOME_MSG));
    assert(0 == err);
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

    err = read_fully(handle->sockfd, magic_data_buffer, sizeof(MAGIC_DATA_SET));
    assert(0==err);
    assert(0==strncmp(MAGIC_DATA_SET, magic_data_buffer, sizeof(MAGIC_DATA_SET)));

    err = read_fully(handle->sockfd, &net_msg_len, sizeof(uint32_t));
    assert(0==err);
    msg_len = ntohl(net_msg_len);

    msg_buffer = malloc(msg_len);
    err = read_fully(handle->sockfd, msg_buffer, msg_len);
    assert(0==err);

    msg_ds = data_set__unpack(NULL, msg_len, msg_buffer);
    free(msg_buffer);

    offset = 0;
    for(i = 0; i < msg_ds->n_channel_data; i++) {
        DataPoints *points = msg_ds->channel_data[i];
        unsigned int n_samples = points->n_analog_data;

        assert(n_samples==msg_ds->channel_data[0]->n_analog_data);
        assert(n_samples==points->n_digital_data);
        assert(buffer_sizes>=offset+n_samples);

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
    uint32_t channels[] = {4};
    void* handle = pmlc_connect("192.168.21.201", 12345, channels , 1);
    size_t buffer_sizes = 4096;
    double analog_data[4096];
    digival_t digital_data[4096];
    unsigned int sample_count;
    uint64_t timestamp;
    int i=0;
    for (i=0; i<10000; i++) {
        pmlc_read(handle,
                  buffer_sizes,
                  analog_data,
                  digital_data,
                  &sample_count,
                  &timestamp);
        printf("Read %u samples starting at second %"PRIu64"\n",
               sample_count,
               timestamp);
        for(int j=0; j<10; j++) {
            printf("%f, ", analog_data[j]);
        }
        printf("\n");
    }
    pmlc_close(handle);
}
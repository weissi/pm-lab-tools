#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>

#include "libpmlab.h"
#include "common/conf.h"

int main(int argc, char *argv[])
{
    /* channels to listen to */
    uint32_t channels[] = { CPU1, TRIGGER1 };
    unsigned int num_channels = sizeof(channels)/sizeof(uint32_t);

    /* buffers, have to be large enough to read from the network */
    const size_t buffer_sizes = 100000;
    double analog_data[buffer_sizes];
    digival_t digital_data[buffer_sizes];

    /* misc */
    void *pm_handle = NULL;
    unsigned int sample_count;
    uint64_t timestamp;
    uint32_t sampling_rate;
    char *server;
    char *port;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    server = argv[1];
    port = argv[2];

    /* connect to server */
    pm_handle = pm_connect(server, port, channels, num_channels);
    if (NULL == pm_handle) {
        fprintf(stderr, "Server connect failed!\n");
        exit(EXIT_FAILURE);
    }

    /* get sampling rate for channels */
    sampling_rate = pm_samplingrate(pm_handle);

    /* read forever */
    while (1) {
        int i, j;
        /* read data from network */
        int err = pm_read(pm_handle,
                          buffer_sizes,
                          analog_data,
                          digital_data,
                          &sample_count,
                          &timestamp);
        if (err < 0) {
            pm_close(pm_handle);
            fprintf(stderr, "Error reading from network!");
            exit(EXIT_FAILURE);
        }
        /* output data to stdout */
        for (i = 0; i < sample_count; i++) {
            double ts = (double)timestamp/1000000000L + (i/(double)sampling_rate);
            printf("%f", ts);
            for (j = 0; j < num_channels; j++) {
                printf(" %f", analog_data[i+j*sample_count]);
            }
            j = ts;
            printf("\n");
        }
    }

    /* close connection to server */
    pm_close(pm_handle);
}

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdbool.h>
#include <signal.h>

#include "libpmlab.h"
#include "common/conf.h"

/* buffers, have to be large enough to read from the network */
#define BUFFER_SIZES  (8 * 50000)
static double analog_data[BUFFER_SIZES] = { 0 };

static bool running = true;

static void sig_hnd() {
    fprintf(stderr, "Ctrl+C caught, exiting...\n");
    running = false;
}

int main(int argc, char *argv[])
{
    /* channels to listen to */
    uint32_t channels[] = {
        PM2_DD,
        PM3_DD,
        PM4_DD,
        PM5_CPU,
        PM6_CPU,
        PM7_CPU,
    };
    unsigned int num_channels = sizeof(channels)/sizeof(uint32_t);

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

    signal(SIGINT, (void (*)(int))sig_hnd);

    /* connect to server */
    pm_handle = pm_connect(server, port, channels, num_channels);
    if (NULL == pm_handle) {
        fprintf(stderr, "Server connect failed!\n");
        exit(EXIT_FAILURE);
    }

    /* get sampling rate for channels */
    sampling_rate = pm_samplingrate(pm_handle);

    /* read forever */
    while (running) {
        int i, j;
        /* read data from network */
        int err = pm_read(pm_handle,
                          BUFFER_SIZES,
                          analog_data,
                          NULL,
                          &sample_count,
                          &timestamp);
        if (0 == err) {
            pm_close(pm_handle);
            fprintf(stderr, "Server closed connection.\n");
            exit(EXIT_FAILURE);
        } else if (err < 0) {
            pm_close(pm_handle);
            fprintf(stderr, "Error reading from network!");
            exit(EXIT_FAILURE);
        }

        /* output data to stdout */
        for (i = 0; i < sample_count; i++) {
            const double ts = (double)timestamp/1000000000L +
                              (i/(double)sampling_rate);
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

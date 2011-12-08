#ifndef PMLABCLIENT
#define PMLABCLIENT

#include <time.h>

#include "common.h"

void *pmlc_connect(char *server,
                   unsigned short port,
                   unsigned int *channels,
                   unsigned int num_channels);

int pmlc_read(void *handle,
              size_t buffer_sizes,
              double *analog_data,
              digival_t *digital_data,
              unsigned int *samples_read,
              uint64_t *timestamp_nanos);

void pmlc_close(void *handle);

/*
void pmlc_highlevel_read(void *handler,
                         size_t buffer_size,
                         double *analog_data,
                         unsigned int data_channel,
                         unsigned int trigger_channel,
                         trigger_type ttype, // embedded(+threshold), digital extern, separate channel(+threshold)
                         bool below,
                         double threshold);

double avs[1025];
digival_t avs[1025];

const unsigned int num_channels = 3;
const unsigned int channels[] = { CPU1, TRIGGER1 };

handle_t *h = pmlc_connect("localhost", 12345, channels, num_channels);
double a_data[8192];
digival_t d_data[8192];

double *d;

while(true) {
    int samples_per_channel;
    int ch_actually_read;
    pmlc_read(h, 8192, a_data, d_data, &samples_per_channel, &ch_actually_read);
    for (int j=0; j<num_channels; j++) {
        for (int i=0; i<samples_per_channel; i++) {
            printf("%f, %u\n", a_data[j*samples_per_channel+i], d_data[j][i]);
        }
    }
}
*/

#endif

/*
 *  Reads and prints data from pm-lab-tools/daemon
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

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

static unsigned int parse_channels(unsigned int argc,
                                   char **argv,
                                   unsigned int max_channels,
                                   uint32_t *all_channels,
                                   uint32_t *chosen_channels) {
    unsigned int active_channels = 0;

    for(unsigned int i=0; i<argc && active_channels <= max_channels; i++) {
        if(0 == strcmp("pm2", argv[i])) {
            chosen_channels[active_channels++] = PM2_DD;
        } else if(0 == strcmp("pm3", argv[i])) {
            chosen_channels[active_channels++] = PM3_DD;
        } else if(0 == strcmp("pm4", argv[i])) {
            chosen_channels[active_channels++] = PM4_DD;
        } else if(0 == strcmp("pm5", argv[i])) {
            chosen_channels[active_channels++] = PM5_CPU;
        } else if(0 == strcmp("pm6", argv[i])) {
            chosen_channels[active_channels++] = PM6_CPU;
        } else if(0 == strcmp("pm7", argv[i])) {
            chosen_channels[active_channels++] = PM7_CPU;
        } else if(0 == strcmp("all", argv[i])) {
            active_channels = max_channels;
            for (unsigned int j=0; j<max_channels; j++) {
                chosen_channels[j] = all_channels[j];
            }
            break;
        } else {
            fprintf(stderr, "Wrong channel '%s', ignored...\n", argv[i]);
        }
    }
    fprintf(stderr, "Setup %u channels.\n", active_channels);

    return active_channels;
}

int main(int argc, char **argv)
{
    uint32_t all_channels[] = {
        PM2_DD,
        PM3_DD,
        PM4_DD,
        PM5_CPU,
        PM6_CPU,
        PM7_CPU,
    };
    /* channels to listen to */
    uint32_t chosen_channels[8] = { 0 };

    assert(sizeof(chosen_channels) >= sizeof(all_channels));
    unsigned int num_channels = sizeof(all_channels)/sizeof(uint32_t);

    /* misc */
    void *pm_handle = NULL;
    unsigned int sample_count;
    uint64_t timestamp;
    uint32_t sampling_rate;
    char *server;
    char *port;

    if (argc <= 3) {
        fprintf(stderr,
                "pmlabclient, Copyright (C)2011-2012, "
                "Jonathan Dimond <jonny@dimond.de>\n");
        fprintf(stderr,
                "                                   & "
                "Johannes Weiß <uni@tux4u.de>\n");
        fprintf(stderr,
                "This program comes with ABSOLUTELY NO WARRANTY; "
                "for details type `show w'.\n"
                "This is free software, and you are welcome to redistribute it"
                "\nunder certain conditions; type `show c' for details.\n\n");
        fprintf(stderr, "Usage: %s SERVER PORT CHANNEL...\n\n", argv[0]);
        fprintf(stderr, "Available CHANNELs:\n");
        fprintf(stderr, "\tpm2\n");
        fprintf(stderr, "\tpm3\n");
        fprintf(stderr, "\tpm4\n");
        fprintf(stderr, "\tpm5\n");
        fprintf(stderr, "\tpm6\n");
        fprintf(stderr, "\tpm7\n");
        exit(EXIT_FAILURE);
    }
    server = argv[1];
    port = argv[2];

    num_channels = parse_channels(argc-3,
                                  argv+3,
                                  num_channels,
                                  all_channels,
                                  chosen_channels);

    signal(SIGINT, (void (*)(int))sig_hnd);

    /* connect to server */
    pm_handle = pm_connect(server, port, chosen_channels, num_channels);
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
/* vim: set fileencoding=utf8 : */

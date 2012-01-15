#ifndef PMLABCLIENT
#define PMLABCLIENT

#include <time.h>

#include "common.h"

/*
 * Connects to the PM Lab server and starts listening for data. The data can be
 * read with pm_read. Once all data has been read, pm_close should be called
 * to ensure a correct cleanup
 *
 * Parameters:
 * server: The PM Lab server to connect to
 * port: The port number to use
 * channels: An array with the channels to listen to
 * num_channels: The size of the channels array
 *
 * Returns:
 * A handle if the connection was successful, NULL else
 */
void *pm_connect(char *server,
                 char *port,
                 unsigned int *channels,
                 unsigned int num_channels);

/*
 * Returns the sampling rate used by the server in Hertz.
 */
uint32_t pm_samplingrate(void *handle);

/*
 * Reads as much data as availabe from the network and writes it to the
 * provided buffers. Crashes if buffers are not large enough
 *
 * Parameters:
 * handle: The server handle
 * buffer_sizes: The sizes of the buffers analog_data and digital_data
 * analog_data: A buffer for the analog data
 * unused: unused (set to NULL)
 * samples_read: A pointer to an integer where the number of samples
 *               read will be written to
 * timestamp_nanons: A pointer to an uint64_t where the timestamp of the first
 *                   sample will be written to
 *
 * Returns:
 * 0 on EOF
 * n | n > 0 when n bytes have successfully been read
 * m | m < 0 on error
 */
int pm_read(void *handle,
            size_t buffer_sizes,
            double *analog_data,
            digival_t *unused,
            unsigned int *samples_read,
            uint64_t *timestamp_nanos);

/*
 * Closes the connection to the server and frees all allocated data
 */
void pm_close(void *handle);

#endif

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
/* vim: set fileencoding=utf8 : */

/*
 *  Records analog data from a NI USB-6218 and send it to connected clients
 *
 *  Copyright (C)2011-2012, Johannes Weiß <weiss@tux4u.de>
 *                        , Jonathan Dimond <jonny@dimond.de>
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

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

#include "utils.h"

ssize_t full_write(int fd, const char *buf, size_t count) {
    ssize_t res;
    size_t size = count;

    while(size > 0 && (res = write(fd, buf, size)) != size) {
        if(res<0 && errno==EINTR) {
           continue;
        }

        if(res < 0) {
            return res;
        }

        assert(res > 0);
        assert(size >= res);

        size-=res;
        buf+=res;
    }

    return count;
}

ssize_t full_read(int fd, char *buf, size_t count) {
    ssize_t res;
    size_t size = count;

    while(size > 0 && (res = read(fd, (void *)buf, size)) != size) {
        if(res<0 && errno==EINTR) {
           continue;
        }

        if(res < 0) {
            return res;
        }

        if(0 == res) {
            /* EOF */
            return 0;
        }

        size-=res;
        buf+=res;
    }

    return count;
}
/* vim: set fileencoding=utf8 : */

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

ssize_t full_read(int fd, const char *buf, size_t count) {
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

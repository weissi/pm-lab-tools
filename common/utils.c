#include <unistd.h>
#include <errno.h>
#include "utils.h"

ssize_t full_write(int fd, const char *buf, size_t count) {
    ssize_t res;
    size_t size;

    while(size > 0 && (res = write(fd, buf, size)) != size) {
        if(res<0 && errno==EINTR) {
           continue;
        }

        if(res < 0) {
            return res;
        }

        size-=res;
        buf+=res;
    }

    return count;
}

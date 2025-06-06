#include "utils.h"
#include <errno.h>

ssize_t read_n_bytes(int fd, void *buffer, size_t n) {
    size_t total = 0;
    char *buf = (char *)buffer;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;
        total += r;
    }
    return total;
}

ssize_t write_n_bytes(int fd, const void *buffer, size_t n) {
    size_t total = 0;
    const char *buf = (const char *)buffer;
    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += w;
    }
    return total;
}

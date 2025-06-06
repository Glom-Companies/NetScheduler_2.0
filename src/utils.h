#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>

ssize_t read_n_bytes(int fd, void *buffer, size_t n);
ssize_t write_n_bytes(int fd, const void *buffer, size_t n);

#endif // UTILS_H

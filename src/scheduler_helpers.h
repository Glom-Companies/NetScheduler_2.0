#ifndef SCHEDULER_HELPERS_H
#define SCHEDULER_HELPERS_H

#include <stdbool.h>
#include "netqueue.h"

void handle_streaming_compress_partial(NetTask *t, size_t quantum_size);
void handle_streaming_convert_partial(NetTask *t, size_t quantum_size);
bool is_media_file(const char *path);

#endif // SCHEDULER_HELPERS_H

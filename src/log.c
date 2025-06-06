#include "log.h"
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "/tmp/scheduler_network.log"

void log_internal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    FILE *f = fopen(LOGFILE, "a");
    if (!f) { va_end(args); return; }
    vfprintf(f, format, args);
    fprintf(f, "\n");
    fclose(f);
    va_end(args);
}

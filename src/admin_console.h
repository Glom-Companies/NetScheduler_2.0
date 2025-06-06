#ifndef ADMIN_CONSOLE_H
#define ADMIN_CONSOLE_H

#include <stdbool.h>
#include "netqueue.h"

void start_admin_console(NetQueue *q, bool *server_running);

#endif // ADMIN_CONSOLE_H

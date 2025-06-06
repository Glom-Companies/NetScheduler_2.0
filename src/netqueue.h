#ifndef NETQUEUE_H
#define NETQUEUE_H

#include <stdbool.h>
#include <pthread.h>

typedef enum {
    TASK_COMPRESS,
    TASK_CONVERT
} TaskType;

typedef struct NetTask {
    int task_id;
    int client_fd;
    int user_priority;
    TaskType type;
    long total_size;
    long processed_bytes;
    char *meta;
    char *output_name;
    struct NetTask *next;
} NetTask;

typedef struct {
    NetTask *head;
    NetTask *tail;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} NetQueue;

void netqueue_init(NetQueue *q);
void netqueue_enqueue(NetQueue *q, NetTask *t);
NetTask *netqueue_dequeue(NetQueue *q);
bool netqueue_is_empty(NetQueue *q);
void nettask_free(NetTask *t);

#endif // NETQUEUE_H

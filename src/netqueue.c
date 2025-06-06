#include "netqueue.h"
#include <stdlib.h>
#include <unistd.h>

void netqueue_init(NetQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

void netqueue_enqueue(NetQueue *q, NetTask *t) {
    t->next = NULL;
    pthread_mutex_lock(&q->mutex);
    if (!q->tail) {
        q->head = q->tail = t;
    } else {
        q->tail->next = t;
        q->tail = t;
    }
    q->size++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

NetTask *netqueue_dequeue(NetQueue *q) {
    pthread_mutex_lock(&q->mutex);
    if (!q->head) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    NetTask *t = q->head;
    q->head = t->next;
    if (!q->head) q->tail = NULL;
    q->size--;
    pthread_mutex_unlock(&q->mutex);
    return t;
}

bool netqueue_is_empty(NetQueue *q) {
    pthread_mutex_lock(&q->mutex);
    bool empty = (q->head == NULL);
    pthread_mutex_unlock(&q->mutex);
    return empty;
}

void nettask_free(NetTask *t) {
    if (!t) return;
    if (t->client_fd >= 0) close(t->client_fd);
    if (t->meta) free(t->meta);
    if (t->output_name) free(t->output_name);
    free(t);
}

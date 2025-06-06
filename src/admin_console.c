#include "admin_console.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    NetQueue *queue;
    bool *running;
} AdminArg;

static void *admin_thread_func(void *arg) {
    AdminArg *a = arg;
    NetQueue *q = a->queue;
    bool *run = a->running;
    char line[128];

    while (*run) {
        printf("\nAdmin> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "list") == 0) {
            pthread_mutex_lock(&q->mutex);
            printf("=== Tâches (%d) ===\n", q->size);
            NetTask *cur = q->head;
            while (cur) {
                printf("ID=%d | Prio=%d | Type=%s | %ld/%ld\n",
                       cur->task_id,
                       cur->user_priority,
                       (cur->type==TASK_COMPRESS?"COMP":"CONV"),
                       cur->processed_bytes,
                       cur->total_size);
                cur = cur->next;
            }
            printf("===============\n");
            pthread_mutex_unlock(&q->mutex);
        }
        else if (strncmp(line, "kick ", 5) == 0) {
            int tid = atoi(line+5);
            NetTask *found = NULL;
            pthread_mutex_lock(&q->mutex);
            NetTask *prev = NULL, *cur = q->head;
            while (cur) {
                if (cur->task_id == tid) {
                    if (!prev) {
                        q->head = cur->next;
                        if (!q->head) q->tail = NULL;
                    } else {
                        prev->next = cur->next;
                        if (!prev->next) q->tail = prev;
                    }
                    q->size--;
                    found = cur;
                    break;
                }
                prev = cur;
                cur = cur->next;
            }
            pthread_mutex_unlock(&q->mutex);
            if (found) {
                log_internal("Admin kick %d", tid);
                nettask_free(found);
                printf("Tâche %d retirée\n", tid);
            } else {
                printf("ID %d introuvable\n", tid);
            }
        }
        else if (strcmp(line, "quit") == 0) {
            *run = false;
            log_internal("Admin quit");
            break;
        }
        else {
            printf("Commandes: list - kick <id> - quit\n");
        }
    }
    free(a);
    return NULL;
}

void start_admin_console(NetQueue *q, bool *server_running) {
    pthread_t tid;
    AdminArg *arg = malloc(sizeof(AdminArg));
    arg->queue = q;
    arg->running = server_running;
    if (pthread_create(&tid, NULL, admin_thread_func, arg) != 0) {
        perror("pthread_create admin_console");
        free(arg);
        return;
    }
    pthread_detach(tid);
}

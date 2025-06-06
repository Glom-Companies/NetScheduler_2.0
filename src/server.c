#include "netqueue.h"
#include "userauth.h"
#include "utils.h"
#include "log.h"
#include "admin_console.h"
#include "scheduler_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define SERVER_PORT 5000
#define BACKLOG 5
#define MAX_CLIENTS 5

static bool server_running = true;
static NetQueue queue;
static int next_task_id = 1;
static pthread_mutex_t taskid_mutex = PTHREAD_MUTEX_INITIALIZER;
static int current_clients = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Gérer la console admin
void *scheduler_thread(void *arg) {
    NetQueue *q = arg;
    while (server_running) {
        // Attendre tâche
        pthread_mutex_lock(&q->mutex);
        while (q->size == 0 && server_running) {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
        if (!server_running) {
            pthread_mutex_unlock(&q->mutex);
            break;
        }
        // Sélection simple : FIFO
        NetTask *t = netqueue_dequeue(q);
        pthread_mutex_unlock(&q->mutex);
        if (!t) continue;

        // Quantum selon priorité
        size_t quantum = (3 - t->user_priority) * 16384; // ex. prio0→49152, prio1→32768, prio2→16384

        if (t->type == TASK_COMPRESS) {
            handle_streaming_compress_partial(t, quantum);
        } else {
            handle_streaming_convert_partial(t, quantum);
        }

        if (t->processed_bytes < t->total_size) {
            netqueue_enqueue(q, t);
        } else {
            close(t->client_fd);
            log_internal("Task %d done", t->task_id);
            nettask_free(t);
        }
    }
    return NULL;
}

static bool pseudo_in_use(const char *pseudo);

void *client_handler(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    // Auth
    char buf[256];
    ssize_t r = read(client_fd, buf, sizeof(buf)-1);
    if (r <= 0) { close(client_fd);   
        pthread_mutex_lock(&clients_mutex);
        current_clients--;
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    buf[r] = '\0';
    // Format : AUTH|pseudo\n
    if (strncmp(buf, "AUTH|", 5) != 0) {
        close(client_fd);
        pthread_mutex_lock(&clients_mutex);
        current_clients--;
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    char *pseudo = strdup(buf+5);
    pseudo[strcspn(pseudo, "\n")] = '\0';

    // Déjà utilisé ?
    pthread_mutex_lock(&clients_mutex);
    if (pseudo_in_use(pseudo)) {
        const char *msg = "AUTH_FAIL|Pseudo déjà connecté\n";
        write(client_fd, msg, strlen(msg));
        close(client_fd);
        current_clients--;
        pthread_mutex_unlock(&clients_mutex);
        free(pseudo);
        return NULL;
    }
    // Marquer comme utilisé
    // (dans production, on stockerait dans une liste ; ici, on simplifie)
    pthread_mutex_unlock(&clients_mutex);

    // Priorité
    int prio;
    if (!find_user_priority(pseudo, &prio)) {
        prio = 2; // invité
    }
    char ok[64];
    snprintf(ok, sizeof(ok), "AUTH_OK|%d\n", prio);
    write(client_fd, ok, strlen(ok));

    // Lire tâche
    r = read(client_fd, buf, sizeof(buf)-1);
    if (r <= 0) {
        close(client_fd);
        pthread_mutex_lock(&clients_mutex);
        current_clients--;
        pthread_mutex_unlock(&clients_mutex);
        free(pseudo);
        return NULL;
    }
    buf[r] = '\0';
    // Format : TASK|TYPE|size|meta|out\n
    char *parts[6] = {0};
    int idx = 0;
    char *tok = strtok(buf, "|");
    while (tok && idx < 5) {
        parts[idx++] = tok;
        tok = strtok(NULL, "|");
    }
    if (idx < 4) {
        close(client_fd);
        pthread_mutex_lock(&clients_mutex);
        current_clients--;
        pthread_mutex_unlock(&clients_mutex);
        free(pseudo);
        return NULL;
    }
    TaskType type = (strcmp(parts[1], "COMPRESS")==0 ? TASK_COMPRESS : TASK_CONVERT);
    long total = atol(parts[2]);
    char *meta = strdup(parts[3]);
    char *out  = (type==TASK_CONVERT? strdup(parts[4]) : NULL);

    // ID
    pthread_mutex_lock(&taskid_mutex);
    int tid = next_task_id++;
    pthread_mutex_unlock(&taskid_mutex);

    NetTask *t = malloc(sizeof(NetTask));
    t->task_id = tid;
    t->client_fd = client_fd;
    t->user_priority = prio;
    t->type = type;
    t->total_size = total;
    t->processed_bytes = 0;
    t->meta = meta;
    t->output_name = out;
    t->next = NULL;

    netqueue_enqueue(&queue, t);

    // Attendre fin (socket close)
    char dummy;
    while (read(client_fd, &dummy, 1) > 0) { /* rien */ }

    close(client_fd);
    pthread_mutex_lock(&clients_mutex);
    current_clients--;
    pthread_mutex_unlock(&clients_mutex);
    free(pseudo);
    return NULL;
}

// Simplification : 
static bool pseudo_in_use(const char *pseudo) {
    // À implémenter avec une liste réelle...
    // Pour l’instant on retourne false pour accepter tout pseudo unique.
    return false;
}

int main(void) {
    if (load_users("users.txt") < 0) {
        fprintf(stderr, "Erreur users.txt\n");
        return 1;
    }
    netqueue_init(&queue);

    pthread_t sched;
    pthread_create(&sched, NULL, scheduler_thread, &queue);
    pthread_detach(sched);

    start_admin_console(&queue, &server_running);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt=1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, BACKLOG);

    while (server_running) {
        pthread_mutex_lock(&clients_mutex);
        if (current_clients >= MAX_CLIENTS) {
            pthread_mutex_unlock(&clients_mutex);
            usleep(100000);
            continue;
        }
        pthread_mutex_unlock(&clients_mutex);

        int cli = accept(listen_fd, NULL, NULL);
        pthread_mutex_lock(&clients_mutex);
        if (current_clients >= MAX_CLIENTS) {
            const char *msg = "ERROR|Serveur plein\n";
            write(cli, msg, strlen(msg));
            close(cli);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        current_clients++;
        pthread_mutex_unlock(&clients_mutex);

        int *pcli = malloc(sizeof(int));
        *pcli = cli;
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, pcli);
        pthread_detach(tid);
    }

    close(listen_fd);
    return 0;
}

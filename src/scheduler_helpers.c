#include "scheduler_helpers.h"
#include "utils.h"
#include "log.h"
#include <zstd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define LOGFILE "/tmp/scheduler_network.log"

bool is_media_file(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    char ext[16];
    strncpy(ext, dot+1, 15);
    ext[15] = '\0';
    for (int i = 0; ext[i]; i++) ext[i] = tolower(ext[i]);
    const char *media[] = {
        "mp4","mkv","mov","avi","mp3","wav","aac","flac","ogg","m4a"
    };
    for (size_t i = 0; i < sizeof(media)/sizeof(media[0]); i++) {
        if (strcmp(ext, media[i]) == 0) return true;
    }
    return false;
}

void handle_streaming_compress_partial(NetTask *t, size_t quantum_size) {
    int fd = t->client_fd;
    bool media = is_media_file(t->meta);

    void *inbuf = malloc(quantum_size);
    if (!inbuf) return;
    ssize_t r = read_n_bytes(fd, inbuf, quantum_size);
    if (r <= 0) {
        free(inbuf);
        t->processed_bytes = t->total_size;
        return;
    }

    if (media) {
        // Processus ffmpeg simple par quantum
        int pin[2], pout[2];
        if (pipe(pin)<0||pipe(pout)<0) { free(inbuf); return; }
        pid_t pid = fork();
        if (pid < 0) { free(inbuf); close(pin[0]);close(pin[1]);close(pout[0]);close(pout[1]); return; }
        if (pid == 0) {
            dup2(pin[0], 0); dup2(pout[1], 1);
            close(pin[1]); close(pout[0]);
            execlp("ffmpeg","ffmpeg","-hide_banner","-loglevel","error",
                   "-i","pipe:0","-f","mp3","-b:a","192k","pipe:1",(char*)NULL);
            _exit(1);
        }
        close(pin[0]); close(pout[1]);
        write_n_bytes(pin[1], inbuf, r);
        close(pin[1]);
        char buf[32768];
        ssize_t w;
        while ((w=read(pout[0], buf, sizeof(buf)))>0) {
            write_n_bytes(fd, buf, w);
        }
        close(pout[0]);
        waitpid(pid, NULL, 0);
    }
    else {
        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 9);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 1);
        size_t bound = ZSTD_compressBound(r);
        void *out = malloc(bound);
        size_t csize = ZSTD_compressCCtx(cctx, out, bound, inbuf, r, 9);
        if (!ZSTD_isError(csize)) {
            write_n_bytes(fd, out, csize);
        }
        ZSTD_freeCCtx(cctx);
        free(out);
    }

    free(inbuf);
    t->processed_bytes += r;
    log_internal("Task %d: processed %zd/%ld", t->task_id, r, t->total_size);
}

void handle_streaming_convert_partial(NetTask *t, size_t quantum_size) {
    static pid_t pid = -1;
    static int wfd = -1, rfd = -1;
    static bool init = false;
    int fd = t->client_fd;

    if (!init) {
        int pin[2], pout[2];
        if (pipe(pin)<0||pipe(pout)<0) return;
        pid = fork();
        if (pid<0) return;
        if (pid==0) {
            dup2(pin[0], 0); dup2(pout[1], 1);
            close(pin[1]); close(pout[0]);
            execlp("ffmpeg","ffmpeg","-hide_banner","-loglevel","error",
                   "-i","pipe:0","-f","mp3","-b:a","192k","pipe:1",(char*)NULL);
            _exit(1);
        }
        close(pin[0]); close(pout[1]);
        wfd=pin[1]; rfd=pout[0];
        init=true;
        fcntl(rfd, F_SETFL, fcntl(rfd,F_GETFL,0)|O_NONBLOCK);
    }

    void *inbuf = malloc(quantum_size);
    if (!inbuf) return;
    ssize_t r = read_n_bytes(fd, inbuf, quantum_size);
    if (r <= 0) {
        free(inbuf);
        t->processed_bytes = t->total_size;
        close(wfd);
        return;
    }
    write_n_bytes(wfd, inbuf, r);
    free(inbuf);

    char buf[16384];
    ssize_t w;
    while ((w = read(rfd, buf, sizeof(buf))) > 0) {
        write_n_bytes(fd, buf, w);
    }

    t->processed_bytes += r;
    log_internal("Task %d: converted %zd/%ld", t->task_id, r, t->total_size);

    if (t->processed_bytes >= t->total_size) {
        close(wfd);
        while ((w = read(rfd, buf, sizeof(buf))) > 0) {
            write_n_bytes(fd, buf, w);
        }
        waitpid(pid, NULL, 0);
        log_internal("Task %d: conversion done", t->task_id);
    }
}

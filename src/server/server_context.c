//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_context.h"

#include "../common/util.h"

#include <string.h>
#include <unistd.h>

void server_context_init(server_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    //defaults
    ctx->world_kind = WORLD_WRAP;
    ctx->world_size.width = 10;
    ctx->world_size.height = 10;

    ctx->probs.p_up = 0.25;
    ctx->probs.p_down = 0.25;
    ctx->probs.p_left = 0.25;
    ctx->probs.p_right = 0.25;

    ctx->k_max_steps = 100;
    ctx->total_reps = 1;
    ctx->current_rep = 0;
    ctx->global_mode = MODE_SUMMARY;

    ctx->clients.count = 0;
    for (int i = 0; i < SERVER_MAX_CLIENTS; i++) {
        ctx->clients.fds[i] = -1;
    }

    if (pthread_mutex_init(&ctx->clients_mtx, NULL) != 0) {
        die("pthread_mutex_init(client_mtx) failed");
    }
    if (pthread_mutex_init(&ctx->state_mtx, NULL) != 0) {
        die("pthread_mutex_init(state_mtx) failed");
    }
}

void server_context_destroy(server_context_t *ctx) {
    pthread_mutex_destroy(&ctx->clients_mtx);
    pthread_mutex_destroy(&ctx->state_mtx);
}

int server_context_add_client(server_context_t *ctx, int client_fd) {
    pthread_mutex_lock(&ctx->clients_mtx);

    if (ctx->clients.count >= SERVER_MAX_CLIENTS) {
        pthread_mutex_unlock(&ctx->clients_mtx);
        return -1;
    }

    //find empty slot*/
    for (int i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (ctx->clients.fds[i] == -1) {
            ctx->clients.fds[i] = client_fd;
            ctx->clients.count++;
            pthread_mutex_unlock(&ctx->clients_mtx);
            return 0;
        }
    }
    pthread_mutex_unlock(&ctx->clients_mtx);
    return -1;
}

void server_context_remove_client(server_context_t *ctx, int client_fd) {
    pthread_mutex_lock(&ctx->clients_mtx);

    for (int i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (ctx->clients.fds[i] == client_fd) {
            ctx->clients.fds[i] = -1;
            if (ctx->clients.count > 0) {
                ctx->clients.count--;
            }
            break;
        }
    }
    pthread_mutex_unlock(&ctx->clients_mtx);
}

void server_context_for_each_client(server_context_t *ctx, client_fd_fn fn, void *user) {
    pthread_mutex_lock(&ctx->clients_mtx);

    for (int i = 0; i < SERVER_MAX_CLIENTS; i++) {
        int fd = ctx->clients.fds[i];
        if (fd != -1) {
            fn(fd, user);
        }
    }
    pthread_mutex_unlock(&ctx->clients_mtx);
}

void server_context_set_mode(server_context_t *ctx, global_mode_t mode) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->global_mode = mode;
    pthread_mutex_unlock(&ctx->state_mtx);
}

global_mode_t server_context_get_mode(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    global_mode_t mode = ctx->global_mode;
    pthread_mutex_unlock(&ctx->state_mtx);
    return mode;
}

void server_context_set_progress(struct server_context *ctx, uint32_t current_rep) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->current_rep = current_rep;
    pthread_mutex_unlock(&ctx->state_mtx);
}

uint32_t server_context_get_progress(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    uint32_t rep = ctx->current_rep;
    pthread_mutex_unlock(&ctx->state_mtx);
    return rep;
}
//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_context.h"

#include "../common/util.h"

#include <string.h>
#include <unistd.h>

/**
 * @file server_context.c
 * @brief Implementation of server context initialization and synchronized accessors.
 */

/**
 * @brief Initialize a server context with defaults.
 *
 * Initializes mutexes and sets reasonable default simulation parameters.
 *
 * @param ctx Context to initialize.
 */
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

    ctx->sim_state = RW_WIRE_SIM_LOBBY;
    ctx->multi_user = 0;
    ctx->owner_fd = -1;

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

/**
 * @brief Destroy mutexes held by the server context.
 *
 * @param ctx Context to destroy.
 */
void server_context_destroy(server_context_t *ctx) {
    pthread_mutex_destroy(&ctx->clients_mtx);
    pthread_mutex_destroy(&ctx->state_mtx);
}

/**
 * @brief Add a client socket FD to the context.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @param client_fd Client socket.
 * @return 0 on success, -1 if the client list is full or no free slot exists.
 */
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

/**
 * @brief Remove a client socket FD from the context.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @param client_fd Client socket to remove.
 */
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

/**
 * @brief Invoke a callback for each connected client.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @param fn Callback invoked for each client.
 * @param user User pointer passed through to the callback.
 */
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

/**
 * @brief Set the global mode.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @param mode New global mode.
 */
void server_context_set_mode(server_context_t *ctx, global_mode_t mode) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->global_mode = mode;
    pthread_mutex_unlock(&ctx->state_mtx);
}

/**
 * @brief Get the global mode.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @return Current global mode.
 */

global_mode_t server_context_get_mode(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    global_mode_t mode = ctx->global_mode;
    pthread_mutex_unlock(&ctx->state_mtx);
    return mode;
}

/**
 * @brief Update current repetition progress.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @param current_rep Current repetition index.
 */
void server_context_set_progress(struct server_context *ctx, uint32_t current_rep) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->current_rep = current_rep;
    pthread_mutex_unlock(&ctx->state_mtx);
}

/**
 * @brief Read current repetition progress.
 *
 * Thread-safe.
 *
 * @param ctx Server context.
 * @return Current repetition index.
 */
uint32_t server_context_get_progress(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    uint32_t rep = ctx->current_rep;
    pthread_mutex_unlock(&ctx->state_mtx);
    return rep;
}

rw_wire_sim_state_t server_context_get_sim_state(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    rw_wire_sim_state_t s = ctx->sim_state;
    pthread_mutex_unlock(&ctx->state_mtx);
    return s;
}

void server_context_set_sim_state(server_context_t *ctx, rw_wire_sim_state_t state) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->sim_state = state;
    pthread_mutex_unlock(&ctx->state_mtx);
}

void server_context_set_multi_user(server_context_t *ctx, uint8_t multi_user) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->multi_user = multi_user ? 1 : 0;
    pthread_mutex_unlock(&ctx->state_mtx);
}

uint8_t server_context_get_multi_user(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    uint8_t v = ctx->multi_user;
    pthread_mutex_unlock(&ctx->state_mtx);
    return v;
}

void server_context_set_owner_fd(server_context_t *ctx, int owner_fd) {
    pthread_mutex_lock(&ctx->state_mtx);
    ctx->owner_fd = owner_fd;
    pthread_mutex_unlock(&ctx->state_mtx);
}

int server_context_get_owner_fd(server_context_t *ctx) {
    pthread_mutex_lock(&ctx->state_mtx);
    int v = ctx->owner_fd;
    pthread_mutex_unlock(&ctx->state_mtx);
    return v;
}

int server_context_client_can_control(server_context_t *ctx, int client_fd) {
    pthread_mutex_lock(&ctx->state_mtx);
    int owner = ctx->owner_fd;
    uint8_t mu = ctx->multi_user;
    pthread_mutex_unlock(&ctx->state_mtx);

    if (owner < 0) {
        /* If not set yet, allow first client to become owner logically. */
        return 1;
    }

    if (!mu) {
        return client_fd == owner;
    }

    /* In multi-user, still limit control to owner for simplicity/determinism. */
    return client_fd == owner;
}

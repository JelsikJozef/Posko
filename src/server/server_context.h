//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_CONTEXT_H
#define SEMPRACA_SERVER_CONTEXT_H

#include <pthread.h>
#include <stdint.h>
#include "../common/types.h"

/**
 * @file server_context.h
 * @brief Shared server state and synchronization primitives.
 *
 * `server_context_t` holds runtime configuration (world parameters, probabilities,
 * number of repetitions, global mode) and runtime state (connected clients,
 * current progress).
 *
 * Thread safety:
 * - Client list operations are protected by `clients_mtx`.
 * - Mode/progress are protected by `state_mtx`.
 * - Public APIs in this file acquire the necessary mutex internally.
 */

/**
 * @brief Maximum number of client file descriptors tracked by the server context.
 */
#define SERVER_MAX_CLIENTS 32

/**
 * @brief Set of connected client file descriptors.
 */
typedef struct {
    int fds[SERVER_MAX_CLIENTS]; /**< Client socket file descriptors. */
    int count;                   /**< Number of active clients. */
} client_list_t;

/**
 * @brief Server runtime context.
 *
 * Contains both configuration (world/probabilities/limits) and runtime state
 * (clients, progress, mode). Use the helper functions below for synchronized access.
 */
typedef struct server_context{
    /* Simulation parameters (configured at startup or via IPC). */
    world_kind_t world_kind;   /**< World kind/topology. */
    world_size_t world_size;   /**< World dimensions. */
    move_probs_t probs;        /**< Movement probabilities for each step direction. */
    uint32_t k_max_steps;      /**< K threshold used for success-within-K metrics. */

    uint32_t total_reps;       /**< Planned number of repetitions. */
    uint32_t current_rep;      /**< Current repetition index (0-based). */

    global_mode_t global_mode; /**< Current server mode (interactive/summary). */

    /* Clients */
    client_list_t clients;     /**< Connected client sockets. */

    /* Synchronization */
    pthread_mutex_t clients_mtx; /**< Protects @ref server_context_t::clients. */
    pthread_mutex_t state_mtx;   /**< Protects @ref server_context_t::current_rep and @ref server_context_t::global_mode. */

} server_context_t;

/**
 * @brief Initialize a server context with default values and mutexes.
 *
 * @param ctx Context to initialize.
 */
void server_context_init(server_context_t *ctx);

/**
 * @brief Destroy a server context (releases mutexes).
 *
 * Does not close any client sockets.
 *
 * @param ctx Context to destroy.
 */
void server_context_destroy(server_context_t *ctx);

/**
 * @brief Register a newly connected client file descriptor.
 *
 * @param ctx Server context.
 * @param client_fd Connected client socket.
 * @return 0 on success, -1 if no slot is available.
 */
int server_context_add_client(server_context_t *ctx, int client_fd);

/**
 * @brief Remove a client file descriptor from the context.
 *
 * @param ctx Server context.
 * @param client_fd Client socket to remove.
 */
void server_context_remove_client(server_context_t *ctx, int client_fd);

/**
 * @brief Callback type used for iterating over connected client sockets.
 *
 * @param fd A client socket FD.
 * @param user User-provided pointer passed through by the iterator.
 */
typedef void(*client_fd_fn)(int fd, void *user);

/**
 * @brief Invoke @p fn for each currently connected client.
 *
 * Note: this holds `clients_mtx` while iterating and calling @p fn.
 * Keep callbacks short and avoid calling back into functions that might
 * try to acquire `clients_mtx` again.
 *
 * @param ctx Server context.
 * @param fn Callback invoked for each client.
 * @param user Opaque pointer passed to each callback invocation.
 */
void server_context_for_each_client(server_context_t *ctx, client_fd_fn fn, void *user);

/**
 * @brief Set the global simulation mode.
 *
 * @param ctx Server context.
 * @param new_mode New mode.
 */
void server_context_set_mode(server_context_t *ctx, global_mode_t new_mode);

/**
 * @brief Read the current global simulation mode.
 *
 * @param ctx Server context.
 * @return Current mode.
 */
global_mode_t server_context_get_mode(server_context_t *ctx);

/**
 * @brief Update the current progress (repetition index).
 *
 * @param ctx Server context.
 * @param current_rep Current repetition index.
 */
void server_context_set_progress(server_context_t *ctx, uint32_t current_rep);

/**
 * @brief Read the current progress (repetition index).
 *
 * @param ctx Server context.
 * @return Current repetition index.
 */
uint32_t server_context_get_progress(server_context_t *ctx);

#endif //SEMPRACA_SERVER_CONTEXT_H


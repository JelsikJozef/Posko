//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_IPC_H
#define SEMPRACA_SERVER_IPC_H

#include <pthread.h>

/**
 * @file server_ipc.h
 * @brief Server-side IPC layer (Unix domain socket accept loop + basic request handling).
 *
 * Responsibilities:
 * - Create a listening AF_UNIX socket at a filesystem path
 * - Accept client connections
 * - Perform initial JOIN/WELCOME handshake
 * - Receive simple requests from clients (e.g., SET_GLOBAL_MODE)
 * - Broadcast simple notifications to all clients
 *
 * This module does not implement the simulation itself; it only coordinates IO and
 * updates of shared state via `server_context_t`.
 */

/** Forward declaration of the server context type. */
struct server_context;

/**
 * @brief Start the server IPC subsystem.
 *
 * Creates a listening socket at @p socket_path, removes any previous stale file,
 * and starts an accept loop thread. Each accepted client is handled in its own
 * detached thread.
 *
 * @param socket_path Filesystem socket path (e.g. "/tmp/rw.sock").
 * @param ctx Shared server context used for handshake parameters and state updates.
 * @return 0 on success, -1 on invalid arguments, program terminates on fatal socket errors.
 */
int server_ipc_start(const char *socket_path, struct server_context *ctx);

/**
 * @brief Stop the server IPC subsystem.
 *
 * Closes the listening socket (if open) and unlinks the socket path.
 *
 * @return Nothing.
 */
void server_ipc_stop(void);


#endif //SEMPRACA_SERVER_IPC_H


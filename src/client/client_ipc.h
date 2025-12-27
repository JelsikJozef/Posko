//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CLIENT_IPC_H
#define SEMPRACA_CLIENT_IPC_H

/**
 * @file client_ipc.h
 * @brief Client-side IPC helpers for communicating with the server via Unix domain sockets.
 *
 * This module contains only the transport/protocol glue:
 * - connect to the server AF_UNIX socket
 * - send simple requests (JOIN, SET_GLOBAL_MODE)
 * - receive simple responses (WELCOME)
 *
 * It intentionally does not implement higher-level client logic or UI.
 */

#include "../common/protocol.h"

/**
 * @brief Connect to a server Unix domain socket.
 *
 * Creates an `AF_UNIX`/`SOCK_STREAM` socket and connects it to @p socket_path.
 *
 * @param socket_path Filesystem path of the server socket (e.g. "/tmp/rw.sock").
 * @return On success, returns a connected file descriptor (>= 0). On failure, returns -1.
 */
int client_ipc_connect(const char *socket_path);

/**
 * @brief Send a JOIN request to the server.
 *
 * The JOIN message contains the current process ID (`getpid()`), allowing the server to
 * identify the client.
 *
 * @param fd Connected client socket.
 * @return 0 on success, -1 on failure.
 */
int client_ipc_send_join(int fd);

/**
 * @brief Receive a WELCOME message from the server.
 *
 * This function blocks until the next message header arrives and expects it to be
 * `RW_MSG_WELCOME` with a payload of size `sizeof(rw_welcome_t)`.
 *
 * @param fd Connected client socket.
 * @param out_welcome Output buffer that receives the decoded welcome payload.
 * @return 0 on success, -1 on failure (wrong type/length, read error, etc.).
 */
int client_ipc_recv_welcome(int fd, rw_welcome_t *out_welcome);

/**
 * @brief Request a global simulation mode change.
 *
 * Sends a `RW_MSG_SET_GLOBAL_MODE` message. The server may broadcast the resulting change
 * via `RW_MSG_GLOBAL_MODE_CHANGED` to all clients.
 *
 * @param fd Connected client socket.
 * @param mode Requested new mode in wire format.
 * @return 0 on success, -1 on failure.
 */
int client_ipc_set_global_mode(int fd, rw_wire_global_mode_t mode);

/** Control-plane helpers (menu). */
int client_ipc_query_status(int fd, rw_status_t *out_status);
int client_ipc_create_sim(int fd, const rw_create_sim_t *req);
int client_ipc_load_world(int fd, const rw_load_world_t *req);
int client_ipc_start_sim(int fd);
int client_ipc_restart_sim(int fd, uint32_t total_reps);
int client_ipc_request_snapshot(int fd);
int client_ipc_save_results(int fd, const char *path);
int client_ipc_load_results(int fd, const char *path);
int client_ipc_quit(int fd, int stop_if_owner);
int client_ipc_stop_sim(int fd);

#endif //SEMPRACA_CLIENT_IPC_H


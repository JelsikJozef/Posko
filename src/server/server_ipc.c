//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_ipc.h"

#include "../common/protocol.h"
#include "../common/util.h"
#include "server_context.h"
#include "sim_manager.h"
#include "snapshot_sender.h"
#include "results.h"
#include "world.h"
#include "persist.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

/**
 * @file server_ipc.c
 * @brief Implementation of the server IPC layer.
 */

/**
 * @brief Listening socket file descriptor.
 */
static int listen_fd = -1;

/**
 * @brief Buffer holding the active socket path.
 */
static char socket_path_buf[108];

/**
 * @brief Thread running the accept loop.
 */
static pthread_t accept_thread;

/**
 * @brief Global pointer to the shared server context.
 */
static struct server_context *g_ctx = NULL;

static world_t *g_world = NULL;
static results_t *g_results = NULL;
static sim_manager_t *g_sm = NULL;

/*forward declarations*/

/**
 * @brief Accept-loop thread function.
 *
 * Repeatedly accepts clients and spawns a detached thread for each.
 *
 * @param arg Unused.
 * @return Always NULL.
 */
static void *accept_loop(void *arg);

/**
 * @brief Per-client worker thread function.
 *
 * Performs the JOIN/WELCOME handshake, registers the client in the server context,
 * and then processes incoming requests until disconnect.
 *
 * @param arg Pointer to dynamically allocated `int` containing client fd.
 * @return Always NULL.
 */
static void *client_thread(void *arg);

/**
 * @brief Handle initial JOIN request and send WELCOME.
 *
 * @param client_fd Client socket fd.
 * @return 0 on success, -1 on protocol/IO error.
 */
static int handle_join(int client_fd);

// Control-plane helpers used in client_thread (defined at end of file)
static void send_error(int fd, uint32_t code, const char *msg);
static void send_ack(int fd, uint16_t req_type, uint16_t status);
static void on_sim_end_cb(void *user, int stopped);

/**
 * @brief Broadcast a global-mode-changed notification to all clients.
 *
 * @param ctx Server context.
 * @param new_mode New mode (wire format).
 * @param changed_by_pid Process id of the client who triggered the change (0 if unknown).
 */
static void broadcast_global_mode_changed(struct server_context *ctx, rw_wire_global_mode_t new_mode, uint32_t changed_by_pid);

typedef struct {
    rw_global_mode_changed_t msg;
}broadcast_mode_ctx_t;

/*======================================================================
*public API
*======================================================================*/

/**
 * @brief Start listening on a Unix domain socket and spawn accept thread.
 *
 * @param socket_path Filesystem socket path.
 * @param ctx Shared server context.
 * @return 0 on success, -1 on invalid args; terminates the process on fatal socket errors.
 */
int server_ipc_start(const char *socket_path, struct server_context *ctx) {
    struct sockaddr_un addr;

    if (!socket_path || !ctx) {
        return -1;
    }

    g_ctx = ctx;

    if (rw_copy_socket_path(socket_path_buf, sizeof(socket_path_buf), socket_path) != 0) {
        log_error("Socket path too long");
        return -1;
    }

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        die("socket() failed");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (rw_copy_socket_path(addr.sun_path, sizeof(addr.sun_path), socket_path_buf) != 0) {
        log_error("Socket path too long for sockaddr_un.sun_path");
        return -1;
    }

    /*if socket exists from previous run */
    unlink(socket_path_buf);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("bind() failed");
    }
    if (listen(listen_fd, 16) < 0) {
        die("listen() failed");
    }

    log_info("Server listening on socket: %s", socket_path_buf);

    if (pthread_create(&accept_thread, NULL, accept_loop, NULL) != 0) {
        die("pthread_create(accept_thread) failed");
    }
    return 0;
}

/**
 * @brief Stop IPC: close listening socket and unlink the socket path.
 */
void server_ipc_stop(void) {
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
    unlink(socket_path_buf);
}

/*======================================================================
 *Accept loop
 *======================================================================*/

/**
 * @brief Accept loop thread body.
 *
 * @param arg Unused.
 * @return NULL.
 */
static void *accept_loop(void *arg) {
    (void)arg;

    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("accept() failed: %s", strerror(errno));
            break;
        }
        log_info("Client connected (fd=%d)", client_fd);

        pthread_t tid;
        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) {
            log_error("malloc() failed for client thread");
            close(client_fd);
            continue;
        }
        *fd_ptr = client_fd;

        if (pthread_create(&tid, NULL, client_thread, fd_ptr) != 0) {
            log_error("pthread_create() failed for client thread");
            close(client_fd);
            free(fd_ptr);
            continue;
        }
        pthread_detach(tid);

    }
    return NULL;
}
/*======================================================================
 *Client thread
 *======================================================================*/
/**
 * @brief Per-client thread body.
 *
 * @param arg Heap-allocated pointer to client fd.
 * @return NULL.
 */
static void *client_thread(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    //join + WELCOME
    if (handle_join(client_fd) != 0) {
        close(client_fd);
        log_info("Client rejected (fd=%d)", client_fd);
        return NULL;
    }
    //Active client
    if (server_context_add_client(g_ctx,client_fd) != 0) {
        log_error("Cannot register client (fd=%d)", client_fd);
        close(client_fd);
        return NULL;
    }

    /* First client becomes owner (if not set). */
    if (server_context_get_owner_fd(g_ctx) < 0) {
        server_context_set_owner_fd(g_ctx, client_fd);
    }

    log_info("Client connected (fd=%d)", client_fd);


    while (1) {
        rw_msg_hdr_t hdr;
        if (rw_recv_hdr(client_fd, &hdr) != 0) {
            break;
        }

        if (hdr.type == RW_MSG_SET_GLOBAL_MODE &&
            hdr.payload_len == sizeof(rw_set_global_mode_t)) {

            rw_set_global_mode_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }

            //change global mode
            server_context_set_mode(g_ctx,
                (global_mode_t)req.new_mode);

            log_info("GLOBAL_MODE changed to %u by (fd=%d)", req.new_mode,client_fd);

            broadcast_global_mode_changed(
                g_ctx,
                req.new_mode,
                0 /*unknown pid*/);

            continue;
            }

        if (hdr.type == RW_MSG_QUERY_STATUS && hdr.payload_len == sizeof(rw_query_status_t)) {
            rw_query_status_t q;
            if (rw_recv_payload(client_fd, &q, sizeof(q)) != 0) {
                break;
            }
            rw_status_t st;
            memset(&st, 0, sizeof(st));
            st.state = server_context_get_sim_state(g_ctx);
            st.multi_user = server_context_get_multi_user(g_ctx);
            st.can_control = server_context_client_can_control(g_ctx, client_fd) ? 1 : 0;
            st.world_kind = (g_ctx->world_kind == WORLD_OBSTACLES) ? RW_WIRE_WORLD_OBSTACLES : RW_WIRE_WORLD_WRAP;
            st.size.width = (uint32_t)g_ctx->world_size.width;
            st.size.height = (uint32_t)g_ctx->world_size.height;
            st.probs.p_up = g_ctx->probs.p_up;
            st.probs.p_down = g_ctx->probs.p_down;
            st.probs.p_left = g_ctx->probs.p_left;
            st.probs.p_right = g_ctx->probs.p_right;
            st.k_max_steps = g_ctx->k_max_steps;
            st.total_reps = g_ctx->total_reps;
            st.current_rep = server_context_get_progress(g_ctx);
            st.global_mode = (rw_wire_global_mode_t)server_context_get_mode(g_ctx);
            rw_send_msg(client_fd, RW_MSG_STATUS, &st, sizeof(st));
            continue;
        }

        if (hdr.type == RW_MSG_CREATE_SIM && hdr.payload_len == sizeof(rw_create_sim_t)) {
            rw_create_sim_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            if (server_context_get_sim_state(g_ctx) == RW_WIRE_SIM_RUNNING) {
                send_error(client_fd, 2, "Simulation already running");
                continue;
            }
            if (req.size.width == 0 || req.size.height == 0 || req.total_reps == 0 || req.k_max_steps == 0) {
                send_error(client_fd, 3, "Invalid parameters");
                continue;
            }
            double sum = req.probs.p_up + req.probs.p_down + req.probs.p_left + req.probs.p_right;
            if (sum < 0.999 || sum > 1.001) {
                send_error(client_fd, 4, "Probabilities must sum to 1");
                continue;
            }

            server_context_set_multi_user(g_ctx, req.multi_user);

            g_ctx->world_kind = (req.world_kind == RW_WIRE_WORLD_OBSTACLES) ? WORLD_OBSTACLES : WORLD_WRAP;
            g_ctx->world_size.width = (int32_t)req.size.width;
            g_ctx->world_size.height = (int32_t)req.size.height;
            g_ctx->probs.p_up = req.probs.p_up;
            g_ctx->probs.p_down = req.probs.p_down;
            g_ctx->probs.p_left = req.probs.p_left;
            g_ctx->probs.p_right = req.probs.p_right;
            g_ctx->k_max_steps = req.k_max_steps;
            g_ctx->total_reps = req.total_reps;
            server_context_set_progress(g_ctx, 0);

            if (g_world) {
                world_destroy(g_world);
                if (world_init(g_world, g_ctx->world_kind, g_ctx->world_size) != 0) {
                    send_error(client_fd, 5, "world_init failed");
                    continue;
                }
                if (g_ctx->world_kind == WORLD_OBSTACLES) {
                    world_generate_obstacles(g_world, 10, 12345);
                }
            }
            if (g_results) {
                results_destroy(g_results);
                if (results_init(g_results, g_ctx->world_size) != 0) {
                    send_error(client_fd, 6, "results_init failed");
                    continue;
                }
            }

            server_context_set_sim_state(g_ctx, RW_WIRE_SIM_LOBBY);
            send_ack(client_fd, RW_MSG_CREATE_SIM, 0);
            continue;
        }

        if (hdr.type == RW_MSG_LOAD_WORLD && hdr.payload_len == sizeof(rw_load_world_t)) {
            rw_load_world_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            if (server_context_get_sim_state(g_ctx) == RW_WIRE_SIM_RUNNING) {
                send_error(client_fd, 2, "Simulation already running");
                continue;
            }
            req.path[RW_PATH_MAX - 1] = '\0';
            server_context_set_multi_user(g_ctx, req.multi_user);

            if (!g_world) {
                send_error(client_fd, 7, "Server world handle not set");
                continue;
            }
            if (persist_load_world(req.path, g_world, g_ctx) != 0) {
                send_error(client_fd, 8, "Failed to load world file");
                continue;
            }
            if (g_results) {
                results_destroy(g_results);
                if (results_init(g_results, g_ctx->world_size) != 0) {
                    send_error(client_fd, 6, "results_init failed");
                    continue;
                }
            }

            server_context_set_sim_state(g_ctx, RW_WIRE_SIM_LOBBY);
            send_ack(client_fd, RW_MSG_LOAD_WORLD, 0);
            continue;
        }

        if (hdr.type == RW_MSG_START_SIM && hdr.payload_len == 0) {
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            if (!g_sm) {
                send_error(client_fd, 9, "Server sim_manager not set");
                continue;
            }
            if (server_context_get_sim_state(g_ctx) == RW_WIRE_SIM_RUNNING) {
                send_error(client_fd, 2, "Simulation already running");
                continue;
            }

            sim_manager_set_on_end(g_sm, on_sim_end_cb, g_ctx);

            if (sim_manager_start(g_sm) != 0) {
                send_error(client_fd, 10, "Failed to start simulation");
                continue;
            }
            send_ack(client_fd, RW_MSG_START_SIM, 0);
            continue;
        }

        if (hdr.type == RW_MSG_RESTART_SIM && hdr.payload_len == sizeof(rw_restart_sim_t)) {
            rw_restart_sim_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            if (!g_sm) {
                send_error(client_fd, 9, "Server sim_manager not set");
                continue;
            }
            if (server_context_get_sim_state(g_ctx) == RW_WIRE_SIM_RUNNING) {
                send_error(client_fd, 2, "Simulation running; stop first");
                continue;
            }
            if (req.total_reps == 0) {
                send_error(client_fd, 3, "Invalid repetitions");
                continue;
            }

            sim_manager_set_on_end(g_sm, on_sim_end_cb, g_ctx);
            if (sim_manager_restart(g_sm, req.total_reps) != 0) {
                send_error(client_fd, 10, "Failed to restart simulation");
                continue;
            }
            send_ack(client_fd, RW_MSG_RESTART_SIM, 0);
            continue;
        }

        if (hdr.type == RW_MSG_STOP_SIM && hdr.payload_len == sizeof(rw_stop_sim_t)) {
            rw_stop_sim_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            if (g_sm) {
                sim_manager_request_stop(g_sm);
            }
            send_ack(client_fd, RW_MSG_STOP_SIM, 0);
            continue;
        }

        if (hdr.type == RW_MSG_REQUEST_SNAPSHOT && hdr.payload_len == sizeof(rw_request_snapshot_t)) {
            rw_request_snapshot_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!g_world || !g_results) {
                send_error(client_fd, 11, "Snapshot unavailable");
                continue;
            }
            if (snapshot_broadcast(g_ctx, g_world, g_results) != 0) {
                send_error(client_fd, 12, "Snapshot send failed");
                continue;
            }
            send_ack(client_fd, RW_MSG_REQUEST_SNAPSHOT, 0);
            continue;
        }

        if (hdr.type == RW_MSG_SAVE_RESULTS && hdr.payload_len == sizeof(rw_save_results_t)) {
            rw_save_results_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            req.path[RW_PATH_MAX - 1] = '\0';
            if (!g_world || !g_results) {
                send_error(client_fd, 13, "Nothing to save");
                continue;
            }
            if (persist_save_results(req.path, g_ctx, g_world, g_results) != 0) {
                send_error(client_fd, 14, "Save failed");
                continue;
            }
            send_ack(client_fd, RW_MSG_SAVE_RESULTS, 0);
            continue;
        }

        if (hdr.type == RW_MSG_LOAD_RESULTS && hdr.payload_len == sizeof(rw_load_results_t)) {
            rw_load_results_t req;
            if (rw_recv_payload(client_fd, &req, sizeof(req)) != 0) {
                break;
            }
            if (!server_context_client_can_control(g_ctx, client_fd)) {
                send_error(client_fd, 1, "Permission denied");
                continue;
            }
            req.path[RW_PATH_MAX - 1] = '\0';
            if (!g_world || !g_results) {
                send_error(client_fd, 7, "Server handles not set");
                continue;
            }
            if (persist_load_results(req.path, g_ctx, g_world, g_results) != 0) {
                send_error(client_fd, 15, "Load failed");
                continue;
            }
            server_context_set_sim_state(g_ctx, RW_WIRE_SIM_FINISHED);
            send_ack(client_fd, RW_MSG_LOAD_RESULTS, 0);
            continue;
        }

        if (hdr.type == RW_MSG_QUIT && hdr.payload_len == sizeof(rw_quit_t)) {
            rw_quit_t q;
            if (rw_recv_payload(client_fd, &q, sizeof(q)) != 0) {
                break;
            }
            if (q.stop_if_owner && server_context_client_can_control(g_ctx, client_fd)) {
                if (g_sm) {
                    sim_manager_request_stop(g_sm);
                }
            }
            send_ack(client_fd, RW_MSG_QUIT, 0);
            break;
        }

        //unknown message type
        if (hdr.payload_len > 0) {
            char buf[256];
            uint32_t left = hdr.payload_len;

            while (left > 0) {
                uint32_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
                if (rw_recv_payload(client_fd, buf, chunk) != 0) {
                    left = 0;
                    break;
                }
                left -= chunk;
            }
        }
    }
    //cleanup
    server_context_remove_client(g_ctx, client_fd);

    /* If owner left, clear owner (next client may become owner). */
    if (server_context_get_owner_fd(g_ctx) == client_fd) {
        server_context_set_owner_fd(g_ctx, -1);
    }

    close(client_fd);

    log_info("Client disconnected (fd=%d)", client_fd);
    return NULL;
}
/*======================================================================
 *Handle client connection
 *======================================================================*/
/**
 * @brief Process a client's JOIN request and send WELCOME.
 *
 * @param client_fd Client socket.
 * @return 0 on success, -1 on error.
 */
static int handle_join(int client_fd) {
    rw_msg_hdr_t hdr;

    /*expect JOIN message*/
    if (rw_recv_hdr(client_fd, &hdr) != 0) {
        log_error("Failed to receive message header from client (fd=%d)", client_fd);
        return -1;
    }
    if (hdr.type != RW_MSG_JOIN) {
        log_error("Expected JOIN message from client (fd=%d), got type=%d", client_fd, hdr.type);
        return -1;
    }
    if (hdr.payload_len != sizeof(rw_join_t)) {
        log_error("Invalid JOIN message payload length from client (fd=%d)", client_fd);
        return -1;
    }
    rw_join_t join_msg;
    if (rw_recv_payload(client_fd, &join_msg, sizeof(join_msg)) != 0) {
        log_error("Failed to receive JOIN message payload from client (fd=%d)", client_fd);
        return -1;
    }

    log_info("Client (pid=%d) joined (fd=%d)", join_msg.pid, client_fd);

    /*send WELCOME message*/
    rw_welcome_t welcome_msg;
    memset(&welcome_msg, 0, sizeof(welcome_msg));

    welcome_msg.world_kind = (rw_wire_world_kinds_t)g_ctx->world_kind;
    welcome_msg.size.width = (uint32_t)g_ctx->world_size.width;
    welcome_msg.size.height = (uint32_t)g_ctx->world_size.height;

    welcome_msg.probs.p_up = g_ctx->probs.p_up;
    welcome_msg.probs.p_down = g_ctx->probs.p_down;
    welcome_msg.probs.p_left = g_ctx->probs.p_left;
    welcome_msg.probs.p_right = g_ctx->probs.p_right;

    welcome_msg.k_max_steps = g_ctx->k_max_steps;
    welcome_msg.total_reps = g_ctx->total_reps;
    welcome_msg.current_rep = g_ctx->current_rep;

    welcome_msg.global_mode = (rw_wire_global_mode_t)g_ctx->global_mode;
    welcome_msg.origin.x = 0;
    welcome_msg.origin.y = 0;

    if (rw_send_msg(client_fd, RW_MSG_WELCOME,
                    &welcome_msg, sizeof(welcome_msg)) != 0) {
        log_error("Failed to send WELCOME message to client (fd=%d)", client_fd);
        return -1;
    }
    log_info("WELCOME (pid=%d)", join_msg.pid);
    return 0;
}

/**
 * @brief Send a GLOBAL_MODE_CHANGED message to a single client.
 *
 * Used as a callback for `server_context_for_each_client()`.
 *
 * @param fd Client socket.
 * @param user Pointer to a broadcast context (see `broadcast_mode_ctx_t`).
 */
static void send_global_mode_changed(int fd, void *user) {
    broadcast_mode_ctx_t *ctx = (broadcast_mode_ctx_t *)user;
    (void)rw_send_msg_noblock(fd, RW_MSG_GLOBAL_MODE_CHANGED,
                              &ctx->msg, sizeof(ctx->msg));
}

/**
 * @brief Broadcast a GLOBAL_MODE_CHANGED notification to all connected clients.
 *
 * @param ctx Server context.
 * @param new_mode New mode in wire representation.
 * @param changed_by_pid PID of the client who triggered the change (0 if unknown).
 */
static void broadcast_global_mode_changed(server_context_t *ctx,
                                        rw_wire_global_mode_t new_mode,
                                        uint32_t changed_by_pid) {
    broadcast_mode_ctx_t bctx;
    bctx.msg.new_mode = new_mode;
    bctx.msg.changed_by_pid = changed_by_pid;

    server_context_for_each_client(ctx, send_global_mode_changed, &bctx);
}

void server_ipc_set_sim_handles(server_context_t *ctx, world_t *world, results_t *results, sim_manager_t *sm) {
    g_ctx = ctx;
    g_world = world;
    g_results = results;
    g_sm = sm;
}

/* NOTE: implementations are located at the end of this file. */

static void send_error(int fd, uint32_t code, const char *msg) {
    rw_error_t e;
    memset(&e, 0, sizeof(e));
    e.error_code = code;
    if (msg) {
        snprintf(e.error_msg, sizeof(e.error_msg), "%s", msg);
    }
    rw_send_msg(fd, RW_MSG_ERROR, &e, sizeof(e));
}

static void send_ack(int fd, uint16_t req_type, uint16_t status) {
    rw_ack_t a;
    a.request_type = req_type;
    a.status = status;
    rw_send_msg(fd, RW_MSG_ACK, &a, sizeof(a));
}

static void send_end_fn(int fd, void *user) {
    const rw_end_t *e = (const rw_end_t *)user;
    (void)rw_send_msg_noblock(fd, RW_MSG_END, e, sizeof(*e));
}

static void broadcast_end_msg(server_context_t *ctx, uint32_t reason) {
    rw_end_t e;
    e.reason = reason;
    server_context_for_each_client(ctx, send_end_fn, &e);
}

static void on_sim_end_cb(void *user, int stopped) {
    server_context_t *ctx = (server_context_t *)user;
    broadcast_end_msg(ctx, stopped ? 1u : 0u);
}

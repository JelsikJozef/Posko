//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_ipc.h"

#include "../common/protocol.h"
#include "../common/util.h"
#include "server_context.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static int listen_fd = -1;
static char socket_path_buf[108];
static pthread_t accept_thread;
static struct server_context *g_ctx = NULL;

/*forward declarations*/
static void *accept_loop(void *arg);
static void *client_thread(void *arg);
static int handle_join(int client_fd);
static void broadcast_global_mode_changed(struct server_context *ctx, rw_wire_global_mode_t new_mode, uint32_t changed_by_pid);

typedef struct {
    rw_global_mode_changed_t msg;
}broadcast_mode_ctx_t;

/*======================================================================
*public API
*======================================================================*/

int server_ipc_start(const char *socket_path, struct server_context *ctx) {
    struct sockaddr_un addr;

    if (!socket_path || !ctx) {
        return -1;
    }

    g_ctx = ctx;
    strncpy(socket_path_buf, socket_path, sizeof(socket_path_buf)-1);
    socket_path_buf[sizeof(socket_path_buf)-1] = '\0';

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        die("socket() failed");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_buf, sizeof(addr.sun_path)-1);

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
    close(client_fd);

    log_info("Client disconnected (fd=%d)", client_fd);
    return NULL;
}
/*======================================================================
 *Handle client connection
 *======================================================================*/
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

static void send_global_mode_changed(int fd, void *user) {
    broadcast_mode_ctx_t *ctx = (broadcast_mode_ctx_t *)user;
    rw_send_msg(fd, RW_MSG_GLOBAL_MODE_CHANGED,
                &ctx->msg, sizeof(ctx->msg));
}

static void broadcast_global_mode_changed(server_context_t *ctx,
                                        rw_wire_global_mode_t new_mode,
                                        uint32_t changed_by_pid) {
    broadcast_mode_ctx_t bctx;
    bctx.msg.new_mode = new_mode;
    bctx.msg.changed_by_pid = changed_by_pid;

    server_context_for_each_client(ctx, send_global_mode_changed, &bctx);
}

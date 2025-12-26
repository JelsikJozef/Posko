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

static int listen_fd = -1;
static char socket_path_buf[108];
static pthread_t accept_thread;
static struct server_context *g_ctx = NULL;

/*forward*/
static void *accept_loop(void *arg);
static void handle_client(int client_fd);

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
    if (listen(listen_fd, 8) < 0) {
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
        handle_client(client_fd);
        close(client_fd);
        log_info("Client disconnected (fd=%d)", client_fd);
    }
    return NULL;
}
/*======================================================================
 *Handle client connection
 *======================================================================*/
static void handle_client(int client_fd) {
    rw_msg_hdr_t hdr;

    /*expect JOIN message*/
    if (rw_recv_hdr(client_fd, &hdr) != 0) {
        log_error("Failed to receive message header from client (fd=%d)", client_fd);
        return;
    }
    if (hdr.type != RW_MSG_JOIN) {
        log_error("Expected JOIN message from client (fd=%d), got type=%d", client_fd, hdr.type);
        return;
    }
    if (hdr.payload_len != sizeof(rw_join_t)) {
        log_error("Invalid JOIN message payload length from client (fd=%d)", client_fd);
        return;
    }
    rw_join_t join_msg;
    if (rw_recv_payload(client_fd, &join_msg, sizeof(join_msg)) != 0) {
        log_error("Failed to receive JOIN message payload from client (fd=%d)", client_fd);
        return;
    }

    log_info("Client (pid=%d) joined (fd=%d)", join_msg.pid, client_fd);

    /*send WELCOME message*/
    rw_welcome_t welcome_msg;
    memset(&welcome_msg, 0, sizeof(welcome_msg));

    welcome_msg.world_kind = (rw_wire_world_kinds_t)g_ctx->world_kind;
    welcome_msg.size.width = (uint32_t)g_ctx->size.width;
    welcome_msg.size.height = (uint32_t)g_ctx->size.height;

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
        return;
    }
    log_info("WELCOME (pid=%d)", join_msg.pid);
}
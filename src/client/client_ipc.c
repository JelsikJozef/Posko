//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "client_ipc.h"

#include "../common/util.h"
#include "../common/protocol.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/**
 * @file client_ipc.c
 * @brief Implementation of client-side IPC helpers.
 */

/**
 * @brief Connect to the server AF_UNIX socket.
 *
 * @param socket_path Socket path on the filesystem.
 * @return Connected socket FD (>=0) on success, -1 on failure.
 */
int client_ipc_connect(const char* socket_path) {
    if (!socket_path) {
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("socket() failed");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (rw_copy_socket_path(addr.sun_path, sizeof(addr.sun_path), socket_path) != 0) {
        log_error("Socket path too long");
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("connect() to server socket failed");
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * @brief Send a JOIN request containing the current process ID.
 *
 * @param fd Connected client socket.
 * @return 0 on success, -1 on failure.
 */
int client_ipc_send_join(int fd) {
    rw_join_t join;
    join.pid = (int32_t)getpid();

    if (rw_send_msg(fd, RW_MSG_JOIN, &join, sizeof(join)) != 0) {
        log_error("Failed to send JOIN message to server");
        return -1;
    }
    return 0;
}

/**
 * @brief Receive and validate the server WELCOME message.
 *
 * This is a blocking read. The function expects that the next message on the socket
 * is a WELCOME message.
 *
 * @param fd Connected client socket.
 * @param out_welcome Output structure to fill.
 * @return 0 on success, -1 on protocol/IO error.
 */
int client_ipc_recv_welcome(int fd, rw_welcome_t *out_welcome) {
    if (!out_welcome) {
        return -1;
    }
    rw_msg_hdr_t hdr;

    if (rw_recv_hdr(fd, &hdr) != 0) {
        log_error("Failed to receive message header from server");
        return -1;
    }
    if (hdr.type != RW_MSG_WELCOME) {
        log_error("Expected WELCOME message from server, got type=%d", hdr.type);
        return -1;
    }

    if (hdr.payload_len != sizeof(rw_welcome_t)) {
        log_error("Invalid WELCOME message payload length from server");
        return -1;
    }

    if (rw_recv_payload(fd, out_welcome, sizeof(rw_welcome_t)) != 0) {
        log_error("Failed to receive WELCOME message payload from server");
        return -1;
    }
    return 0;
}

/**
 * @brief Send a SET_GLOBAL_MODE request.
 *
 * @param fd Connected client socket.
 * @param mode New global mode in wire format.
 * @return 0 on success, -1 on failure.
 */
int client_ipc_set_global_mode(int fd, rw_wire_global_mode_t mode) {
    rw_set_global_mode_t msg;
    msg.new_mode = mode;

    if (rw_send_msg(fd, RW_MSG_SET_GLOBAL_MODE, &msg, sizeof(msg)) != 0) {
        log_error("Failed to send SET_GLOBAL_MODE message to server");
        return -1;
    }
    return 0;
}


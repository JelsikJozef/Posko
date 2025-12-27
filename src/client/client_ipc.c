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
#include <stdio.h>

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

static int recv_ack_or_error(int fd, uint16_t expected_req_type) {
    rw_msg_hdr_t hdr;
    if (rw_recv_hdr(fd, &hdr) != 0) {
        log_error("Failed to receive server response header");
        return -1;
    }

    if (hdr.type == RW_MSG_ACK && hdr.payload_len == sizeof(rw_ack_t)) {
        rw_ack_t ack;
        if (rw_recv_payload(fd, &ack, sizeof(ack)) != 0) {
            return -1;
        }
        if (ack.request_type != expected_req_type) {
            log_error("ACK for unexpected request: got=%u expected=%u", (unsigned)ack.request_type, (unsigned)expected_req_type);
            return -1;
        }
        if (ack.status != 0) {
            log_error("Server returned non-zero status=%u for request=%u", (unsigned)ack.status, (unsigned)expected_req_type);
            return -1;
        }
        return 0;
    }

    if (hdr.type == RW_MSG_ERROR && hdr.payload_len == sizeof(rw_error_t)) {
        rw_error_t err;
        if (rw_recv_payload(fd, &err, sizeof(err)) != 0) {
            return -1;
        }
        err.error_msg[sizeof(err.error_msg) - 1] = '\0';
        log_error("Server error (%u): %s", (unsigned)err.error_code, err.error_msg);
        return -1;
    }

    /* Unexpected message; drain payload if any. */
    if (hdr.payload_len > 0) {
        char buf[256];
        uint32_t left = hdr.payload_len;
        while (left > 0) {
            uint32_t n = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
            if (rw_recv_payload(fd, buf, n) != 0) {
                break;
            }
            left -= n;
        }
    }

    log_error("Unexpected response type=%u len=%u", (unsigned)hdr.type, (unsigned)hdr.payload_len);
    return -1;
}

int client_ipc_query_status(int fd, rw_status_t *out_status) {
    if (!out_status) return -1;

    rw_query_status_t q;
    q.pid = (uint32_t)getpid();

    if (rw_send_msg(fd, RW_MSG_QUERY_STATUS, &q, sizeof(q)) != 0) {
        return -1;
    }

    rw_msg_hdr_t hdr;
    if (rw_recv_hdr(fd, &hdr) != 0) {
        return -1;
    }
    if (hdr.type != RW_MSG_STATUS || hdr.payload_len != sizeof(rw_status_t)) {
        log_error("Expected STATUS, got type=%u len=%u", (unsigned)hdr.type, (unsigned)hdr.payload_len);
        return -1;
    }
    if (rw_recv_payload(fd, out_status, sizeof(*out_status)) != 0) {
        return -1;
    }
    return 0;
}

int client_ipc_create_sim(int fd, const rw_create_sim_t *req) {
    if (!req) return -1;
    if (rw_send_msg(fd, RW_MSG_CREATE_SIM, req, sizeof(*req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_CREATE_SIM);
}

int client_ipc_load_world(int fd, const rw_load_world_t *req) {
    if (!req) return -1;
    if (rw_send_msg(fd, RW_MSG_LOAD_WORLD, req, sizeof(*req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_LOAD_WORLD);
}

int client_ipc_start_sim(int fd) {
    if (rw_send_msg(fd, RW_MSG_START_SIM, NULL, 0) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_START_SIM);
}

int client_ipc_restart_sim(int fd, uint32_t total_reps) {
    rw_restart_sim_t req;
    req.total_reps = total_reps;

    if (rw_send_msg(fd, RW_MSG_RESTART_SIM, &req, sizeof(req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_RESTART_SIM);
}

int client_ipc_request_snapshot(int fd) {
    rw_request_snapshot_t req;
    req.pid = (uint32_t)getpid();

    if (rw_send_msg(fd, RW_MSG_REQUEST_SNAPSHOT, &req, sizeof(req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_REQUEST_SNAPSHOT);
}

int client_ipc_save_results(int fd, const char *path) {
    if (!path) return -1;
    rw_save_results_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.path, sizeof(req.path), "%s", path);

    if (rw_send_msg(fd, RW_MSG_SAVE_RESULTS, &req, sizeof(req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_SAVE_RESULTS);
}

int client_ipc_load_results(int fd, const char *path) {
    if (!path) return -1;
    rw_load_results_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.path, sizeof(req.path), "%s", path);

    if (rw_send_msg(fd, RW_MSG_LOAD_RESULTS, &req, sizeof(req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_LOAD_RESULTS);
}

int client_ipc_quit(int fd, int stop_if_owner) {
    rw_quit_t q;
    q.pid = (uint32_t)getpid();
    q.stop_if_owner = stop_if_owner ? 1 : 0;
    q.reserved8[0] = q.reserved8[1] = q.reserved8[2] = 0;

    (void)rw_send_msg(fd, RW_MSG_QUIT, &q, sizeof(q));
    /* Best-effort; expect ACK but tolerate connection close. */
    (void)recv_ack_or_error(fd, RW_MSG_QUIT);
    return 0;
}

int client_ipc_stop_sim(int fd) {
    rw_stop_sim_t req;
    req.pid = (uint32_t)getpid();

    if (rw_send_msg(fd, RW_MSG_STOP_SIM, &req, sizeof(req)) != 0) {
        return -1;
    }
    return recv_ack_or_error(fd, RW_MSG_STOP_SIM);
}

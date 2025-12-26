//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "client_ipc.h"
#include "../common/util.h"
#include "../common/protocol.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>

/**
 * @file client_main.c
 * @brief Minimal interactive client used to test the IPC protocol.
 *
 * The client connects to the server, sends JOIN, prints WELCOME parameters, and then
 * allows the user to change the global mode using single-key commands.
 */

/**
 * @brief Drain and discard a payload from a socket.
 *
 * Used to skip over message payloads that the client does not yet understand.
 *
 * @param fd Socket file descriptor to read from.
 * @param len Number of bytes to read and discard.
 */
static void drain_payload(int fd, uint32_t len) {
    char buf[256];
    uint32_t left = len;
    while (left > 0) {
        uint32_t n = left > sizeof(buf) ? sizeof(buf) : left;
        if (rw_recv_payload(fd, buf, n) != 0) {
            break;
        }
        left -= n;
    }
}

/**
 * @brief Receive one server message and handle it.
 *
 * Currently supported:
 * - `RW_MSG_GLOBAL_MODE_CHANGED`
 *
 * Other message types are ignored (payload is drained).
 *
 * @param fd Connected socket to the server.
 */
static void handle_server_message(int fd) {
    rw_msg_hdr_t hdr;
    if (rw_recv_hdr(fd, &hdr) != 0) {
        die("Server disconnected");
    }

    if (hdr.type == RW_MSG_GLOBAL_MODE_CHANGED &&
        hdr.payload_len == sizeof(rw_global_mode_changed_t)) {

        rw_global_mode_changed_t msg;
        if (rw_recv_payload(fd, &msg, sizeof(msg)) != 0) {
            die("Failed to receive GLOBAL_MODE_CHANGED payload");
        }

        log_info("GLOBAL_MODE_CHANGED: new_mode=%u changed_by_pid=%u",
                 (unsigned)msg.new_mode, (unsigned)msg.changed_by_pid);
        return;
    }

    /* other messages are ignored for now */
    if (hdr.payload_len > 0) {
        drain_payload(fd, hdr.payload_len);
    }
}

/**
 * @brief Handle a single key press from stdin.
 *
 * Controls:
 * - 'i' => request INTERACTIVE mode
 * - 's' => request SUMMARY mode
 * - 'q' => quit immediately
 *
 * @param fd Connected socket to the server.
 */
static void handle_stdin(int fd) {
    int c = getchar();
    if (c == EOF) {
        return;
    }

    if (c == '\n' || c == '\r') {
        return;
    }

    if (c == 'i') {
        client_ipc_set_global_mode(fd, RW_WIRE_MODE_INTERACTIVE);
        log_info("Sent SET_GLOBAL_MODE=INTERACTIVE");
    } else if (c == 's') {
        client_ipc_set_global_mode(fd, RW_WIRE_MODE_SUMMARY);
        log_info("Sent SET_GLOBAL_MODE=SUMMARY");
    } else if (c == 'q') {
        log_info("Quitting...");
        close(fd);
        _exit(0);
    } else {
        log_info("Unknown key '%c' (use i/s/q)", c);
    }
}

/**
 * @brief Program entry point.
 *
 * Usage: `client <socket_path>`
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on normal exit, non-zero on argument/connection errors.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <socket_path>\n", argv[0]);
        return 1;
    }

    const char *socket_path = argv[1];

    int fd = client_ipc_connect(socket_path);
    if (fd < 0) {
        die("Failed to connect to server");
    }
    log_info("Connected to server socket %s", socket_path);

    if (client_ipc_send_join(fd) != 0) {
        die("Failed to send JOIN");
    }

    rw_welcome_t welcome;
    if (client_ipc_recv_welcome(fd, &welcome) != 0) {
        die("Failed to receive WELCOME");
    }

    log_info("WELCOME received:");
    log_info("  world_kind=%u", (unsigned)welcome.world_kind);
    log_info("  size=%ux%u", welcome.size.width, welcome.size.height);
    log_info("  probs: up=%.3f down=%.3f left=%.3f right=%.3f",
             welcome.probs.p_up, welcome.probs.p_down,
             welcome.probs.p_left, welcome.probs.p_right);
    log_info("  K=%u reps=%u current=%u",
             welcome.k_max_steps, welcome.total_reps, welcome.current_rep);
    log_info("  global_mode=%u", (unsigned)welcome.global_mode);

    log_info("Controls: i=INTERACTIVE, s=SUMMARY, q=quit");

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        int maxfd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;

        int r = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (r < 0) {
            die("select() failed");
        }

        if (FD_ISSET(fd, &rfds)) {
            handle_server_message(fd);
        }
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            handle_stdin(fd);
        }
    }

    /* unreachable */
}

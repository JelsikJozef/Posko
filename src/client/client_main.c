//
// Created by Jozef Jelšík on 26/12/2025.
//

/* client_main.c (minimal test) */
#include "client_ipc.h"

#include "../common/util.h"

#include <stdio.h>
#include <unistd.h>

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

    close(fd);
    return 0;
}

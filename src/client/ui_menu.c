//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "ui_menu.h"

#include "client_ipc.h"
#include "client_dispatcher.h"
#include "snapshot_reciever.h"
#include "../common/util.h"
#include "../common/protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void print_status_summary(const rw_status_t *st) {
    const char *state = "?";
    if (st->state == RW_WIRE_SIM_LOBBY) state = "LOBBY";
    else if (st->state == RW_WIRE_SIM_RUNNING) state = "RUNNING";
    else if (st->state == RW_WIRE_SIM_FINISHED) state = "FINISHED";

    printf("\n[STATUS] state=%s multi_user=%u can_control=%u\n", state, st->multi_user, st->can_control);
    printf("         world=%u size=%ux%u K=%u reps=%u progress=%u\n\n",
           (unsigned)st->world_kind,
           (unsigned)st->size.width, (unsigned)st->size.height,
           (unsigned)st->k_max_steps,
           (unsigned)st->total_reps,
           (unsigned)st->current_rep);
}

static int read_line(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    if (!fgets(buf, (int)cap, stdin)) {
        return -1;
    }
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[n - 1] = '\0';
        n--;
    }
    return 0;
}

static int prompt_u32(const char *label, uint32_t *out) {
    char line[128];
    while (1) {
        printf("%s: ", label);
        fflush(stdout);
        if (read_line(line, sizeof(line)) != 0) return -1;
        unsigned long v = 0;
        if (sscanf(line, "%lu", &v) == 1 && v <= 0xFFFFFFFFu) {
            *out = (uint32_t)v;
            return 0;
        }
        printf("Invalid number. Try again.\n");
    }
}

static int prompt_double(const char *label, double *out) {
    char line[128];
    while (1) {
        printf("%s: ", label);
        fflush(stdout);
        if (read_line(line, sizeof(line)) != 0) return -1;
        double v = 0.0;
        if (sscanf(line, "%lf", &v) == 1) {
            *out = v;
            return 0;
        }
        printf("Invalid number. Try again.\n");
    }
}

static int prompt_yes_no(const char *label, int *out_yes) {
    char line[32];
    while (1) {
        printf("%s (y/n): ", label);
        fflush(stdout);
        if (read_line(line, sizeof(line)) != 0) return -1;
        if (line[0] == 'y' || line[0] == 'Y') {
            *out_yes = 1;
            return 0;
        }
        if (line[0] == 'n' || line[0] == 'N') {
            *out_yes = 0;
            return 0;
        }
        printf("Please enter y or n.\n");
    }
}

static int menu_new_sim(int fd) {
    int use_load = 0;
    if (prompt_yes_no("Load world from file?", &use_load) != 0) return -1;

    int multi = 0;
    if (prompt_yes_no("Multi-user mode?", &multi) != 0) return -1;

    if (use_load) {
        char path[RW_PATH_MAX];
        printf("World/results file path to load (RWRES): ");
        fflush(stdout);
        if (read_line(path, sizeof(path)) != 0) return -1;

        /* RWRES contains both world + results; load both so summaries work. */
        if (client_ipc_load_results(fd, path) != 0) {
            return -1;
        }
        return 0;
    }

    rw_create_sim_t req;
    memset(&req, 0, sizeof(req));

    uint32_t w = 0, h = 0;
    if (prompt_u32("World width", &w) != 0) return -1;
    if (prompt_u32("World height", &h) != 0) return -1;

    int obstacles = 0;
    if (prompt_yes_no("World type obstacles? (n=wrap)", &obstacles) != 0) return -1;

    uint32_t reps = 0;
    if (prompt_u32("Number of replications", &reps) != 0) return -1;

    uint32_t K = 0;
    if (prompt_u32("K (max steps)", &K) != 0) return -1;

    double p_up, p_down, p_left, p_right;
    if (prompt_double("p_up", &p_up) != 0) return -1;
    if (prompt_double("p_down", &p_down) != 0) return -1;
    if (prompt_double("p_left", &p_left) != 0) return -1;
    if (prompt_double("p_right", &p_right) != 0) return -1;

    req.world_kind = obstacles ? RW_WIRE_WORLD_OBSTACLES : RW_WIRE_WORLD_WRAP;
    req.size.width = w;
    req.size.height = h;
    req.probs.p_up = p_up;
    req.probs.p_down = p_down;
    req.probs.p_left = p_left;
    req.probs.p_right = p_right;
    req.k_max_steps = K;
    req.total_reps = reps;
    req.multi_user = (uint8_t)(multi ? 1 : 0);

    return client_ipc_create_sim(fd, &req);
}

static int menu_restart_finished(int fd) {
    char load_path[RW_PATH_MAX];
    printf("Load results from file (RWRES path): ");
    fflush(stdout);
    if (read_line(load_path, sizeof(load_path)) != 0) return -1;

    if (client_ipc_load_results(fd, load_path) != 0) {
        return -1;
    }

    uint32_t new_reps = 0;
    if (prompt_u32("New number of replications", &new_reps) != 0) return -1;

    char save_path[RW_PATH_MAX];
    printf("Save results to file (RWRES path): ");
    fflush(stdout);
    if (read_line(save_path, sizeof(save_path)) != 0) return -1;

    if (client_ipc_restart_sim(fd, new_reps) != 0) {
        return -1;
    }

    printf("Simulation restarted. Waiting for END... (END will be printed asynchronously)\n");

    /* v1: do not block on reading END here (dispatcher is the only reader).
     * The dispatcher will print END when it arrives.
     */

    if (client_ipc_save_results(fd, save_path) != 0) {
        return -1;
    }

    printf("Saved to %s\n", save_path);
    return 0;
}

int ui_menu_run(const char *socket_path) {
    if (!socket_path) return 1;

    int fd = client_ipc_connect(socket_path);
    if (fd < 0) {
        die("Failed to connect to server");
    }

    /* Handshake BEFORE dispatcher: JOIN + blocking WELCOME receive. */
    if (client_ipc_send_join(fd) != 0) {
        die("Failed to send JOIN");
    }

    rw_welcome_t welcome;
    if (client_ipc_recv_welcome(fd, &welcome) != 0) {
        die("Failed to receive WELCOME");
    }

    log_info("Connected. WELCOME: size=%ux%u reps=%u K=%u", welcome.size.width, welcome.size.height, welcome.total_reps, welcome.k_max_steps);
    client_snapshot_set_k_max(welcome.k_max_steps);

    /* Start single-reader dispatcher AFTER handshake. */
    if (dispatcher_start(fd) != 0) {
        die("Failed to start dispatcher");
    }

    while (1) {
        rw_status_t st;
        if (client_ipc_query_status(fd, &st) != 0) {
            die("Failed to query status");
        }
        /* Keep snapshot summaries in sync with the latest server K. */
        client_snapshot_set_k_max(st.k_max_steps);
        print_status_summary(&st);

        printf("Main menu:\n");
        printf("  1) New simulation\n");
        printf("  2) Join existing simulation (no-op; just stays connected)\n");
        printf("  3) Restart finished simulation (load results + new reps + save)\n");
        printf("  4) Request snapshot\n");
        printf("  5) Start simulation (from lobby)\n");
        printf("  6) Save results\n");
        printf("  7) Stop simulation\n");
        printf("  8) Re-render last snapshot\n");
        printf("  9) Dump cell from last snapshot\n");
        printf("  0) Quit\n");
        printf("Choice: ");
        fflush(stdout);

        char line[32];
        if (read_line(line, sizeof(line)) != 0) {
            break;
        }
        int choice = -1;
        (void)sscanf(line, "%d", &choice);

        if (choice == 1) {
            if (menu_new_sim(fd) != 0) {
                log_error("Failed to create/load simulation");
            }
        } else if (choice == 2) {
            log_info("Joined. Waiting for progress/end... (async messages printed by dispatcher)");
        } else if (choice == 3) {
            if (menu_restart_finished(fd) != 0) {
                log_error("Restart failed");
            }
        } else if (choice == 4) {
            if (client_ipc_request_snapshot(fd) != 0) {
                log_error("Snapshot request failed");
            } else {
                log_info("Snapshot requested. Waiting for snapshot stream...");
            }
        } else if (choice == 5) {
            if (client_ipc_start_sim(fd) != 0) {
                log_error("Start failed");
            }
        } else if (choice == 6) {
            char path[RW_PATH_MAX];
            printf("Save results to file (RWRES path): ");
            fflush(stdout);
            if (read_line(path, sizeof(path)) == 0) {
                if (client_ipc_save_results(fd, path) != 0) {
                    log_error("Save failed");
                }
            }
        } else if (choice == 7) {
            if (client_ipc_stop_sim(fd) != 0) {
                log_error("Stop failed");
            }
        } else if (choice == 8) {
            if (client_snapshot_render_last() != 0) {
                log_error("No snapshot to render");
            }
        } else if (choice == 9) {
            uint32_t x = 0, y = 0;
            if (prompt_u32("Cell x", &x) == 0 && prompt_u32("Cell y", &y) == 0) {
                if (client_snapshot_dump_cell(x, y) != 0) {
                    log_error("Cell dump failed");
                }
            }
        } else if (choice == 0) {
            int stop = 0;
            if (isatty(STDIN_FILENO)) {
                (void)prompt_yes_no("Stop simulation if you are owner?", &stop);
            }
            (void)client_ipc_quit(fd, stop);
            dispatcher_stop();
            close(fd);
            client_snapshot_free();
            return 0;
        } else {
            printf("Unknown choice.\n");
        }
    }

    (void)client_ipc_quit(fd, 0);
    dispatcher_stop();
    close(fd);
    client_snapshot_free();
    return 0;
}

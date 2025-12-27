// server_main.c – full minimal server main (IPC + world + results + sim_manager)

#include "server_context.h"
#include "server_ipc.h"
#include "world.h"
#include "results.h"
#include "sim_manager.h"

#include "../common/util.h"

#include <signal.h>
#include <unistd.h>
#include <stdint.h>

/* jednoduché ukončenie cez Ctrl+C */
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

int main(void) {
    signal(SIGINT, on_sigint);

    /* ===== 1) server context ===== */
    server_context_t ctx;
    server_context_init(&ctx);

    /* parametre simulácie (zatím natvrdo – neskôr z argv/config) */
    ctx.world_kind = WORLD_WRAP;          /* alebo WORLD_OBSTACLES */
    ctx.world_size.width = 20;
    ctx.world_size.height = 20;

    ctx.probs.p_up = 0.25;
    ctx.probs.p_down = 0.25;
    ctx.probs.p_left = 0.25;
    ctx.probs.p_right = 0.25;

    ctx.k_max_steps = 200;
    ctx.total_reps = 50;
    ctx.current_rep = 0;

    ctx.global_mode = MODE_SUMMARY;

    const char *socket_path = "/tmp/rw_test.sock";

    /* ===== 2) world ===== */
    world_t world;
    if (world_init(&world, ctx.world_kind, ctx.world_size) != 0) {
        die("world_init failed");
    }

    /* len ak chceš prekážky */
    if (ctx.world_kind == WORLD_OBSTACLES) {
        world_generate_obstacles(&world, 10, 12345); /* 10% prekážok */
    } else {
        /* aj vo WRAP móde môžeš generovať, ale zatiaľ necháme prázdne */
        world_generate_obstacles(&world, 0, 1);
    }

    /* ===== 3) results ===== */
    results_t results;
    if (results_init(&results, ctx.world_size) != 0) {
        die("results_init failed");
    }

    /* ===== 4) IPC server ===== */
    if (server_ipc_start(socket_path, &ctx) != 0) {
        die("server_ipc_start failed");
    }

    /* ===== 5) sim manager ===== */
    sim_manager_t sm;
    if (sim_manager_init(&sm, &ctx, &world, &results,
                         4,        /* nthreads */
                         8192      /* queue capacity */
                         ) != 0) {
        die("sim_manager_init failed");
    }

    if (sim_manager_start(&sm) != 0) {
        die("sim_manager_start failed");
    }

    log_info("Server running. Ctrl+C to stop.");

    /* ===== 6) main loop ===== */
    while (!g_stop) {
        pause();
    }

    log_info("Stopping...");

    /* ===== 7) cleanup ===== */
    sim_manager_request_stop(&sm);
    sim_manager_destroy(&sm);

    server_ipc_stop();

    results_destroy(&results);
    world_destroy(&world);
    server_context_destroy(&ctx);

    return 0;
}

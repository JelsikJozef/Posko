//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_ipc.h"
#include "server_context.h"
#include "../common/util.h"
#include "world.h"

#include <unistd.h>

/**
 * @file server_main.c
 * @brief Minimal server entry point.
 *
 * This binary creates and configures a `server_context_t`, starts the IPC subsystem,
 * and then keeps running.
 */

/**
 * @brief Program entry point.
 *
 * Initializes the server state, starts listening on a Unix domain socket and then
 * blocks forever.
 *
 * @return This function does not normally return.
 */
int main(void) {
    server_context_t ctx;
    server_context_init(&ctx);

    /* tu si nastavíš parametre simulácie */
    ctx.world_kind = WORLD_WRAP;
    ctx.world_size.width = 10;
    ctx.world_size.height = 10;
    ctx.probs.p_up = 0.25;
    ctx.probs.p_down = 0.25;
    ctx.probs.p_left = 0.25;
    ctx.probs.p_right = 0.25;
    ctx.k_max_steps = 100;
    ctx.total_reps = 50;
    ctx.current_rep = 0;
    ctx.global_mode = MODE_SUMMARY;

    world_t world;
    world_size_t sz = {.width = 10, .height = 10};
    world_init(&world, WORLD_OBSTACLES, sz);
    world_generate_obstacles(&world, 20, 86785);

    log_info("obstacle(0,0)=%d", world_is_obstacle_xy(&world,0,0));
    log_info("obstacle(3,4)=%d", world_is_obstacle_xy(&world,3,4));
    world_destroy(&world);

    server_ipc_start("/tmp/rw_test.sock", &ctx);


    while (1) pause();
}

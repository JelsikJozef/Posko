//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "server_ipc.h"
#include "server_context.h"
#include "../common/util.h"

#include <unistd.h>

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

    server_ipc_start("/tmp/rw_test.sock", &ctx);

    while (1) pause();
}

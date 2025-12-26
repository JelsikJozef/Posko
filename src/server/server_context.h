//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_CONTEXT_H
#define SEMPRACA_SERVER_CONTEXT_H

#include <pthread.h>
#include "../common/types.h"

typedef struct server_context{
    world_kinds_t world_kind;
    world_size_t world_size;
    move_probs_t probs;

    uint32_t k_max_steps;
    uint32_t total_reps;
    uint32_t current_rep;

    global_mode_t global_mode;
}server_context_t;

#endif //SEMPRACA_SERVER_CONTEXT_H
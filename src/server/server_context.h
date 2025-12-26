//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_CONTEXT_H
#define SEMPRACA_SERVER_CONTEXT_H

#include <pthread.h>
#include "../common/types.h"

typedef struct {
    world_size_t size;
    int *obstacles;
    int total_reps;
    int current_rep;
    int k;
    move_probs_t probs;

    pthread_mutex_t stats_lock;
    global_mode_t global_mode;
}server_context_t;

#endif //SEMPRACA_SERVER_CONTEXT_H
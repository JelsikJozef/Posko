//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_RANDOM_WALK_H
#define SEMPRACA_RANDOM_WALK_H

#include  "../common/types.h"
#include "world.h"

#include  <stdint.h>
#include  <stdlib.h>

/*
 *Thread-safe RNG for random walk:
 *-Each worker's thread ha its own rw_rng_t
 *-random_r() uses internal state in this structure
 */

typedef struct {
    struct random_data rd;
    char state_buf[128];
    int initialized;
}rw_rng_t;

/* Initialisation of RND - seed from time + pid + tid */
void rw_rng_init_time_seed(rw_rng_t *rng);

//returns double in [0,1)
double rw_rng_next01(rw_rng_t *rng);

/*
 *Simulate one trajectory of random walk from start
 *Rules:
 *-Each step: choose direction based on probs
 *-if enters (0,0) -> reached_origin=1 and ends
 *-if walks max_steps and not reached (0,0) -> reached_origin=0 and ends
 *
 *world:
 *- if WORLD_WRAP: wrap around edges
 *- id WORLD OBSTACLES: stays in place if next cell is obstacle
 *
 *Outputs:
 *-out_steps: number of steps taken(0..max_steps)
 *-out_reached_origin: 1 if (0,0) reached, 0 otherwise
 *-out_success_leq_k: 1 if steps <= k_max_steps, 0 otherwise
 */
void random_walk_run(const world_t *w,
                    pos_t start,
                    move_probs_t probs,
                    uint32_t max_steps,
                    rw_rng_t *rng,
                    uint32_t *out_steps,
                    int *out_reached_origin,
                    int *out_success_leq_k);



#endif //SEMPRACA_RANDOM_WALK_H
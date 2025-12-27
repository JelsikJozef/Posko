//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_RESULTS_H
#define SEMPRACA_RESULTS_H

/*
 *results.h - saves stats for each tile int the world
 *
 * For each tile we keep:
 * -trials[i]
 * -sum_steps[i]
 * -success_leq_k[i]
 *
 * We can calculate from that:
 * -avg_steps = sum_steps[i] / trials[i]
 * -prob_leq_k = success_leq_k[i] / trials[i]
 *
 * note: results are kept on server: klient obtains them via IPC when needed
 */

#include "../common/types.h"
#include <stdint.h>
#include <pthread.h>

typedef struct {
    world_size_t size;
    uint32_t cell_count;

    /* tiles stats arrays of size cell_count */
    uint32_t *trials;        /* number of trials that ended in this cell */
    uint64_t *sum_steps;     /* sum of steps taken by all trials ending in this cell */
    uint32_t *success_leq_k; /* number of trials that ended in this cell with steps <= k_max_steps */

    pthread_mutex_t mtx;    /* mutex to protect updates to the results */
} results_t;

/* lifecycle */
int results_init(results_t *r, world_size_t size);
void results_destroy(results_t *r);

/* reset to zeros */
void results_clear(results_t *r);

/* update of one tile:
 * - always increments trials
 * - if came to origin, calculates steps to sum_steps
 * - if success_leq_k == 1, increments success
 */

void results_update(
    results_t *r,
    uint32_t idx,
    uint32_t steps,
    int reached_origin,
    int success_leq_k);

const uint32_t *results_trials(const results_t *r);
const uint64_t *results_sum_steps(const results_t *r);
const uint32_t *results_success_leq_k(const results_t *r);

uint32_t results_cell_count(const results_t *r);
world_size_t results_size(const results_t *r);

#endif //SEMPRACA_RESULTS_H
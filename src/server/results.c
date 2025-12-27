//
// Created by Jozef Jelšík on 26/12/2025.
//

/**
 * @file results.c
 * @brief Implementation of per-tile statistics storage.
 */

#include "results.h"

#include "../common/util.h"

#include <stdlib.h>
#include <string.h>

static uint32_t cell_count_from_size(world_size_t s) {
    return s.width * s.height;
}

int results_init(results_t *r, world_size_t size) {
    if (!r) return -1;
    if (size.width == 0 || size.height == 0) return -1;

    memset(r,0, sizeof(*r));
    r->size = size;
    r->cell_count = cell_count_from_size(size);

    r->trials = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)r->cell_count);
    r->sum_steps = (uint64_t*)malloc(sizeof(uint64_t) * (size_t)r->cell_count);
    r->success_leq_k = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)r->cell_count);

    if (!r->trials || !r->sum_steps || !r->success_leq_k) {
        results_destroy(r);
        return -1;
    }

    memset(r->trials, 0, sizeof(uint32_t) * (size_t)r->cell_count);
    memset(r->sum_steps, 0, sizeof(uint64_t) * (size_t)r->cell_count);
    memset(r->success_leq_k, 0, sizeof(uint32_t) * (size_t)r->cell_count);

    if (pthread_mutex_init(&r->mtx, NULL) != 0) {
        results_destroy(r);
        return -1;
    }
    return 0;
}

void results_destroy(results_t *r) {
    if (!r) return;

    /* The mutex is initialized in results_init(); destroy it once on teardown.
     * (If init failed before mutex initialization, destroying an uninitialized
     * mutex would be UB, so results_init() only calls results_destroy() before
     * pthread_mutex_init().)
     */
    (void)pthread_mutex_destroy(&r->mtx);

    free(r->trials);
    free(r->sum_steps);
    free(r->success_leq_k);

    r->trials = NULL;
    r->sum_steps = NULL;
    r->success_leq_k = NULL;

    r->cell_count = 0;
    r->size.width = 0;
    r->size.height = 0;
}

void results_clear(results_t *r) {
    if (!r) return;

    pthread_mutex_lock(&r->mtx);
    memset(r->trials, 0, sizeof(uint32_t) * (size_t)r->cell_count);
    memset(r->sum_steps, 0, sizeof(uint64_t) * (size_t)r->cell_count);
    memset(r->success_leq_k, 0, sizeof(uint32_t) * (size_t)r->cell_count);
    pthread_mutex_unlock(&r->mtx);
}

void results_update(
    results_t *r,
    uint32_t idx,
    uint32_t steps,
    int reached_origin,
    int success_leq_k) {
    if (!r) return;
    if (idx >= r->cell_count) return;

    pthread_mutex_lock(&r->mtx);

    r->trials[idx] += 1;

    if (reached_origin) {
        r->sum_steps[idx] += (uint64_t)steps;
    }

    if (success_leq_k) {
        r->success_leq_k[idx] += 1;
    }
    pthread_mutex_unlock(&r->mtx);
}

const uint32_t *results_trials(const results_t *r) {
    return r ? r->trials : NULL;
}

const uint64_t *results_sum_steps(const results_t *r) {
    return r ? r->sum_steps : NULL;
}
const uint32_t *results_success_leq_k(const results_t *r) {
    return r ? r->success_leq_k : NULL;
}

uint32_t results_cell_count(const results_t *r) {
    return r ? r->cell_count : 0;
}

world_size_t results_size(const results_t *r) {
    return r ? r->size : (world_size_t){0, 0};
}

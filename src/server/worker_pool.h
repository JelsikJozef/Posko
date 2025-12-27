//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORKER_POOL_H
#define SEMPRACA_WORKER_POOL_H

#include "world.h"
#include "random_walk.h"
#include "results.h"
#include "../common/types.h"

#include <pthread.h>
#include <stdint.h>

typedef struct {
    uint32_t cell_idx;
    pos_t start;
}rw_job_t;

typedef struct {
    pthread_t *threads;
    int nthreads;

    rw_job_t *q;
    uint32_t q_cap;
    uint32_t q_head;
    uint32_t q_tail;
    uint32_t q_count;

    //synchronisation

    pthread_mutex_t mtx;
    pthread_cond_t cv_nonempty;
    pthread_cond_t cv_all_done;

    int stop;

    //batch tracking
    uint32_t in_flight;

    //shared pointer (server-side)
    const world_t *world;
    results_t *results;
    move_probs_t probs;
    uint32_t max_steps;
}worker_pool_t;

int worker_pool_init(worker_pool_t *p,
                     int nthreads,
                     uint32_t queue_capacity,
                     const world_t *world,
                     results_t *results,
                     move_probs_t probs,
                     uint32_t max_steps);

void worker_pool_destroy(worker_pool_t *p);

int worker_pool_submit(worker_pool_t *p, rw_job_t job);

void worker_pool_wait_all(worker_pool_t *p);

void worker_pool_stop(worker_pool_t *p);

#endif //SEMPRACA_WORKER_POOL_H
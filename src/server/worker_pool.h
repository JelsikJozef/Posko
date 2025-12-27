//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORKER_POOL_H
#define SEMPRACA_WORKER_POOL_H

/**
 * @file worker_pool.h
 * @brief Simple thread pool used by the server to execute random-walk jobs.
 *
 * The worker pool maintains a bounded FIFO queue of @ref rw_job_t items.
 * Each worker thread repeatedly:
 * - pops a job
 * - runs one random walk from the provided start position
 * - updates shared @ref results_t
 *
 * Threading model:
 * - Queue operations and in-flight accounting are protected by an internal mutex.
 * - Results are updated via @ref results_update(), which is internally synchronized.
 */

#include "world.h"
#include "random_walk.h"
#include "results.h"
#include "../common/types.h"

#include <pthread.h>
#include <stdint.h>

/**
 * @brief Random-walk job type.
 */
typedef struct {
    /** Linear index into result arrays (row-major). */
    uint32_t cell_idx;

    /** Starting position for this random-walk job. */
    pos_t start;
} rw_job_t;

/**
 * @brief Worker pool state.
 */
typedef struct {
    pthread_t *threads; /**< Array of worker threads. */
    int nthreads;       /**< Number of worker threads. */

    rw_job_t *q;        /**< Job queue. */
    uint32_t q_cap;    /**< Capacity of the job queue. */
    uint32_t q_head;   /**< Index of the next job to be processed. */
    uint32_t q_tail;   /**< Index where the next job will be added. */
    uint32_t q_count;  /**< Current number of jobs in the queue. */

    /* synchronization */
    pthread_mutex_t mtx;                /**< Mutex for protecting shared data. */
    pthread_cond_t cv_nonempty;        /**< Condition variable for non-empty queue. */
    pthread_cond_t cv_all_done;        /**< Condition variable for all jobs done. */

    /** When non-zero, workers should exit. */
    int stop;

    /** Number of submitted jobs not yet marked done. */
    uint32_t in_flight;

    /* shared references provided by the server */
    const world_t *world; /**< World definition used for random walks. */
    results_t *results;   /**< Results accumulator. */
    move_probs_t probs;  /**< Movement probabilities. */
    uint32_t max_steps;  /**< Maximum steps per random walk. */
} worker_pool_t;

/**
 * @brief Initialize a worker pool and start worker threads.
 *
 * @param p              Pool to initialize.
 * @param nthreads       Number of worker threads.
 * @param queue_capacity Capacity of the internal job queue.
 * @param world          World definition used for random walks.
 * @param results        Results accumulator.
 * @param probs          Movement probabilities.
 * @param max_steps      Maximum steps per random walk.
 *
 * @retval 0  Success.
 * @retval -1 Invalid arguments or initialization failure.
 */
int worker_pool_init(worker_pool_t *p,
                     int nthreads,
                     uint32_t queue_capacity,
                     const world_t *world,
                     results_t *results,
                     move_probs_t probs,
                     uint32_t max_steps);

/**
 * @brief Stop workers (cooperative) and release all pool resources.
 *
 * Joins worker threads and frees internal allocations.
 *
 * @param p Pool (may be NULL).
 */
void worker_pool_destroy(worker_pool_t *p);

/**
 * @brief Submit one job to the pool.
 *
 * @param p   Pool.
 * @param job Job to submit.
 * @return 0 on success, -1 on failure (stopping, allocation issues, etc.).
 */
int worker_pool_submit(worker_pool_t *p, rw_job_t job);

/**
 * @brief Block until all submitted jobs are completed.
 *
 * @param p Pool.
 */
void worker_pool_wait_all(worker_pool_t *p);

/**
 * @brief Request the pool to stop.
 *
 * This wakes waiting workers and causes them to exit once they observe @ref worker_pool_t::stop.
 *
 * @param p Pool.
 */
void worker_pool_stop(worker_pool_t *p);

#endif //SEMPRACA_WORKER_POOL_H


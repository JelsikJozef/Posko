//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SIM_MANAGER_H
#define SEMPRACA_SIM_MANAGER_H

/**
 * @file sim_manager.h
 * @brief Simulation manager: orchestrates replications and job submission.
 *
 * The simulation manager owns a @ref worker_pool_t and runs a dedicated thread that:
 * - iterates repetitions
 * - submits per-cell random-walk jobs to the worker pool
 * - updates progress in @ref server_context_t
 *
 * It does not handle client IO directly; IPC is handled by the server IPC layer.
 */

#include "server_context.h"
#include  "world.h"
#include  "results.h"
#include  "worker_pool.h"

#include <pthread.h>
#include <stdint.h>

typedef void (*sim_manager_on_end_fn)(void *user, int stopped);

typedef struct {
    /** Shared server context (configuration + progress). */
    server_context_t *ctx;

    /** World definition used by simulations. */
    world_t *world;

    /** Results accumulator. */
    results_t *results;

    /** Worker pool used to run random walks concurrently. */
    worker_pool_t pool;

    /** Number of worker threads to use. */
    int nthreads;

    /** Job queue capacity for the worker pool. */
    uint32_t queue_capacity;

    /** Background thread running the simulation loop. */
    pthread_t thread;

    /** Non-zero when the manager thread is running. */
    int running;

    /** Non-zero when a stop was requested. */
    int stop_requested;

    /** Optional callback invoked when the simulation thread finishes. */
    sim_manager_on_end_fn on_end;
    void *on_end_user;
} sim_manager_t;

/**
 * @brief Initialize a simulation manager.
 *
 * Does not start any background thread yet; call @ref sim_manager_start().
 *
 * @param sm             Manager to initialize.
 * @param ctx            Shared server context.
 * @param world          World used for random walks.
 * @param results        Results accumulator.
 * @param nthreads       Number of worker threads.
 * @param queue_capacity Worker queue capacity.
 *
 * @retval 0  Success.
 * @retval -1 Invalid arguments or initialization failure.
 */
int sim_manager_init(sim_manager_t *sm,
                     server_context_t *ctx,
                     world_t *world,
                     results_t *results,
                     int nthreads,
                     uint32_t queue_capacity);

/**
 * @brief Destroy a simulation manager and release resources.
 *
 * If a simulation is running, it is requested to stop and joined.
 *
 * @param sm Manager to destroy (may be NULL).
 */
void sim_manager_destroy(sim_manager_t *sm);

/**
 * @brief Start the simulation manager background thread.
 *
 * @param sm Manager.
 * @return 0 on success, -1 on failure.
 */
int sim_manager_start(sim_manager_t *sm);

/**
 * @brief Join (wait for) the simulation thread if running.
 */
void sim_manager_join(sim_manager_t *sm);

/**
 * @brief Set an optional end callback.
 */
void sim_manager_set_on_end(sim_manager_t *sm, sim_manager_on_end_fn fn, void *user);

/**
 * @brief Restart the simulation with a new total_reps (clears results).
 */
int sim_manager_restart(sim_manager_t *sm, uint32_t total_reps);

/**
 * @brief Request the currently running simulation to stop.
 *
 * The stop is cooperative: worker threads will finish their current work and the
 * manager thread will exit its loop.
 *
 * @param sm Manager.
 */
void sim_manager_request_stop(sim_manager_t *sm);

#endif //SEMPRACA_SIM_MANAGER_H


//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_RESULTS_H
#define SEMPRACA_RESULTS_H

/**
 * @file results.h
 * @brief Per-tile statistics collected by the server during simulations.
 *
 * The server maintains one statistics slot per world tile (cell). Each slot stores:
 * - @c trials[i]        : number of simulation trials that ended in tile @c i
 * - @c sum_steps[i]     : sum of steps for trials ending in tile @c i (only for
 *                         trials that reached the origin, see @p reached_origin)
 * - @c success_leq_k[i] : count of trials ending in tile @c i with steps <= k
 *
 * From these arrays you can derive, per tile @c i:
 * - Average steps (conditional): @c avg_steps = sum_steps[i] / trials[i]
 * - Probability of success <= k: @c p_leq_k   = success_leq_k[i] / trials[i]
 *
 * @note Division by zero must be handled by the caller when @c trials[i] == 0.
 * @note Results are stored on the server; the client retrieves them via IPC
 *       when needed.
 *
 * Threading
 * ---------
 * Updates are protected by an internal mutex so that multiple worker threads can
 * call @ref results_update() concurrently.
 *
 * The pointer-returning getters (e.g. @ref results_trials()) expose the internal
 * arrays. They do not take the mutex and do not provide a consistent snapshot.
 * If you need a consistent view, synchronize externally with the same lifetime
 * guarantees as your use case (or extend the API to provide a copy/snapshot).
 */

#include "../common/types.h"
#include <pthread.h>
#include <stdint.h>

typedef struct {
    /** World dimensions for which these results were allocated. */
    world_size_t size;

    /** Total number of cells (size.width * size.height). */
    uint32_t cell_count;

    /**
     * Number of trials that ended in each cell.
     *
     * Length: @ref results_t::cell_count
     */
    uint32_t *trials;

    /**
     * Sum of steps taken by trials ending in each cell.
     *
     * Length: @ref results_t::cell_count
     *
     * @note The implementation only adds @p steps when @p reached_origin != 0 in
     *       @ref results_update().
     */
    uint64_t *sum_steps;

    /**
     * Number of trials ending in each cell with steps <= k.
     *
     * Length: @ref results_t::cell_count
     */
    uint32_t *success_leq_k;

    /** Mutex protecting updates/clears of the arrays above. */
    pthread_mutex_t mtx;
} results_t;

/**
 * @brief Initialize the results storage for a given world size.
 *
 * Allocates and zero-initializes all internal arrays.
 *
 * @param r    Destination structure.
 * @param size World size; both dimensions must be non-zero.
 *
 * @retval 0  Success.
 * @retval -1 Invalid arguments or allocation/mutex initialization failure.
 */
int results_init(results_t *r, world_size_t size);

/**
 * @brief Free resources associated with @p r.
 *
 * Safe to call with partially-initialized data (frees NULL pointers).
 *
 * @param r Results structure to destroy (may be NULL).
 */
void results_destroy(results_t *r);

/**
 * @brief Reset all per-tile counters to 0.
 *
 * @param r Results structure (may be NULL).
 */
void results_clear(results_t *r);

/**
 * @brief Update statistics for one tile.
 *
 * Always increments @c trials[idx]. Conditionally updates:
 * - @c sum_steps[idx] only when @p reached_origin != 0
 * - @c success_leq_k[idx] only when @p success_leq_k != 0
 *
 * @param r              Results structure.
 * @param idx            Tile index in row-major order.
 * @param steps          Number of steps for this trial.
 * @param reached_origin Non-zero if the trial reached the origin.
 * @param success_leq_k  Non-zero if the trial is considered successful with
 *                       respect to the configured k (steps <= k).
 */
void results_update(
    results_t *r,
    uint32_t idx,
    uint32_t steps,
    int reached_origin,
    int success_leq_k);

/**
 * @brief Get the trials array (internal storage).
 * @param r Results structure.
 * @return Pointer to an array of length @ref results_cell_count(), or NULL.
 * @warning Not a synchronized snapshot; see file documentation.
 */
const uint32_t *results_trials(const results_t *r);

/**
 * @brief Get the sum_steps array (internal storage).
 * @param r Results structure.
 * @return Pointer to an array of length @ref results_cell_count(), or NULL.
 * @warning Not a synchronized snapshot; see file documentation.
 */
const uint64_t *results_sum_steps(const results_t *r);

/**
 * @brief Get the success_leq_k array (internal storage).
 * @param r Results structure.
 * @return Pointer to an array of length @ref results_cell_count(), or NULL.
 * @warning Not a synchronized snapshot; see file documentation.
 */
const uint32_t *results_success_leq_k(const results_t *r);

/**
 * @brief Get the number of cells tracked by these results.
 * @param r Results structure.
 * @return cell count, or 0 if @p r is NULL.
 */
uint32_t results_cell_count(const results_t *r);

/**
 * @brief Get the world size associated with these results.
 * @param r Results structure.
 * @return size, or {0,0} if @p r is NULL.
 */
world_size_t results_size(const results_t *r);

#endif //SEMPRACA_RESULTS_H


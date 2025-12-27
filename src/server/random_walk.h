//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_RANDOM_WALK_H
#define SEMPRACA_RANDOM_WALK_H

/**
 * @file random_walk.h
 * @brief Random-walk core: per-thread RNG + one-trajectory simulation.
 *
 * This module provides:
 * - a small per-instance RNG type (@ref rw_rng_t) intended to be owned by a worker thread
 * - @ref random_walk_run(), which simulates one trajectory until the origin is reached
 *   or a maximum number of steps is exceeded
 */

#include  "../common/types.h"
#include "world.h"

#include  <stdint.h>

/**
 * @brief Per-thread random number generator state.
 *
 * Each worker thread should keep its own instance to avoid locking.
 */
typedef struct {
    uint64_t state;      /**< Internal RNG state. */
    int initialized;     /**< Non-zero once seeded. */
} rw_rng_t;

/**
 * @brief Initialize RNG state using a time-based seed.
 *
 * Seed is mixed from current time, process id, and thread id.
 *
 * @param rng RNG instance to initialize.
 */
void rw_rng_init_time_seed(rw_rng_t *rng);

/**
 * @brief Generate a pseudo-random floating-point value in [0, 1).
 *
 * @param rng Initialized RNG instance.
 * @return Uniform double in [0,1).
 */
double rw_rng_next01(rw_rng_t *rng);

/**
 * @brief Simulate one random-walk trajectory.
 *
 * Rules:
 * - Start at @p start.
 * - Each step chooses direction based on @p probs.
 * - If the walk reaches the origin (0,0), the run ends with reached_origin=1.
 * - If @p max_steps are executed without reaching origin, reached_origin=0.
 *
 * World semantics:
 * - If @c WORLD_WRAP: positions wrap around edges.
 * - If obstacles are enabled: attempting to step into an obstacle keeps the walker in place.
 *
 * Outputs:
 * - @p out_steps: number of steps actually taken (0..max_steps)
 * - @p out_reached_origin: 1 if origin reached, else 0
 * - @p out_success_leq_k: currently set to 1 only when origin reached (see server logic)
 *
 * @param w World.
 * @param start Starting position.
 * @param probs Movement probabilities.
 * @param max_steps Maximum number of steps.
 * @param rng Thread-local RNG.
 * @param out_steps Output: steps taken.
 * @param out_reached_origin Output: reached origin flag.
 * @param out_success_leq_k Output: success-within-K flag.
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


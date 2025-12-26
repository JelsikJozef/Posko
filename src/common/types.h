//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_TYPES_H
#define SEMPRACA_TYPES_H

/**
 * @file types.h
 * @brief Internal domain types shared between server and client logic.
 *
 * Important:
 * - These types are for internal logic (world, simulation, results).
 * - They are NOT a stable wire format for IPC.
 * - For socket communication, use `protocol.h` wire types (`rw_wire_*`).
 */

#include <stdint.h>

/**
 * @brief 2D grid position in internal (host) representation.
 */
typedef struct {
    int32_t x;
    int32_t y;
} pos_t;

/**
 * @brief Internal world dimensions.
 */
typedef struct {
    int32_t width;
    int32_t height;
} world_size_t;

/**
 * @brief Movement probabilities for a random-walk step.
 *
 * Probabilities are expected to be in range [0,1] and typically sum to 1.
 */
typedef struct {
    double p_up;
    double p_down;
    double p_left;
    double p_right;
} move_probs_t;

/**
 * @brief Global simulation mode as kept by the server.
 */
typedef enum {
    MODE_INTERACTIVE = 1,
    MODE_SUMMARY = 2,
} global_mode_t;

/**
 * @brief World topology/feature set.
 */
typedef enum {
    /** World wraps around edges (toroidal topology). */
    WORLD_WRAP = 1,
    /** World contains obstacles and does not wrap. */
    WORLD_OBSTACLES = 2,
} world_kinds_t;

/**
 * @brief Client-side view selection for rendering/aggregation in summary mode.
 */
typedef enum {
    /** Render/compute average steps to origin per cell. */
    VIEW_AVG_STEPS = 1,
    /** Render/compute probability of success within K steps per cell. */
    VIEW_PROB_LEQ_K = 2,
} summary_view_t;

/**
 * @brief Accumulated statistics for a single cell.
 */
typedef struct {
    /** Number of trials performed from this cell. */
    uint32_t trials;
    /** Sum of steps across all trials (used for average). */
    uint64_t sum_steps;
    /** Count of trials that reached the goal within K steps. */
    uint32_t succes_leq_k;
} cell_stats_t;

#endif //SEMPRACA_TYPES_H


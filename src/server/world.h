//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORLD_H
#define SEMPRACA_WORLD_H

/**
 * @file world.h
 * @brief World (grid) representation and obstacle utilities.
 *
 * This module defines the in-memory representation of the simulation world:
 * a 2D grid of size (width x height) whose cells can be either free or blocked
 * by an obstacle.
 *
 * Storage
 * -------
 * Obstacles are stored in a flat array in row-major order:
 * @code
 * idx = y * width + x
 * @endcode
 * with values:
 * - 0 = free cell
 * - 1 = obstacle
 *
 * Semantics
 * ---------
 * - @ref world_in_bounds() checks whether (x,y) lies inside the world rectangle.
 * - @ref world_wrap_pos() wraps a position into the world bounds using
 *   modulo arithmetic (useful for toroidal worlds).
 * - Obstacle queries treat out-of-bounds coordinates/indices as blocked
 *   (they return 1).
 *
 * Threading
 * ---------
 * This module does not synchronize access internally. If a world is shared
 * across threads while being mutated (e.g., via @ref world_set_obstacle()), the
 * caller must provide external synchronization.
 */

#include "../common/types.h"
#include <stdint.h>

typedef struct {
    /** World kind/topology (meaning defined elsewhere by world_kind_t). */
    world_kind_t kind;

    /** World dimensions. Must be non-zero after @ref world_init(). */
    world_size_t size;

    /**
     * Obstacle bitmap for all cells.
     *
     * Length: width*height bytes.
     * Values: 0 (free) or 1 (obstacle).
     *
     * Memory is owned by this structure and allocated in @ref world_init().
     */
    uint8_t *obstacles;
} world_t;

/**
 * @brief Initialize a world.
 *
 * Allocates a zeroed obstacle array of size width*height.
 *
 * @param w    World handle to initialize.
 * @param kind World kind/topology.
 * @param size World size; both dimensions must be non-zero.
 *
 * @retval 0  Success.
 * @retval -1 Invalid arguments or allocation failure.
 */
int world_init(world_t *w, world_kind_t kind, world_size_t size);

/**
 * @brief Free resources associated with the world.
 * @param w World to destroy (may be NULL).
 */
void world_destroy(world_t *w);

/**
 * @brief Generate obstacles with a deterministic pseudo-random distribution.
 *
 * Each cell is set to obstacle with probability approximately @p percent/100.
 * Percent is clamped to [0,100]. The same @p seed yields the same obstacle map.
 *
 * The origin cell (index 0, i.e. coordinate (0,0) in row-major order) is always
 * forced to be free.
 *
 * @param w       World.
 * @param percent Obstacle percentage in range [0,100] (values outside are clamped).
 * @param seed    RNG seed used for deterministic generation.
 */
void world_generate_obstacles(world_t *w, int percent, uint32_t seed);

/**
 * @brief Get total number of cells in the world.
 * @param w World.
 * @return width*height.
 */
uint32_t world_cell_count(const world_t *w);

/**
 * @brief Convert 2D coordinates to linear index (row-major).
 *
 * @param w World.
 * @param x X coordinate (column).
 * @param y Y coordinate (row).
 * @return Linear index (@c y*width + x).
 *
 * @warning This function does not validate bounds; use @ref world_in_bounds().
 */
uint32_t world_index(const world_t *w, int32_t x, int32_t y);

/**
 * @brief Test whether a coordinate is within the world rectangle.
 * @param w World.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @return Non-zero if in bounds, 0 otherwise.
 */
int world_in_bounds(const world_t *w, int32_t x, int32_t y);

/**
 * @brief Wrap a position into the world bounds.
 *
 * Performs modulo wrapping on both axes, producing a position guaranteed to be
 * in-bounds when width and height are non-zero.
 *
 * @param w World.
 * @param p Input position.
 * @return Wrapped position.
 */
pos_t world_wrap_pos(const world_t *w, pos_t p);

/**
 * @brief Check whether a cell is an obstacle (`idx` version).
 *
 * @param w   World.
 * @param idx Linear cell index.
 * @return 1 if the index is invalid or the cell is an obstacle; 0 otherwise.
 */
int world_is_obstacle_idx(const world_t *w, uint32_t idx);

/**
 * @brief Check whether a cell is an obstacle (`x,y` version).
 *
 * @param w World.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @return 1 if out of bounds or the cell is an obstacle; 0 otherwise.
 */
int world_is_obstacle_xy(const world_t *w, int32_t x, int32_t y);

/**
 * @brief Set or clear an obstacle at (x,y).
 *
 * If (x,y) is out of bounds, the function does nothing.
 *
 * @param w     World.
 * @param x     X coordinate.
 * @param y     Y coordinate.
 * @param value Non-zero sets obstacle, 0 clears it.
 */
void world_set_obstacle(world_t *w, int32_t x, int32_t y, int value);

#endif //SEMPRACA_WORLD_H


//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_TYPES_H
#define SEMPRACA_TYPES_H

/*types.h - internal types of domain
 *
 *Important:
 * - This file is for server/client logic (world, simulation, results).
 * - Cannot be used as "wire format" for IPC. For socket communication, use protocol.h
 */

#include <stdint.h>

/*===== Position in 2D grid (internal) =====*/
typedef struct {
    int32_t x;
    int32_t y;
}pos_t;

/*===== World size (internal) =====*/
typedef struct {
    int32_t width;
    int32_t height;
}world_size_t;

/*===== Move probabilities (internal) =====*/
typedef struct {
    double p_up;
    double p_down;
    double p_left;
    double p_right;
}move_probs_t;

/*===== Global simulation mode (server) =====*/
typedef enum {
    MODE_INTERACTIVE = 1,
    MODE_SUMMARY = 2,
}global_mode_t;

/*===== World kinds (internal) =====*/
typedef enum {
    WORLD_WRAP = 1, //World wraps around edges
    WORLD_OBSTACLES = 2, //World has obstacles
}world_kinds_t;

/*===== Local view of client in summary mode ======*/
typedef enum {
    VIEW_AVG_STEPS = 1,
    VIEW_PROB_LEQ_K = 2,
}summary_view_t;

/*===== Statistics for one cell (internal) ======*/
typedef struct {
    uint32_t trials;        //number of trials in cell
    uint64_t sum_steps;     //sum of steps in cell to [0,0]
    uint32_t succes_leq_k;  //number of successes within k steps in cell
}cell_stats_t;


#endif //SEMPRACA_TYPES_H
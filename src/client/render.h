//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_RENDER_H
#define SEMPRACA_RENDER_H

/**
 * @file render.h
 * @brief Client-side rendering helpers.
 *
 * These functions are intended to render the current simulation state in either
 * interactive or summary modes. (Currently stubbed.)
 */

/**
 * @brief Render the interactive view.
 * @return Nothing.
 */
void render_interactive(void);

/**
 * @brief Render summary mode as average steps per cell.
 * @return Nothing.
 */
void render_summary_avg(void);

/**
 * @brief Render summary mode as probability of success within K steps per cell.
 * @return Nothing.
 */
void render_summary_prob(void);

#endif //SEMPRACA_RENDER_H


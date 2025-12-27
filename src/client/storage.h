//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_STORAGE_H
#define SEMPRACA_STORAGE_H

/**
 * @file storage.h
 * @brief Persistence helpers for saving/loading simulation results.
 *
 * NOTE: This module is currently a stub; the declared functions are placeholders.
 */

/**
 * @brief Save current results to a file.
 *
 * @param filename Path to the output file.
 * @return 0 on success, -1 on failure.
 */
int save_results(const char *filename);

/**
 * @brief Load results from a file.
 *
 * @param filename Path to the input file.
 * @return 0 on success, -1 on failure.
 */
int load_results(const char *filename);

#endif //SEMPRACA_STORAGE_H


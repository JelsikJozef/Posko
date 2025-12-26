//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_RESULTS_H
#define SEMPRACA_RESULTS_H

void results_init(int cell_count);
void results_update(int cell_index, int steps, int success_leq_k);
double results_avg_steps(int cell_index);
double results_prob_leq_k(int cell_index);

#endif //SEMPRACA_RESULTS_H
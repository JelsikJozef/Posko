//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_TYPES_H
#define SEMPRACA_TYPES_H

#include <stdint.h>

typedef struct {
    int x;
    int y;
}position_t;

typedef struct {
    int width;
    int height;
}world_size_t;

typedef struct {
    double p_up;
    double p_down;
    double p_left;
    double p_right;
}move_probs_t;

#endif //SEMPRACA_TYPES_H
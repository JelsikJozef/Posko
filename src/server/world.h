//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORLD_H
#define SEMPRACA_WORLD_H

#include "../common/types.h"

void world_generate(world_size_t size);
int world_is_obstacle(int x, int y);
pos_t world_wrap(pos_t p);

#endif //SEMPRACA_WORLD_H
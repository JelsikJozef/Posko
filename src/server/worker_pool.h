//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORKER_POOL_H
#define SEMPRACA_WORKER_POOL_H

void worker_pool_init(int num_workers);
void worker_pool_submit_job(int replica, int cell_index);
void worker_pool_shutdown(void);


#endif //SEMPRACA_WORKER_POOL_H
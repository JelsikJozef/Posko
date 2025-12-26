//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_CONTEXT_H
#define SEMPRACA_SERVER_CONTEXT_H

#include <pthread.h>
#include <stdint.h>
#include "../common/types.h"

#define SERVER_MAX_CLIENTS 32

typedef struct {
    int fds[SERVER_MAX_CLIENTS];
    int count;
}client_list_t;

typedef struct server_context{
    //simulation parameters
    world_kinds_t world_kind;
    world_size_t world_size;
    move_probs_t probs;
    uint32_t k_max_steps;

    uint32_t total_reps;
    uint32_t current_rep;

    global_mode_t global_mode;

    //clients
    client_list_t clients;

    //synchronization
    pthread_mutex_t clients_mtx;
    pthread_mutex_t state_mtx; //protects current_rep and global_mode

}server_context_t;

/* init/destroy */
void server_context_init(server_context_t *ctx);
void server_context_destroy(server_context_t *ctx);

/*client management*/
int server_context_add_client(server_context_t *ctx, int client_fd);
void server_context_remove_client(server_context_t *ctx, int client_fd);

/*broadcast helper: sends message to all connected clients*/
typedef void(*client_fd_fn)(int fd, void *user);
void server_context_for_each_client(server_context_t *ctx, client_fd_fn fn, void *user);

/*global mode management*/
void server_context_set_mode(server_context_t *ctx, global_mode_t new_mode);
global_mode_t server_context_get_mode(server_context_t *ctx);

/*progress*/
void server_context_set_progress(server_context_t *ctx, uint32_t current_rep);
uint32_t server_context_get_progress(server_context_t *ctx);


#endif //SEMPRACA_SERVER_CONTEXT_H
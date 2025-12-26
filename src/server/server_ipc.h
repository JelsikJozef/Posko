//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_IPC_H
#define SEMPRACA_SERVER_IPC_H
#include <stddef.h>

void server_ipc_init(const char *socket_path);
void server_ipc_accept_loop(void);
void server_ipc_broadcast(void *msg, size_t size);


#endif //SEMPRACA_SERVER_IPC_H
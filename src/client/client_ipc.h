//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CLIENT_IPC_H
#define SEMPRACA_CLIENT_IPC_H

#include <stddef.h>

int client_connect(const char* socket_path);
void client_send(void *msg, size_t size);
void client_receive_loop(void);

#endif //SEMPRACA_CLIENT_IPC_H
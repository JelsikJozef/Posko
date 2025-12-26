//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CLIENT_CONTEXT_H
#define SEMPRACA_CLIENT_CONTEXT_H

#include "../common/protocol.h"

typedef struct {
    int socket_fd;
    global_mode_t global_mode;
    summary_view_t summary_view;
}client_context_t;

#endif //SEMPRACA_CLIENT_CONTEXT_H
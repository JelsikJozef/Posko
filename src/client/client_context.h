//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CLIENT_CONTEXT_H
#define SEMPRACA_CLIENT_CONTEXT_H

#include "../common/protocol.h"
#include "../common/types.h"

/**
 * @file client_context.h
 * @brief Client runtime state container.
 *
 * This header defines `client_context_t`, which holds the client-side connection
 * handle and a minimal set of UI/state preferences.
 */

/**
 * @brief Client runtime context.
 */
typedef struct {
    /** Connected socket FD to the server (>=0 when connected). */
    int socket_fd;

    /** Locally cached global mode (internal representation). */
    global_mode_t global_mode;

    /** Selected summary view when in summary mode. */
    summary_view_t summary_view;
} client_context_t;

#endif //SEMPRACA_CLIENT_CONTEXT_H
//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CLIENT_IPC_H
#define SEMPRACA_CLIENT_IPC_H

/*client_ipc.h - IPC layer of client(AF_UNIX socket)
 *
 *Responsibilities:
 *-connect to server socket
 *-send messages to server
 *-receive messages from server
 *
 *Does not containt client logic
 */

#include "../common/protocol.h"

/*connects to server socket at given path*/
int client_ipc_connect(const char* socket_path);

/*sends JOIN message to server*/
int client_ipc_send_join(int fd);

/*waits for WELCOME message from server; returns 0 if ok , else -1 */
int client_ipc_recv_welcome(int fd, rw_welcome_t *out_welcome);

#endif //SEMPRACA_CLIENT_IPC_H
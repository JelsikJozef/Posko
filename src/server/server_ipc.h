//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_SERVER_IPC_H
#define SEMPRACA_SERVER_IPC_H

/*server_ipc.h - IPC layer of server(AF_UNIX socket)
 *
 *Responsibilities:
 *-create socket
 *-accept client connections
 *-process basic requests(JOIN-> WELCOME)
 *
 *Does not containt simulation logic
 */

#include <pthread.h>

/*context declaration*/
struct server_context;

/*initializes socket + start of accept thread*/
int server_ipc_start(const char *socket_path, struct server_context *ctx);

/*correct shutdown of IPC layer*/
void server_ipc_stop(void);


#endif //SEMPRACA_SERVER_IPC_H
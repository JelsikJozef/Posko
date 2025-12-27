//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "ui_menu.h"

#include <stdio.h>

/**
 * @file client_main.c
 * @brief Minimal interactive client used to test the IPC protocol.
 *
 * The client connects to the server, sends JOIN, prints WELCOME parameters, and then
 * allows the user to change the global mode using single-key commands.
 */

/*
 * NOTE: Historical test code for single-key i/s/q control was removed.
 * The client now runs the full console menu in ui_menu_run().
 */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <socket_path>\n", argv[0]);
        return 1;
    }

    return ui_menu_run(argv[1]);
}

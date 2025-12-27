//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_UI_MENU_H
#define SEMPRACA_UI_MENU_H

/**
 * @file ui_menu.h
 * @brief Console menu for client (C9/C10).
 */

/**
 * @brief Run interactive menu.
 *
 * Connects to the server socket, performs JOIN, and then provides a console
 * menu to create/join/restart/save/quit.
 */
int ui_menu_run(const char *socket_path);

#endif //SEMPRACA_UI_MENU_H


//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_UTIL_H
#define SEMPRACA_UTIL_H
/*
 *util.h - simple helper functions for the project
 *
 *purpose is simpli logging and error handling
 */

/*logs an error message and exits the program*/
void die(const char *fmt, ...);

/*logs an informational message*/
void log_info(const char *fmt, ...);

/*logs a warning message*/
void log_error(const char *fmt, ...);

#endif //SEMPRACA_UTIL_H
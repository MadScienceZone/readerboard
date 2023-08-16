/*
 * Readerboard firmware
 *
 * Commands to control the readerboard via USB serial connection.
 *
 * Steve Willoughby steve@madscience.zone 2023
 */

#ifndef READERBOARD_COMMANDS
#define READERBOARD_COMMANDS
extern void setup_commands(void);
extern void receive_serial_data(void);
#endif
